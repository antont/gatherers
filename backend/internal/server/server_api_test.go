package server

import (
	"context"
	"encoding/json"
	"io"
	"net/http"
	"strings"
	"testing"
	"time"

	"github.com/antont/gatherers/backend/internal/config"
	"github.com/antont/gatherers/backend/internal/loadsim"
	"github.com/coder/websocket"
	"github.com/coder/websocket/wsjson"
)

func newAPITestTarget(t *testing.T) testTarget {
	t.Helper()
	srv := New(config.Config{})
	return newTestTarget(t, srv.Handler())
}

func TestAPIServerUsesSharedTargetAbstraction(t *testing.T) {
	target := newAPITestTarget(t)
	defer target.closeFn()

	if target.baseURL == "" {
		t.Fatal("expected API tests to resolve a target base URL")
	}
}

func TestSummaryReflectsEventsIngestedOverWebSocket(t *testing.T) {
	target := newAPITestTarget(t)
	defer target.closeFn()

	ctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)
	defer cancel()

	wsURL := strings.Replace(target.baseURL, "http://", "ws://", 1) + "/ws/ingest"
	conn, _, err := websocket.Dial(ctx, wsURL, nil)
	if err != nil {
		t.Fatalf("expected websocket ingest endpoint to accept connection: %v", err)
	}
	defer conn.Close(websocket.StatusNormalClosure, "")

	events := []map[string]any{
		{
			"type":         "sim_hello",
			"sim_id":       "sim-a",
			"seq":          1,
			"timestamp_ms": 1000,
			"payload": map[string]any{
				"sim_name": "local-a",
			},
		},
		{
			"type":         "food_drop",
			"sim_id":       "sim-a",
			"seq":          2,
			"timestamp_ms": 1001,
			"payload": map[string]any{
				"food_id": "food-1",
				"x":       10.0,
				"y":       10.0,
			},
		},
		{
			"type":         "food_drop",
			"sim_id":       "sim-a",
			"seq":          3,
			"timestamp_ms": 1002,
			"payload": map[string]any{
				"food_id": "food-2",
				"x":       12.0,
				"y":       12.0,
			},
		},
		{
			"type":         "food_pickup",
			"sim_id":       "sim-a",
			"seq":          4,
			"timestamp_ms": 1003,
			"payload": map[string]any{
				"food_id": "food-1",
			},
		},
	}

	for _, event := range events {
		if err := wsjson.Write(ctx, conn, event); err != nil {
			t.Fatalf("expected event %#v to be accepted over websocket: %v", event["type"], err)
		}
	}

	summary := waitForSummary(t, target.baseURL, 1500*time.Millisecond, func(summary struct {
		ConnectedSimCount int `json:"connected_sim_count"`
		LooseFoodCount    int `json:"loose_food_count"`
	}) bool {
		return summary.ConnectedSimCount == 1 && summary.LooseFoodCount == 1
	})
	if summary.ConnectedSimCount != 1 {
		t.Fatalf("expected 1 connected sim, got %d", summary.ConnectedSimCount)
	}

	if summary.LooseFoodCount != 1 {
		t.Fatalf("expected 1 loose food item after two drops and one pickup, got %d", summary.LooseFoodCount)
	}
}

func TestSummaryReadStartsDemandAndEventuallyRefreshesCachedSnapshot(t *testing.T) {
	target := newAPITestTarget(t)
	defer target.closeFn()

	ctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)
	defer cancel()

	err := loadsim.SendEvents(ctx, target.baseURL, []loadsim.Event{
		{
			Type:        "sim_hello",
			SimID:       "sim-lazy-api",
			Seq:         1,
			TimestampMS: 1000,
			Payload: map[string]any{
				"sim_name":   "lazy-api",
				"ant_count":  26,
				"food_count": 80,
			},
		},
		{
			Type:        "food_drop",
			SimID:       "sim-lazy-api",
			Seq:         2,
			TimestampMS: 1001,
			Payload: map[string]any{
				"food_id": "food-1",
				"x":       10.0,
				"y":       10.0,
			},
		},
	})
	if err != nil {
		t.Fatalf("expected ingest events to succeed before demand starts: %v", err)
	}

	initial := fetchSummary(t, target.baseURL)
	if initial.ConnectedSimCount != 0 || initial.LooseFoodCount != 0 {
		t.Fatalf("expected first summary read to serve stale cached data before demand-driven refresh, got %+v", initial)
	}

	deadline := time.Now().Add(1500 * time.Millisecond)
	last := initial
	for time.Now().Before(deadline) {
		last = fetchSummary(t, target.baseURL)
		if last.ConnectedSimCount == 1 && last.LooseFoodCount == 1 {
			return
		}
		time.Sleep(50 * time.Millisecond)
	}

	t.Fatalf("expected repeated summary reads to trigger eventual cached refresh, last summary was %+v", last)
}

