package server

import (
	"context"
	"encoding/json"
	"fmt"
	"net/http"
	"sync"
	"testing"
	"time"

	"github.com/antont/gatherers/backend/internal/config"
	"github.com/antont/gatherers/backend/internal/loadsim"
)

func TestStressHundredFakeClients(t *testing.T) {
	const clientCount = 100

	srv := New(config.Config{})
	target := newTestTarget(t, srv.Handler())
	defer target.closeFn()

	ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
	defer cancel()

	initialSummary := fetchSummary(t, target.baseURL)

	start := make(chan struct{})
	errCh := make(chan error, clientCount)
	var wg sync.WaitGroup

	for i := 0; i < clientCount; i++ {
		wg.Add(1)
		go func(i int) {
			defer wg.Done()
			<-start

			simID := fmt.Sprintf("sim-%03d", i)
			err := sendClientEventsErr(ctx, target.baseURL, []loadsim.Event{
				{
					Type:        "sim_hello",
					SimID:       simID,
					Seq:         1,
					TimestampMS: int64(1000 + i),
					Payload: map[string]any{
						"sim_name": simID,
					},
				},
				{
					Type:        "food_drop",
					SimID:       simID,
					Seq:         2,
					TimestampMS: int64(2000 + i),
					Payload: map[string]any{
						"food_id": fmt.Sprintf("food-%03d", i),
						"x":       float64(i),
						"y":       float64(i),
					},
				},
			})
			errCh <- err
		}(i)
	}

	close(start)
	wg.Wait()
	close(errCh)

	for err := range errCh {
		if err != nil {
			t.Fatalf("expected all fake clients to complete successfully: %v", err)
		}
	}

	finalSummary := waitForSummary(t, target.baseURL, 1500*time.Millisecond, func(summary struct {
		ConnectedSimCount int `json:"connected_sim_count"`
		LooseFoodCount    int `json:"loose_food_count"`
	}) bool {
		connectedDelta := summary.ConnectedSimCount - initialSummary.ConnectedSimCount
		looseFoodDelta := summary.LooseFoodCount - initialSummary.LooseFoodCount
		return connectedDelta >= clientCount && looseFoodDelta >= clientCount
	})

	connectedDelta := finalSummary.ConnectedSimCount - initialSummary.ConnectedSimCount
	looseFoodDelta := finalSummary.LooseFoodCount - initialSummary.LooseFoodCount

	if connectedDelta < clientCount {
		t.Fatalf("expected connected sim count to increase by at least %d, got initial=%d final=%d", clientCount, initialSummary.ConnectedSimCount, finalSummary.ConnectedSimCount)
	}

	if looseFoodDelta < clientCount {
		t.Fatalf("expected loose food count to increase by at least %d, got initial=%d final=%d", clientCount, initialSummary.LooseFoodCount, finalSummary.LooseFoodCount)
	}
}

func TestStressHundredFakeClientsDeliversExactEventTotals(t *testing.T) {
	const (
		clientCount       = 100
		initialFoodCount  = 80
		activityTriplets  = 20
		totalEventsPerSim = 2 + activityTriplets*3
	)

	srv := New(config.Config{})
	target := newTestTarget(t, srv.Handler())
	defer target.closeFn()

	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	start := make(chan struct{})
	errCh := make(chan error, clientCount)
	var wg sync.WaitGroup

	for i := 0; i < clientCount; i++ {
		wg.Add(1)
		go func(i int) {
			defer wg.Done()
			<-start

			simID := fmt.Sprintf("exact-%03d", i)
			stream := loadsim.NewClientEventStream(loadsim.ClientOptions{
				SimID:            simID,
				SimName:          simID,
				Seed:             int64(1_000 + i),
				StartX:           float64(i),
				StartY:           float64(i),
				StartFood:        fmt.Sprintf("%s-food", simID),
				StartAnt:         fmt.Sprintf("%s-ant", simID),
				InitialAntCount:  26,
				InitialFoodCount: initialFoodCount,
				Interval:         5 * time.Millisecond,
			})
			events := stream.NextEvents(context.Background(), totalEventsPerSim)
			if len(events) != totalEventsPerSim {
				errCh <- fmt.Errorf("%s generated %d events, want %d", simID, len(events), totalEventsPerSim)
				return
			}

			if err := sendClientEventsErr(ctx, target.baseURL, events); err != nil {
				errCh <- fmt.Errorf("%s failed after attempting %d events: %w", simID, len(events), err)
				return
			}

			errCh <- nil
		}(i)
	}

	close(start)
	wg.Wait()
	close(errCh)

	var sendErrors []error
	for err := range errCh {
		if err != nil {
			sendErrors = append(sendErrors, err)
		}
	}
	if len(sendErrors) > 0 {
		t.Fatalf("expected all %d clients to deliver %d events each without error, got %d errors; first error: %v", clientCount, totalEventsPerSim, len(sendErrors), sendErrors[0])
	}

	sims := waitForExactSimTotals(t, target.baseURL, 10*time.Second, exactSimTotals{
		SimCount:          clientCount,
		PickupCount:       clientCount * activityTriplets,
		DropCount:         clientCount * activityTriplets,
		TurnMoveCount:     clientCount * activityTriplets,
		LooseFoodCount:    clientCount * initialFoodCount,
		PerSimPickupCount: activityTriplets,
		PerSimDropCount:   activityTriplets,
		PerSimMoveCount:   activityTriplets,
		PerSimLooseFood:   initialFoodCount,
	})

	if len(sims) != clientCount {
		t.Fatalf("expected %d sim summaries, got %d", clientCount, len(sims))
	}
}

