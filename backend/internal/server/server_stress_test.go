package server

import (
	"context"
	"encoding/json"
	"fmt"
	"net/http"
	"net/http/httptest"
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
	testServer := httptest.NewServer(srv.Handler())
	defer testServer.Close()

	ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
	defer cancel()

	start := make(chan struct{})
	errCh := make(chan error, clientCount)
	var wg sync.WaitGroup

	for i := 0; i < clientCount; i++ {
		wg.Add(1)
		go func(i int) {
			defer wg.Done()
			<-start

			simID := fmt.Sprintf("sim-%03d", i)
			err := sendClientEventsErr(ctx, testServer.URL, []map[string]any{
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

	resp, err := http.Get(testServer.URL + "/api/summary")
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

	if summary.ConnectedSimCount != clientCount {
		t.Fatalf("expected %d connected sims after stress run, got %d", clientCount, summary.ConnectedSimCount)
	}

	if summary.LooseFoodCount != clientCount {
		t.Fatalf("expected %d loose food items after stress run, got %d", clientCount, summary.LooseFoodCount)
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