func TestStartupFoodSnapshotSeedsLooseFoodState(t *testing.T) {
	target := newAPITestTarget(t)
	defer target.closeFn()

	ctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)
	defer cancel()

	wsURL := strings.Replace(target.baseURL, "http://", "ws://", 1) + "/ws/ingest"
	conn, _, err := websocket.Dial(ctx, wsURL, nil)
	if err != nil {
		t.Fatalf("expected websocket ingest endpoint to accept connection: %v", err)
	}
	defer conn.Close(websocket.StatusNormalClosure, "")

	events := []map[string]any{
		{
			"type":         "sim_hello",
			"sim_id":       "sim-startup",
			"seq":          1,
			"timestamp_ms": 1000,
			"payload":      map[string]any{"sim_name": "startup"},
		},
		{
			"type":         "sim_food_snapshot",
			"sim_id":       "sim-startup",
			"seq":          2,
			"timestamp_ms": 1001,
			"payload": map[string]any{
				"foods": []map[string]any{
					{"food_id": "food-1", "x": 10.0, "y": 20.0},
					{"food_id": "food-2", "x": 30.0, "y": 40.0},
					{"food_id": "food-3", "x": 50.0, "y": 60.0},
				},
			},
		},
	}

	for _, event := range events {
		if err := wsjson.Write(ctx, conn, event); err != nil {
			t.Fatalf("expected event %#v to be accepted over websocket: %v", event["type"], err)
		}
	}

	sims := waitForSimSummaries(t, target.baseURL, 1500*time.Millisecond, func(sims []simSummaryResponse) bool {
		return len(sims) == 1 && sims[0].SimID == "sim-startup" && sims[0].LooseFoodCount == 3
	})

	if len(sims) != 1 {
		t.Fatalf("expected one sim summary, got %d", len(sims))
	}
	if sims[0].SimID != "sim-startup" || sims[0].LooseFoodCount != 3 {
		t.Fatalf("expected startup snapshot to seed 3 loose food items, got %+v", sims[0])
	}

	summary := waitForSummary(t, target.baseURL, 1500*time.Millisecond, func(summary struct {
		ConnectedSimCount int `json:"connected_sim_count"`
		LooseFoodCount    int `json:"loose_food_count"`
	}) bool {
		return summary.ConnectedSimCount == 1 && summary.LooseFoodCount == 3
	})

	if summary.ConnectedSimCount != 1 || summary.LooseFoodCount != 3 {
		t.Fatalf("expected startup snapshot to count as 3 loose food items, got %+v", summary)
	}
}

func TestSummaryIncludesElapsedTimeAndEventRate(t *testing.T) {
	target := newAPITestTarget(t)
	defer target.closeFn()

	ctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)
	defer cancel()

	wsURL := strings.Replace(target.baseURL, "http://", "ws://", 1) + "/ws/ingest"
	conn, _, err := websocket.Dial(ctx, wsURL, nil)
	if err != nil {
		t.Fatalf("expected websocket ingest endpoint to accept connection: %v", err)
	}
	defer conn.Close(websocket.StatusNormalClosure, "")

	for _, event := range []map[string]any{
		{
			"type":         "sim_hello",
			"sim_id":       "sim-rate",
			"seq":          1,
			"timestamp_ms": 1000,
			"payload": map[string]any{
				"sim_name":   "rate",
				"ant_count":  26,
				"food_count": 80,
			},
		},
		{
			"type":         "food_drop",
			"sim_id":       "sim-rate",
			"seq":          2,
			"timestamp_ms": 1001,
			"payload": map[string]any{
				"food_id": "food-1",
				"x":       10.0,
				"y":       20.0,
			},
		},
	} {
		if err := wsjson.Write(ctx, conn, event); err != nil {
			t.Fatalf("expected event %#v to be accepted over websocket: %v", event["type"], err)
		}
	}

	summary := waitForExtendedSummary(t, target.baseURL, 1500*time.Millisecond, func(summary struct {
		ElapsedSeconds  float64 `json:"elapsed_seconds"`
		EventsPerSecond float64 `json:"events_per_second"`
	}) bool {
		return summary.ElapsedSeconds > 0 && summary.EventsPerSecond > 0
	})

	if summary.ElapsedSeconds <= 0 {
		t.Fatalf("expected elapsed_seconds to be positive, got %f", summary.ElapsedSeconds)
	}
	if summary.EventsPerSecond <= 0 {
		t.Fatalf("expected events_per_second to be positive, got %f", summary.EventsPerSecond)
	}
}