func sendClientEventsErr(ctx context.Context, baseURL string, events []loadsim.Event) error {
	return loadsim.SendEvents(ctx, baseURL, events)
}

type simSummaryResponse struct {
	SimID          string `json:"sim_id"`
	AntCount       int    `json:"ant_count"`
	PickupCount    int    `json:"pickup_count"`
	DropCount      int    `json:"drop_count"`
	TurnMoveCount  int    `json:"turn_move_count"`
	LooseFoodCount int    `json:"loose_food_count"`
}

type exactSimTotals struct {
	SimCount          int
	PickupCount       int
	DropCount         int
	TurnMoveCount     int
	LooseFoodCount    int
	PerSimPickupCount int
	PerSimDropCount   int
	PerSimMoveCount   int
	PerSimLooseFood   int
}

func waitForExactSimTotals(t *testing.T, baseURL string, timeout time.Duration, want exactSimTotals) []simSummaryResponse {
	t.Helper()

	deadline := time.Now().Add(timeout)
	var last []simSummaryResponse
	for time.Now().Before(deadline) {
		last = fetchSimSummaries(t, baseURL)
		if matchesExactTotals(last, want) {
			return last
		}
		time.Sleep(100 * time.Millisecond)
	}

	totalPickups, totalDrops, totalMoves, totalLooseFood := aggregateSimTotals(last)
	first := simSummaryResponse{}
	if len(last) > 0 {
		first = last[0]
	}
	t.Fatalf(
		"lost events under stress: expected sims=%d pickups=%d drops=%d moves=%d loose_food=%d and per-sim pickup/drop/move/loose=%d/%d/%d/%d; got sims=%d pickups=%d drops=%d moves=%d loose_food=%d first_sim=%+v",
		want.SimCount,
		want.PickupCount,
		want.DropCount,
		want.TurnMoveCount,
		want.LooseFoodCount,
		want.PerSimPickupCount,
		want.PerSimDropCount,
		want.PerSimMoveCount,
		want.PerSimLooseFood,
		len(last),
		totalPickups,
		totalDrops,
		totalMoves,
		totalLooseFood,
		first,
	)
	return nil
}

func fetchSimSummaries(t *testing.T, baseURL string) []simSummaryResponse {
	t.Helper()

	resp, err := http.Get(baseURL + "/api/sims")
	if err != nil {
		t.Fatalf("expected sims endpoint to respond: %v", err)
	}
	defer resp.Body.Close()

	var sims []simSummaryResponse
	if err := json.NewDecoder(resp.Body).Decode(&sims); err != nil {
		t.Fatalf("expected sims JSON to decode: %v", err)
	}
	return sims
}

func matchesExactTotals(sims []simSummaryResponse, want exactSimTotals) bool {
	if len(sims) != want.SimCount {
		return false
	}
	totalPickups, totalDrops, totalMoves, totalLooseFood := aggregateSimTotals(sims)
	if totalPickups != want.PickupCount || totalDrops != want.DropCount || totalMoves != want.TurnMoveCount || totalLooseFood != want.LooseFoodCount {
		return false
	}

	for _, sim := range sims {
		if sim.PickupCount != want.PerSimPickupCount ||
			sim.DropCount != want.PerSimDropCount ||
			sim.TurnMoveCount != want.PerSimMoveCount ||
			sim.LooseFoodCount != want.PerSimLooseFood {
			return false
		}
	}

	return true
}

func aggregateSimTotals(sims []simSummaryResponse) (pickups int, drops int, moves int, looseFood int) {
	for _, sim := range sims {
		pickups += sim.PickupCount
		drops += sim.DropCount
		moves += sim.TurnMoveCount
		looseFood += sim.LooseFoodCount
	}
	return pickups, drops, moves, looseFood
}

func fetchSummary(t *testing.T, baseURL string) struct {
	ConnectedSimCount int `json:"connected_sim_count"`
	LooseFoodCount    int `json:"loose_food_count"`
} {
	t.Helper()

	resp, err := http.Get(baseURL + "/api/summary")
	if err != nil {
		t.Fatalf("expected summary endpoint to respond: %v", err)
	}
	defer resp.Body.Close()

	var summary struct {
		ConnectedSimCount int `json:"connected_sim_count"`
		LooseFoodCount    int `json:"loose_food_count"`
	}
	if err := json.NewDecoder(resp.Body).Decode(&summary); err != nil {
		t.Fatalf("expected summary JSON to decode: %v", err)
	}

	return summary
}
