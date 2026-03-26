package server

import (
	"context"
	"testing"
	"time"

	"github.com/antont/gatherers/backend/internal/config"
	"github.com/antont/gatherers/backend/internal/loadsim"
)

func TestLoadRunnerGeneratesMixedPerSimActivity(t *testing.T) {
	srv := New(config.Config{})
	target := newTestTarget(t, srv.Handler())
	defer target.closeFn()

	ctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)
	defer cancel()

	err := loadsim.Run(ctx, loadsim.RunOptions{
		BaseURL:     target.baseURL,
		ClientCount: 3,
		Duration:    150 * time.Millisecond,
		Interval:    10 * time.Millisecond,
		Seed:        42,
		SimIDPrefix: "manual",
	})
	if err != nil {
		t.Fatalf("expected load runner to complete successfully: %v", err)
	}

	sims := waitForSimSummaries(t, target.baseURL, 1500*time.Millisecond, func(sims []simSummaryResponse) bool {
		if len(sims) != 3 {
			return false
		}
		for _, sim := range sims {
			if sim.DropCount == 0 || sim.PickupCount == 0 || sim.TurnMoveCount == 0 {
				return false
			}
		}
		return true
	})

	if len(sims) != 3 {
		t.Fatalf("expected 3 sim summaries, got %d", len(sims))
	}

	for _, sim := range sims {
		if sim.DropCount == 0 {
			t.Fatalf("expected %s to report at least one drop, got %+v", sim.SimID, sim)
		}
		if sim.PickupCount == 0 {
			t.Fatalf("expected %s to report at least one pickup, got %+v", sim.SimID, sim)
		}
		if sim.TurnMoveCount == 0 {
			t.Fatalf("expected %s to report at least one move, got %+v", sim.SimID, sim)
		}
	}
}
