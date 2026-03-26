package server

import (
	"context"
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"slices"
	"strings"
	"testing"
	"time"

	"github.com/antont/gatherers/backend/internal/config"
	"github.com/coder/websocket"
	"github.com/coder/websocket/wsjson"
)

func TestFakeClientsPopulatePerSimSummaries(t *testing.T) {
	srv := New(config.Config{})
	testServer := httptest.NewServer(srv.Handler())
	defer testServer.Close()

	ctx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
	defer cancel()

	sendClientEvents(t, ctx, testServer.URL, []map[string]any{
		{
			"type":         "sim_hello",
			"sim_id":       "sim-a",
			"seq":          1,
			"timestamp_ms": 1000,
			"payload":      map[string]any{"sim_name": "alpha"},
		},
		{
			"type":         "food_drop",
			"sim_id":       "sim-a",
			"seq":          2,
			"timestamp_ms": 1001,
			"payload": map[string]any{
				"food_id": "food-a1",
				"x":       10.0,
				"y":       10.0,
			},
		},
		{
			"type":         "ant_turn_move",
			"sim_id":       "sim-a",
			"seq":          3,
			"timestamp_ms": 1002,
			"payload": map[string]any{
				"ant_id":       "ant-a1",
				"x":            11.0,
				"y":            12.0,
				"direction_x":  0.5,
				"direction_y":  0.5,
				"frame":        3,
			},
		},
	})

	sendClientEvents(t, ctx, testServer.URL, []map[string]any{
		{
			"type":         "sim_hello",
			"sim_id":       "sim-b",
			"seq":          1,
			"timestamp_ms": 2000,
			"payload":      map[string]any{"sim_name": "beta"},
		},
		{
			"type":         "food_drop",
			"sim_id":       "sim-b",
			"seq":          2,
			"timestamp_ms": 2001,
			"payload": map[string]any{
				"food_id": "food-b1",
				"x":       100.0,
				"y":       50.0,
			},
		},
		{
			"type":         "food_pickup",
			"sim_id":       "sim-b",
			"seq":          3,
			"timestamp_ms": 2002,
			"payload": map[string]any{
				"food_id": "food-b1",
			},
		},
	})

	resp, err := http.Get(testServer.URL + "/api/sims")
	if err != nil {
		t.Fatalf("expected sims endpoint to respond: %v", err)
	}
	defer resp.Body.Close()

	if resp.StatusCode != http.StatusOK {
		t.Fatalf("expected sims endpoint status %d, got %d", http.StatusOK, resp.StatusCode)
	}

	var sims []struct {
		SimID         string `json:"sim_id"`
		PickupCount   int    `json:"pickup_count"`
		DropCount     int    `json:"drop_count"`
		TurnMoveCount int    `json:"turn_move_count"`
		LooseFoodCount int   `json:"loose_food_count"`
	}
	if err := json.NewDecoder(resp.Body).Decode(&sims); err != nil {
		t.Fatalf("expected sims JSON to decode: %v", err)
	}

	if len(sims) != 2 {
		t.Fatalf("expected 2 sim summaries, got %d", len(sims))
	}

	slices.SortFunc(sims, func(a, b struct {
		SimID         string `json:"sim_id"`
		PickupCount   int    `json:"pickup_count"`
		DropCount     int    `json:"drop_count"`
		TurnMoveCount int    `json:"turn_move_count"`
		LooseFoodCount int   `json:"loose_food_count"`
	}) int {
		return strings.Compare(a.SimID, b.SimID)
	})

	if sims[0].SimID != "sim-a" || sims[0].DropCount != 1 || sims[0].TurnMoveCount != 1 || sims[0].LooseFoodCount != 1 {
		t.Fatalf("unexpected sim-a summary: %+v", sims[0])
	}

	if sims[1].SimID != "sim-b" || sims[1].DropCount != 1 || sims[1].PickupCount != 1 || sims[1].LooseFoodCount != 0 {
		t.Fatalf("unexpected sim-b summary: %+v", sims[1])
	}
}

func sendClientEvents(t *testing.T, ctx context.Context, baseURL string, events []map[string]any) {
	t.Helper()

	wsURL := strings.Replace(baseURL, "http://", "ws://", 1) + "/ws/ingest"
	conn, _, err := websocket.Dial(ctx, wsURL, nil)
	if err != nil {
		t.Fatalf("expected websocket ingest endpoint to accept connection: %v", err)
	}
	defer conn.Close(websocket.StatusNormalClosure, "")

	for _, event := range events {
		if err := wsjson.Write(ctx, conn, event); err != nil {
			t.Fatalf("expected event %#v to be accepted over websocket: %v", event["type"], err)
		}
	}
}