func TestSimHelloAntCountAppearsInPerSimSummary(t *testing.T) {
	target := newAPITestTarget(t)
	defer target.closeFn()

	ctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)
	defer cancel()

	wsURL := strings.Replace(target.baseURL, "http://", "ws://", 1) + "/ws/ingest"
	conn, _, err := websocket.Dial(ctx, wsURL, nil)
	if err != nil {
		t.Fatalf("expected websocket ingest endpoint to accept connection: %v", err)
	}
	defer conn.Close(websocket.StatusNormalClosure, "")

	event := map[string]any{
		"type":         "sim_hello",
		"sim_id":       "sim-meta",
		"seq":          1,
		"timestamp_ms": 1000,
		"payload": map[string]any{
			"sim_name":   "meta",
			"ant_count":  26,
			"food_count": 80,
		},
	}
	if err := wsjson.Write(ctx, conn, event); err != nil {
		t.Fatalf("expected sim_hello to be accepted over websocket: %v", err)
	}

	sims := waitForSimSummaries(t, target.baseURL, 1500*time.Millisecond, func(sims []simSummaryResponse) bool {
		return len(sims) == 1 && sims[0].SimID == "sim-meta" && sims[0].AntCount == 26
	})

	if len(sims) != 1 || sims[0].SimID != "sim-meta" || sims[0].AntCount != 26 {
		t.Fatalf("expected ant_count metadata to appear in sim summary, got %+v", sims)
	}
}

func TestDashboardShowsConnectedSimAndLooseFoodCounts(t *testing.T) {
	target := newAPITestTarget(t)
	defer target.closeFn()

	ctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)
	defer cancel()

	wsURL := strings.Replace(target.baseURL, "http://", "ws://", 1) + "/ws/ingest"
	conn, _, err := websocket.Dial(ctx, wsURL, nil)
	if err != nil {
		t.Fatalf("expected websocket ingest endpoint to accept connection: %v", err)
	}
	defer conn.Close(websocket.StatusNormalClosure, "")

	events := []map[string]any{
		{
			"type":         "sim_hello",
			"sim_id":       "sim-dashboard",
			"seq":          1,
			"timestamp_ms": 1000,
			"payload":      map[string]any{"sim_name": "dashboard"},
		},
		{
			"type":         "food_drop",
			"sim_id":       "sim-dashboard",
			"seq":          2,
			"timestamp_ms": 1001,
			"payload": map[string]any{
				"food_id": "food-1",
				"x":       25.0,
				"y":       25.0,
			},
		},
	}

	for _, event := range events {
		if err := wsjson.Write(ctx, conn, event); err != nil {
			t.Fatalf("expected event %#v to be accepted over websocket: %v", event["type"], err)
		}
	}

	resp, err := http.Get(target.baseURL + "/")
	if err != nil {
		t.Fatalf("expected dashboard endpoint to respond: %v", err)
	}
	defer resp.Body.Close()

	if resp.StatusCode != http.StatusOK {
		t.Fatalf("expected dashboard status %d, got %d", http.StatusOK, resp.StatusCode)
	}

	body, err := io.ReadAll(resp.Body)
	if err != nil {
		t.Fatalf("expected dashboard body to be readable: %v", err)
	}

	bodyText := string(body)
	if !strings.Contains(bodyText, "Connected sims") {
		t.Fatalf("expected dashboard to describe connected sims, body was %q", bodyText)
	}

	if !strings.Contains(bodyText, "connected-sims-value") || !strings.Contains(bodyText, "loose-food-value") {
		t.Fatalf("expected dashboard to include cached count containers, body was %q", bodyText)
	}
}

