package server

import (
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"net/http"
	"sync"
	"time"

	"github.com/antont/gatherers/backend/internal/config"
	"github.com/antont/gatherers/backend/internal/state"
	"github.com/coder/websocket"
	"github.com/coder/websocket/wsjson"
)

type Server struct {
	cfg   config.Config
	store *state.Store
	hub   *dashboardHub

	snapshotMu     sync.RWMutex
	cachedSnapshot dashboardSnapshot

	refreshMu         sync.Mutex
	dirty             bool
	dashboardWatchers int
	lastAPIReadAt     time.Time
	nextEligibleAt    time.Time

	refreshCh    chan struct{}
	workerOnce   sync.Once
	closeOnce    sync.Once
	workerCancel context.CancelFunc

	now                func() time.Time
	minRefreshInterval time.Duration
	maxRefreshInterval time.Duration
	refreshMultiplier  int
	apiDemandTTL       time.Duration
}

func New(cfg config.Config) *Server {
	return &Server{
		cfg:                cfg,
		store:              state.NewStore(50),
		hub:                newDashboardHub(),
		refreshCh:          make(chan struct{}, 1),
		now:                time.Now,
		minRefreshInterval: 250 * time.Millisecond,
		maxRefreshInterval: 5 * time.Second,
		refreshMultiplier:  3,
		apiDemandTTL:       10 * time.Second,
	}
}

func (s *Server) Close() {
	s.closeOnce.Do(func() {
		if s.workerCancel != nil {
			s.workerCancel()
		}
	})
}

func (s *Server) Handler() http.Handler {
	mux := http.NewServeMux()
	mux.HandleFunc("/", s.handleDashboard)
	mux.HandleFunc("/api/sims", s.handleSims)
	mux.HandleFunc("/api/summary", s.handleSummary)
	mux.HandleFunc("/ws/dashboard", s.handleDashboardWS)
	mux.HandleFunc("/ws/ingest", s.handleIngest)
	return mux
}

func (s *Server) Run(ctx context.Context) error {
	defer s.Close()

	httpServer := &http.Server{
		Addr:    s.cfg.Addr,
		Handler: s.Handler(),
	}

	errCh := make(chan error, 1)
	go func() {
		errCh <- httpServer.ListenAndServe()
	}()

	select {
	case <-ctx.Done():
		shutdownErr := httpServer.Shutdown(context.Background())
		if shutdownErr != nil {
			return shutdownErr
		}
		err := <-errCh
		if errors.Is(err, http.ErrServerClosed) {
			return nil
		}
		return err
	case err := <-errCh:
		if errors.Is(err, http.ErrServerClosed) {
			return nil
		}
		return err
	}
}

type ingestEnvelope struct {
	Type    string          `json:"type"`
	SimID   string          `json:"sim_id"`
	Payload json.RawMessage `json:"payload"`
}

type foodDropPayload struct {
	FoodID string  `json:"food_id"`
	X      float64 `json:"x"`
	Y      float64 `json:"y"`
}

type helloPayload struct {
	SimName   string `json:"sim_name"`
	AntCount  int    `json:"ant_count"`
	FoodCount int    `json:"food_count"`
}

type foodPickupPayload struct {
	FoodID string `json:"food_id"`
}

type foodSnapshotPayload struct {
	Foods []foodDropPayload `json:"foods"`
}

type dashboardSnapshot struct {
	Summary state.Summary      `json:"summary"`
	Sims    []state.SimSummary `json:"sims"`
}

type dashboardHub struct {
	mu      sync.Mutex
	clients map[chan dashboardSnapshot]struct{}
}

func (s *Server) handleSims(w http.ResponseWriter, _ *http.Request) {
	snapshot := s.currentSnapshot()
	s.registerAPIDemand()
	w.Header().Set("Content-Type", "application/json")
	_ = json.NewEncoder(w).Encode(snapshot.Sims)
}

func (s *Server) handleSummary(w http.ResponseWriter, _ *http.Request) {
	snapshot := s.currentSnapshot()
	s.registerAPIDemand()
	w.Header().Set("Content-Type", "application/json")
	_ = json.NewEncoder(w).Encode(snapshot.Summary)
}

