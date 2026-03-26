package server

import (
	"context"
	"encoding/json"
	"net/http"
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

	resp, err := http.Get(target.baseURL + "/api/sims")
	if err != nil {
		t.Fatalf("expected sims endpoint to respond: %v", err)
	}
	defer resp.Body.Close()

	var sims []struct {
		SimID         string `json:"sim_id"`
		PickupCount   int    `json:"pickup_count"`
		DropCount     int    `json:"drop_count"`
		TurnMoveCount int    `json:"turn_move_count"`
	}
	if err := json.NewDecoder(resp.Body).Decode(&sims); err != nil {
		t.Fatalf("expected sims JSON to decode: %v", err)
	}

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
