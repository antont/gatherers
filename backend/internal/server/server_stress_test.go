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

	finalSummary := fetchSummary(t, target.baseURL)

	connectedDelta := finalSummary.ConnectedSimCount - initialSummary.ConnectedSimCount
	looseFoodDelta := finalSummary.LooseFoodCount - initialSummary.LooseFoodCount

	if connectedDelta < clientCount {
		t.Fatalf("expected connected sim count to increase by at least %d, got initial=%d final=%d", clientCount, initialSummary.ConnectedSimCount, finalSummary.ConnectedSimCount)
	}

	if looseFoodDelta < clientCount {
		t.Fatalf("expected loose food count to increase by at least %d, got initial=%d final=%d", clientCount, initialSummary.LooseFoodCount, finalSummary.LooseFoodCount)
	}
}

func sendClientEventsErr(ctx context.Context, baseURL string, events []loadsim.Event) error {
	return loadsim.SendEvents(ctx, baseURL, events)
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