func (s *Server) handleDashboard(w http.ResponseWriter, _ *http.Request) {
	summary := s.currentSnapshot().Summary
	w.Header().Set("Content-Type", "text/html; charset=utf-8")
	_, _ = fmt.Fprintf(w, `<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Gatherers Backend</title>
  <style>
    :root {
      color-scheme: dark;
      --bg: #111827;
      --panel: #1f2937;
      --panel-alt: #0f172a;
      --text: #e5e7eb;
      --muted: #94a3b8;
      --accent: #34d399;
      --border: #334155;
    }
    body {
      margin: 0;
      font-family: ui-sans-serif, system-ui, sans-serif;
      background: var(--bg);
      color: var(--text);
    }
    main {
      max-width: 1100px;
      margin: 0 auto;
      padding: 24px;
    }
    .cards {
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(170px, 1fr));
      gap: 16px;
      margin: 24px 0;
    }
    .card, .panel {
      background: var(--panel);
      border: 1px solid var(--border);
      border-radius: 12px;
      padding: 16px;
    }
    .label {
      color: var(--muted);
      font-size: 0.9rem;
      margin-bottom: 8px;
    }
    .value {
      font-size: 2rem;
      font-weight: 700;
    }
    table {
      width: 100%%;
      border-collapse: collapse;
    }
    th, td {
      padding: 10px 12px;
      text-align: left;
      border-bottom: 1px solid var(--border);
    }
    th {
      color: var(--muted);
      font-size: 0.85rem;
      text-transform: uppercase;
      letter-spacing: 0.04em;
    }
    .status {
      color: var(--muted);
      margin-top: 8px;
    }
    .accent {
      color: var(--accent);
    }
  </style>
</head>
<body>
  <main>
    <h1>Gatherers Backend</h1>
    <p class="status">Realtime dashboard fed by <span class="accent">/ws/dashboard</span>.</p>
    <section class="cards">
      <div class="card">
        <div class="label">Connected sims</div>
        <div class="value" id="connected-sims-value">%d</div>
      </div>
      <div class="card">
        <div class="label">Loose food</div>
        <div class="value" id="loose-food-value">%d</div>
      </div>
      <div class="card">
        <div class="label">Occupied cells</div>
        <div class="value" id="occupied-cells-value">%d</div>
      </div>
      <div class="card">
        <div class="label">Mean nearest-neighbor distance</div>
        <div class="value" id="nearest-neighbor-value">%.2f</div>
      </div>
      <div class="card">
        <div class="label">Elapsed time</div>
        <div class="value" id="elapsed-seconds-value">%.1fs</div>
      </div>
      <div class="card">
        <div class="label">Events/sec</div>
        <div class="value" id="events-per-second-value">%.2f</div>
      </div>
    </section>
    <section class="panel">
      <div class="label">Last update</div>
      <div id="last-updated-value">server-rendered snapshot</div>
    </section>
    <section class="panel" style="margin-top: 16px;">
      <div class="label">Connected sims</div>
      <table>
        <thead>
          <tr>
            <th>Sim</th>
            <th>Ants</th>
            <th>Drops</th>
            <th>Pickups</th>
            <th>Moves</th>
            <th>Loose food</th>
          </tr>
        </thead>
        <tbody id="sim-table-body">
          <tr><td colspan="6">Waiting for realtime data...</td></tr>
        </tbody>
      </table>
    </section>
  </main>
  <script>
    const connectedSimsValue = document.getElementById("connected-sims-value");
    const looseFoodValue = document.getElementById("loose-food-value");
    const occupiedCellsValue = document.getElementById("occupied-cells-value");
    const nearestNeighborValue = document.getElementById("nearest-neighbor-value");
    const elapsedSecondsValue = document.getElementById("elapsed-seconds-value");
    const eventsPerSecondValue = document.getElementById("events-per-second-value");
    const lastUpdatedValue = document.getElementById("last-updated-value");
    const simTableBody = document.getElementById("sim-table-body");

    function render(snapshot) {
      connectedSimsValue.textContent = String(snapshot.summary.connected_sim_count ?? 0);
      looseFoodValue.textContent = String(snapshot.summary.loose_food_count ?? 0);
      occupiedCellsValue.textContent = String(snapshot.summary.occupied_cell_count ?? 0);
      nearestNeighborValue.textContent = Number(snapshot.summary.nearest_neighbor_mean_distance ?? 0).toFixed(2);
      elapsedSecondsValue.textContent = Number(snapshot.summary.elapsed_seconds ?? 0).toFixed(1) + "s";
      eventsPerSecondValue.textContent = Number(snapshot.summary.events_per_second ?? 0).toFixed(2);
      lastUpdatedValue.textContent = new Date().toLocaleTimeString();

      const sims = [...(snapshot.sims ?? [])].sort((a, b) => a.sim_id.localeCompare(b.sim_id));
      if (sims.length === 0) {
        simTableBody.innerHTML = '<tr><td colspan="6">No sims connected yet.</td></tr>';
        return;
      }

      simTableBody.innerHTML = sims.map((sim) => {
        return '<tr>' +
          '<td>' + sim.sim_id + '</td>' +
          '<td>' + (sim.ant_count ?? 0) + '</td>' +
          '<td>' + sim.drop_count + '</td>' +
          '<td>' + sim.pickup_count + '</td>' +
          '<td>' + sim.turn_move_count + '</td>' +
          '<td>' + sim.loose_food_count + '</td>' +
          '</tr>';
      }).join('');
    }

    const scheme = window.location.protocol === "https:" ? "wss" : "ws";
    const socket = new WebSocket(scheme + "://" + window.location.host + "/ws/dashboard");
    socket.addEventListener("message", (event) => {
      render(JSON.parse(event.data));
    });
    socket.addEventListener("close", () => {
      lastUpdatedValue.textContent = "dashboard websocket disconnected";
    });
  </script>
</body>
</html>`, summary.ConnectedSimCount, summary.LooseFoodCount, summary.OccupiedCellCount, summary.NearestNeighborMeanDistance, summary.ElapsedSeconds, summary.EventsPerSecond)
}

