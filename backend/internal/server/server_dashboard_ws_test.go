package server

import (
	"context"
	"strings"
	"testing"
	"time"

	"github.com/antont/gatherers/backend/internal/config"
	"github.com/antont/gatherers/backend/internal/loadsim"
	"github.com/coder/websocket"
	"github.com/coder/websocket/wsjson"
)

func TestDashboardWebsocketReceivesUpdatedSnapshotAfterIngest(t *testing.T) {
	srv := New(config.Config{})
	target := newTestTarget(t, srv.Handler())
	defer target.closeFn()

	ctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)
	defer cancel()

	dashboardConn, _, err := websocket.Dial(ctx, websocketURL(target.baseURL, "/ws/dashboard"), nil)
	if err != nil {
		t.Fatalf("expected dashboard websocket endpoint to accept connection: %v", err)
	}
	defer dashboardConn.Close(websocket.StatusNormalClosure, "")

	var initial struct {
		Summary struct {
			ConnectedSimCount int `json:"connected_sim_count"`
		} `json:"summary"`
	}
	if err := wsjson.Read(ctx, dashboardConn, &initial); err != nil {
		t.Fatalf("expected initial dashboard snapshot: %v", err)
	}

	if initial.Summary.ConnectedSimCount != 0 {
		t.Fatalf("expected initial connected sim count to be 0, got %d", initial.Summary.ConnectedSimCount)
	}

	err = loadsim.SendEvents(ctx, target.baseURL, []loadsim.Event{
		{
			Type:        "sim_hello",
			SimID:       "sim-dashboard",
			Seq:         1,
			TimestampMS: 1000,
			Payload:     map[string]any{"sim_name": "dashboard"},
		},
		{
			Type:        "food_drop",
			SimID:       "sim-dashboard",
			Seq:         2,
			TimestampMS: 1001,
			Payload: map[string]any{
				"food_id": "food-1",
				"x":       25.0,
				"y":       25.0,
			},
		},
	})
	if err != nil {
		t.Fatalf("expected ingest events to succeed: %v", err)
	}

	update := waitForDashboardSnapshot(t, ctx, dashboardConn, func(snapshot dashboardSnapshotMessage) bool {
		return snapshot.Summary.ConnectedSimCount == 1 &&
			snapshot.Summary.LooseFoodCount == 1 &&
			len(snapshot.Sims) == 1 &&
			snapshot.Sims[0].SimID == "sim-dashboard" &&
			snapshot.Sims[0].DropCount == 1 &&
			snapshot.Sims[0].LooseFoodCount == 1
	})

	if update.Summary.ConnectedSimCount != 1 {
		t.Fatalf("expected connected sim count 1, got %d", update.Summary.ConnectedSimCount)
	}
	if update.Summary.LooseFoodCount != 1 {
		t.Fatalf("expected loose food count 1, got %d", update.Summary.LooseFoodCount)
	}
	if len(update.Sims) != 1 || update.Sims[0].SimID != "sim-dashboard" || update.Sims[0].DropCount != 1 || update.Sims[0].LooseFoodCount != 1 {
		t.Fatalf("unexpected dashboard sim snapshot: %+v", update.Sims)
	}
}

type dashboardSnapshotMessage struct {
	Summary struct {
		ConnectedSimCount int `json:"connected_sim_count"`
		LooseFoodCount    int `json:"loose_food_count"`
	} `json:"summary"`
	Sims []struct {
		SimID          string `json:"sim_id"`
		DropCount      int    `json:"drop_count"`
		LooseFoodCount int    `json:"loose_food_count"`
	} `json:"sims"`
}

func waitForDashboardSnapshot(t *testing.T, ctx context.Context, conn *websocket.Conn, match func(dashboardSnapshotMessage) bool) dashboardSnapshotMessage {
	t.Helper()

	for {
		var snapshot dashboardSnapshotMessage
		if err := wsjson.Read(ctx, conn, &snapshot); err != nil {
			t.Fatalf("expected matching dashboard snapshot: %v", err)
		}
		if match(snapshot) {
			return snapshot
		}
	}
}

func websocketURL(baseURL, path string) string {
	switch {
	case strings.HasPrefix(baseURL, "https://"):
		return "wss://" + strings.TrimPrefix(baseURL, "https://") + path
	default:
		return "ws://" + strings.TrimPrefix(baseURL, "http://") + path
	}
}
