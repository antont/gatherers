package server

import (
	"context"
	"encoding/json"
	"io"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
	"time"

	"github.com/antont/gatherers/backend/internal/config"
	"github.com/coder/websocket"
	"github.com/coder/websocket/wsjson"
)

func TestSummaryReflectsEventsIngestedOverWebSocket(t *testing.T) {
	srv := New(config.Config{})
	testServer := httptest.NewServer(srv.Handler())
	defer testServer.Close()

	ctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)
	defer cancel()

	wsURL := strings.Replace(testServer.URL, "http://", "ws://", 1) + "/ws/ingest"
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

	resp, err := http.Get(testServer.URL + "/api/summary")
	if err != nil {
		t.Fatalf("expected summary endpoint to respond: %v", err)
	}
	defer resp.Body.Close()

	if resp.StatusCode != http.StatusOK {
		t.Fatalf("expected summary endpoint status %d, got %d", http.StatusOK, resp.StatusCode)
	}

	var summary struct {
		ConnectedSimCount int `json:"connected_sim_count"`
		LooseFoodCount    int `json:"loose_food_count"`
	}
	if err := json.NewDecoder(resp.Body).Decode(&summary); err != nil {
		t.Fatalf("expected summary JSON to decode: %v", err)
	}

	if summary.ConnectedSimCount != 1 {
		t.Fatalf("expected 1 connected sim, got %d", summary.ConnectedSimCount)
	}

	if summary.LooseFoodCount != 1 {
		t.Fatalf("expected 1 loose food item after two drops and one pickup, got %d", summary.LooseFoodCount)
	}
}

func TestDashboardShowsConnectedSimAndLooseFoodCounts(t *testing.T) {
	srv := New(config.Config{})
	testServer := httptest.NewServer(srv.Handler())
	defer testServer.Close()

	ctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)
	defer cancel()

	wsURL := strings.Replace(testServer.URL, "http://", "ws://", 1) + "/ws/ingest"
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

	resp, err := http.Get(testServer.URL + "/")
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

	if !strings.Contains(bodyText, "1") {
		t.Fatalf("expected dashboard to include the connected sim or loose food counts, body was %q", bodyText)
	}
}