func (s *Server) handleDashboardWS(w http.ResponseWriter, r *http.Request) {
	conn, err := websocket.Accept(w, r, nil)
	if err != nil {
		return
	}
	defer conn.Close(websocket.StatusNormalClosure, "")

	initialSnapshot := s.currentSnapshot()
	updates, unsubscribe := s.hub.subscribe()
	defer unsubscribe()

	if err := wsjson.Write(r.Context(), conn, initialSnapshot); err != nil {
		return
	}

	s.addDashboardWatcher(1)
	defer s.addDashboardWatcher(-1)

	for {
		select {
		case <-r.Context().Done():
			return
		case snapshot, ok := <-updates:
			if !ok {
				return
			}
			if err := wsjson.Write(r.Context(), conn, snapshot); err != nil {
				return
			}
		}
	}
}

func (s *Server) handleIngest(w http.ResponseWriter, r *http.Request) {
	conn, err := websocket.Accept(w, r, nil)
	if err != nil {
		return
	}
	defer conn.Close(websocket.StatusNormalClosure, "")

	ctx := r.Context()
	for {
		var event ingestEnvelope
		if err := wsjson.Read(ctx, conn, &event); err != nil {
			return
		}

		switch event.Type {
		case "sim_hello":
			var payload helloPayload
			if len(event.Payload) > 0 {
				if err := json.Unmarshal(event.Payload, &payload); err != nil {
					return
				}
			}
			s.store.RecordHello(event.SimID, payload.AntCount)
			s.markSnapshotDirty()
		case "sim_food_snapshot":
			var payload foodSnapshotPayload
			if err := json.Unmarshal(event.Payload, &payload); err != nil {
				return
			}
			foods := make([]state.FoodDrop, 0, len(payload.Foods))
			for _, food := range payload.Foods {
				foods = append(foods, state.FoodDrop{
					FoodID: food.FoodID,
					X:      food.X,
					Y:      food.Y,
				})
			}
			s.store.RecordFoodSnapshot(event.SimID, foods)
			s.markSnapshotDirty()
		case "food_drop":
			var payload foodDropPayload
			if err := json.Unmarshal(event.Payload, &payload); err != nil {
				return
			}
			s.store.RecordDrop(event.SimID, state.FoodDrop{
				FoodID: payload.FoodID,
				X:      payload.X,
				Y:      payload.Y,
			})
			s.markSnapshotDirty()
		case "food_pickup":
			var payload foodPickupPayload
			if err := json.Unmarshal(event.Payload, &payload); err != nil {
				return
			}
			s.store.RecordPickup(event.SimID, state.FoodPickup{
				FoodID: payload.FoodID,
			})
			s.markSnapshotDirty()
		case "ant_turn_move":
			s.store.RecordTurnMove(event.SimID)
			s.markSnapshotDirty()
		}
	}
}

func newDashboardHub() *dashboardHub {
	return &dashboardHub{
		clients: make(map[chan dashboardSnapshot]struct{}),
	}
}

func (h *dashboardHub) subscribe() (<-chan dashboardSnapshot, func()) {
	ch := make(chan dashboardSnapshot, 1)

	h.mu.Lock()
	h.clients[ch] = struct{}{}
	h.mu.Unlock()

	return ch, func() {
		h.mu.Lock()
		if _, ok := h.clients[ch]; ok {
			delete(h.clients, ch)
			close(ch)
		}
		h.mu.Unlock()
	}
}

func (h *dashboardHub) broadcast(snapshot dashboardSnapshot) {
	h.mu.Lock()
	defer h.mu.Unlock()

	for ch := range h.clients {
		select {
		case ch <- snapshot:
		default:
			select {
			case <-ch:
			default:
			}
			select {
			case ch <- snapshot:
			default:
			}
		}
	}
}

