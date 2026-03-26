package server

import (
	"context"
	"slices"
	"strings"
	"testing"
	"time"

	"github.com/antont/gatherers/backend/internal/config"
	"github.com/antont/gatherers/backend/internal/loadsim"
)

func TestFakeClientsPopulatePerSimSummaries(t *testing.T) {
	srv := New(config.Config{})
	target := newTestTarget(t, srv.Handler())
	defer target.closeFn()

	ctx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
	defer cancel()

	sendClientEvents(t, ctx, target.baseURL, []loadsim.Event{
		{
			Type:        "sim_hello",
			SimID:       "sim-a",
			Seq:         1,
			TimestampMS: 1000,
			Payload:     map[string]any{"sim_name": "alpha"},
		},
		{
			Type:        "food_drop",
			SimID:       "sim-a",
			Seq:         2,
			TimestampMS: 1001,
			Payload: map[string]any{
				"food_id": "food-a1",
				"x":       10.0,
				"y":       10.0,
			},
		},
		{
			Type:        "ant_turn_move",
			SimID:       "sim-a",
			Seq:         3,
			TimestampMS: 1002,
			Payload: map[string]any{
				"ant_id":      "ant-a1",
				"x":           11.0,
				"y":           12.0,
				"direction_x": 0.5,
				"direction_y": 0.5,
				"frame":       3,
			},
		},
	})

	sendClientEvents(t, ctx, target.baseURL, []loadsim.Event{
		{
			Type:        "sim_hello",
			SimID:       "sim-b",
			Seq:         1,
			TimestampMS: 2000,
			Payload:     map[string]any{"sim_name": "beta"},
		},
		{
			Type:        "food_drop",
			SimID:       "sim-b",
			Seq:         2,
			TimestampMS: 2001,
			Payload: map[string]any{
				"food_id": "food-b1",
				"x":       100.0,
				"y":       50.0,
			},
		},
		{
			Type:        "food_pickup",
			SimID:       "sim-b",
			Seq:         3,
			TimestampMS: 2002,
			Payload: map[string]any{
				"food_id": "food-b1",
			},
		},
	})

	sims := waitForSimSummaries(t, target.baseURL, 1500*time.Millisecond, func(sims []simSummaryResponse) bool {
		if len(sims) < 2 {
			return false
		}
		simAIndex := slices.IndexFunc(sims, func(sim simSummaryResponse) bool { return sim.SimID == "sim-a" })
		simBIndex := slices.IndexFunc(sims, func(sim simSummaryResponse) bool { return sim.SimID == "sim-b" })
		if simAIndex == -1 || simBIndex == -1 {
			return false
		}
		simA := sims[simAIndex]
		simB := sims[simBIndex]
		return simA.DropCount == 1 && simA.TurnMoveCount == 1 && simA.LooseFoodCount == 1 &&
			simB.DropCount == 1 && simB.PickupCount == 1 && simB.LooseFoodCount == 0
	})

	if len(sims) < 2 {
		t.Fatalf("expected at least 2 sim summaries, got %d", len(sims))
	}

	slices.SortFunc(sims, func(a, b simSummaryResponse) int {
		return strings.Compare(a.SimID, b.SimID)
	})

	simAIndex := slices.IndexFunc(sims, func(sim simSummaryResponse) bool {
		return sim.SimID == "sim-a"
	})
	if simAIndex == -1 {
		t.Fatalf("expected sim-a summary to be present, got %+v", sims)
	}
	simA := sims[simAIndex]
	if simA.DropCount != 1 || simA.TurnMoveCount != 1 || simA.LooseFoodCount != 1 {
		t.Fatalf("unexpected sim-a summary: %+v", simA)
	}

	simBIndex := slices.IndexFunc(sims, func(sim simSummaryResponse) bool {
		return sim.SimID == "sim-b"
	})
	if simBIndex == -1 {
		t.Fatalf("expected sim-b summary to be present, got %+v", sims)
	}
	simB := sims[simBIndex]
	if simB.DropCount != 1 || simB.PickupCount != 1 || simB.LooseFoodCount != 0 {
		t.Fatalf("unexpected sim-b summary: %+v", simB)
	}
}

func sendClientEvents(t *testing.T, ctx context.Context, baseURL string, events []loadsim.Event) {
	t.Helper()

	if err := loadsim.SendEvents(ctx, baseURL, events); err != nil {
		failedType := "<unknown>"
		if len(events) > 0 {
			failedType = events[0].Type
		}
		t.Fatalf("expected %q event batch to be accepted over websocket: %v", failedType, err)
	}
}
