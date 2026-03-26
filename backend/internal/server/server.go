package server

import (
	"context"
	"encoding/json"
	"errors"
	"net/http"
	"strconv"

	"github.com/antont/gatherers/backend/internal/config"
	"github.com/antont/gatherers/backend/internal/state"
	"github.com/coder/websocket"
	"github.com/coder/websocket/wsjson"
)

type Server struct {
	cfg config.Config
	store *state.Store
}

func New(cfg config.Config) *Server {
	return &Server{
		cfg:   cfg,
		store: state.NewStore(50),
	}
}

func (s *Server) Handler() http.Handler {
	mux := http.NewServeMux()
	mux.HandleFunc("/", s.handleDashboard)
	mux.HandleFunc("/api/summary", s.handleSummary)
	mux.HandleFunc("/ws/ingest", s.handleIngest)
	return mux
}

func (s *Server) Run(ctx context.Context) error {
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

type foodPickupPayload struct {
	FoodID string `json:"food_id"`
}

func (s *Server) handleSummary(w http.ResponseWriter, _ *http.Request) {
	w.Header().Set("Content-Type", "application/json")
	_ = json.NewEncoder(w).Encode(s.store.GlobalSummary())
}

func (s *Server) handleDashboard(w http.ResponseWriter, _ *http.Request) {
	summary := s.store.GlobalSummary()
	w.Header().Set("Content-Type", "text/html; charset=utf-8")
	_, _ = w.Write([]byte("<!doctype html><html><body>" +
		"<h1>Gatherers Backend</h1>" +
		"<p>Connected sims: " + strconv.Itoa(summary.ConnectedSimCount) + "</p>" +
		"<p>Loose food: " + strconv.Itoa(summary.LooseFoodCount) + "</p>" +
		"</body></html>"))
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
			s.store.RecordHello(event.SimID)
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
		case "food_pickup":
			var payload foodPickupPayload
			if err := json.Unmarshal(event.Payload, &payload); err != nil {
				return
			}
			s.store.RecordPickup(event.SimID, state.FoodPickup{
				FoodID: payload.FoodID,
			})
		}
	}
}