func (s *Server) currentSnapshot() dashboardSnapshot {
	s.snapshotMu.RLock()
	defer s.snapshotMu.RUnlock()
	return s.cachedSnapshot
}

func (s *Server) replaceSnapshot(snapshot dashboardSnapshot) {
	s.snapshotMu.Lock()
	s.cachedSnapshot = snapshot
	s.snapshotMu.Unlock()
}

func (s *Server) markSnapshotDirty() {
	s.refreshMu.Lock()
	s.dirty = true
	hasDemand := s.hasDemandLocked(s.now())
	s.refreshMu.Unlock()

	if hasDemand {
		s.ensureWorker()
		s.signalRefresh()
	}
}

func (s *Server) registerAPIDemand() {
	s.refreshMu.Lock()
	s.lastAPIReadAt = s.now()
	s.refreshMu.Unlock()

	s.ensureWorker()
	s.signalRefresh()
}

func (s *Server) addDashboardWatcher(delta int) {
	s.refreshMu.Lock()
	s.dashboardWatchers += delta
	if s.dashboardWatchers < 0 {
		s.dashboardWatchers = 0
	}
	s.refreshMu.Unlock()

	if delta > 0 {
		s.ensureWorker()
		s.signalRefresh()
	}
}

func (s *Server) ensureWorker() {
	s.workerOnce.Do(func() {
		ctx, cancel := context.WithCancel(context.Background())
		s.workerCancel = cancel
		go s.runSnapshotWorker(ctx)
	})
}

func (s *Server) signalRefresh() {
	select {
	case s.refreshCh <- struct{}{}:
	default:
	}
}

func (s *Server) runSnapshotWorker(ctx context.Context) {
	for {
		select {
		case <-ctx.Done():
			return
		case <-s.refreshCh:
			for {
				delay, shouldWait, shouldRefresh := s.nextRefreshPlan(s.now())
				if !shouldRefresh {
					break
				}
				if shouldWait {
					timer := time.NewTimer(delay)
					select {
					case <-ctx.Done():
						if !timer.Stop() {
							<-timer.C
						}
						return
					case <-s.refreshCh:
						if !timer.Stop() {
							<-timer.C
						}
						continue
					case <-timer.C:
					}
				}

				if !s.claimDirtyRefresh(s.now()) {
					continue
				}

				started := s.now()
				snapshot := s.computeDashboardSnapshot()
				computeDuration := s.now().Sub(started)
				s.replaceSnapshot(snapshot)
				s.finishRefresh(computeDuration)
				s.hub.broadcast(snapshot)
			}
		}
	}
}

func (s *Server) nextRefreshPlan(now time.Time) (delay time.Duration, shouldWait bool, shouldRefresh bool) {
	s.refreshMu.Lock()
	defer s.refreshMu.Unlock()

	if !s.hasDemandLocked(now) || !s.dirty {
		return 0, false, false
	}
	if !s.nextEligibleAt.IsZero() && now.Before(s.nextEligibleAt) {
		return s.nextEligibleAt.Sub(now), true, true
	}
	return 0, false, true
}

func (s *Server) claimDirtyRefresh(now time.Time) bool {
	s.refreshMu.Lock()
	defer s.refreshMu.Unlock()

	if !s.hasDemandLocked(now) || !s.dirty {
		return false
	}
	if !s.nextEligibleAt.IsZero() && now.Before(s.nextEligibleAt) {
		return false
	}
	s.dirty = false
	return true
}

func (s *Server) finishRefresh(computeDuration time.Duration) {
	s.refreshMu.Lock()
	s.nextEligibleAt = s.now().Add(s.adaptiveRefreshInterval(computeDuration))
	s.refreshMu.Unlock()
}

func (s *Server) adaptiveRefreshInterval(computeDuration time.Duration) time.Duration {
	interval := computeDuration * time.Duration(s.refreshMultiplier)
	if interval < s.minRefreshInterval {
		interval = s.minRefreshInterval
	}
	if interval > s.maxRefreshInterval {
		interval = s.maxRefreshInterval
	}
	return interval
}

func (s *Server) hasDemandLocked(now time.Time) bool {
	if s.dashboardWatchers > 0 {
		return true
	}
	if s.lastAPIReadAt.IsZero() {
		return false
	}
	return now.Sub(s.lastAPIReadAt) <= s.apiDemandTTL
}

func (s *Server) computeDashboardSnapshot() dashboardSnapshot {
	data := s.store.DashboardSnapshotData()
	return dashboardSnapshot{
		Summary: data.Summary(s.now()),
		Sims:    data.SimSummaries(),
	}
}