func waitForSummary(t *testing.T, baseURL string, timeout time.Duration, match func(struct {
	ConnectedSimCount int `json:"connected_sim_count"`
	LooseFoodCount    int `json:"loose_food_count"`
}) bool) struct {
	ConnectedSimCount int `json:"connected_sim_count"`
	LooseFoodCount    int `json:"loose_food_count"`
} {
	t.Helper()

	deadline := time.Now().Add(timeout)
	last := fetchSummary(t, baseURL)
	for {
		if match(last) {
			return last
		}
		if time.Now().After(deadline) {
			t.Fatalf("expected summary to converge before timeout, last summary was %+v", last)
		}
		time.Sleep(50 * time.Millisecond)
		last = fetchSummary(t, baseURL)
	}
}

func waitForExtendedSummary(t *testing.T, baseURL string, timeout time.Duration, match func(struct {
	ElapsedSeconds  float64 `json:"elapsed_seconds"`
	EventsPerSecond float64 `json:"events_per_second"`
}) bool) struct {
	ElapsedSeconds  float64 `json:"elapsed_seconds"`
	EventsPerSecond float64 `json:"events_per_second"`
} {
	t.Helper()

	deadline := time.Now().Add(timeout)
	last := fetchExtendedSummary(t, baseURL)
	for {
		if match(last) {
			return last
		}
		if time.Now().After(deadline) {
			t.Fatalf("expected extended summary to converge before timeout, last summary was %+v", last)
		}
		time.Sleep(50 * time.Millisecond)
		last = fetchExtendedSummary(t, baseURL)
	}
}

func fetchExtendedSummary(t *testing.T, baseURL string) struct {
	ElapsedSeconds  float64 `json:"elapsed_seconds"`
	EventsPerSecond float64 `json:"events_per_second"`
} {
	t.Helper()

	resp, err := http.Get(baseURL + "/api/summary")
	if err != nil {
		t.Fatalf("expected summary endpoint to respond: %v", err)
	}
	defer resp.Body.Close()

	var summary struct {
		ElapsedSeconds  float64 `json:"elapsed_seconds"`
		EventsPerSecond float64 `json:"events_per_second"`
	}
	if err := json.NewDecoder(resp.Body).Decode(&summary); err != nil {
		t.Fatalf("expected summary JSON to decode: %v", err)
	}
	return summary
}

func waitForSimSummaries(t *testing.T, baseURL string, timeout time.Duration, match func([]simSummaryResponse) bool) []simSummaryResponse {
	t.Helper()

	deadline := time.Now().Add(timeout)
	last := fetchSimSummaries(t, baseURL)
	for {
		if match(last) {
			return last
		}
		if time.Now().After(deadline) {
			t.Fatalf("expected sim summaries to converge before timeout, last summaries were %+v", last)
		}
		time.Sleep(50 * time.Millisecond)
		last = fetchSimSummaries(t, baseURL)
	}
}

func TestDashboardPageBootstrapsRealtimeWebsocketUi(t *testing.T) {
	target := newAPITestTarget(t)
	defer target.closeFn()

	resp, err := http.Get(target.baseURL + "/")
	if err != nil {
		t.Fatalf("expected dashboard endpoint to respond: %v", err)
	}
	defer resp.Body.Close()

	body, err := io.ReadAll(resp.Body)
	if err != nil {
		t.Fatalf("expected dashboard body to be readable: %v", err)
	}

	bodyText := string(body)
	for _, required := range []string{
		"/ws/dashboard",
		"connected-sims-value",
		"loose-food-value",
		"elapsed-seconds-value",
		"events-per-second-value",
		"sim-table-body",
		"sim.ant_count",
		"Ants",
	} {
		if !strings.Contains(bodyText, required) {
			t.Fatalf("expected dashboard HTML to contain %q, body was %q", required, bodyText)
		}
	}
}
