package server

import (
	"context"
	"encoding/json"
	"fmt"
	"net/http"
	"strings"
	"sync"
	"testing"
	"time"

	"github.com/antont/gatherers/backend/internal/config"
	"github.com/coder/websocket"
	"github.com/coder/websocket/wsjson"
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
			err := sendClientEventsErr(ctx, target.baseURL, []map[string]any{
				{
					"type":         "sim_hello",
					"sim_id":       simID,
					"seq":          1,
					"timestamp_ms": 1000 + i,
					"payload": map[string]any{
						"sim_name": simID,
					},
				},
				{
					"type":         "food_drop",
					"sim_id":       simID,
					"seq":          2,
					"timestamp_ms": 2000 + i,
					"payload": map[string]any{
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

func sendClientEventsErr(ctx context.Context, baseURL string, events []map[string]any) error {
	wsURL := strings.Replace(baseURL, "http://", "ws://", 1) + "/ws/ingest"
	conn, _, err := websocket.Dial(ctx, wsURL, nil)
	if err != nil {
		return err
	}
	defer conn.Close(websocket.StatusNormalClosure, "")

	for _, event := range events {
		if err := wsjson.Write(ctx, conn, event); err != nil {
			return err
		}
	}

	return nil
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
