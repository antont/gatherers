package loadsim

import (
	"context"
	"net/http"
	"net/http/httptest"
	"testing"
)

func TestFetchBreakpointTotalsUsesCompactTotalsEndpoint(t *testing.T) {
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.URL.Path != "/api/breakpoint_totals" {
			t.Fatalf("expected compact totals path, got %q", r.URL.Path)
		}
		if got := r.URL.Query().Get("prefix"); got != "bp-run" {
			t.Fatalf("expected prefix query to be propagated, got %q", got)
		}
		w.Header().Set("Content-Type", "application/json")
		_, _ = w.Write([]byte(`{
			"connected_sims": 3,
			"pickup_count": 9,
			"drop_count": 9,
			"turn_move_count": 9,
			"loose_food_count": 240
		}`))
	}))
	defer server.Close()

	totals, err := fetchBreakpointTotals(context.Background(), server.URL, "bp-run", 80)
	if err != nil {
		t.Fatalf("expected compact totals request to succeed, got %v", err)
	}
	if totals.ConnectedSims != 3 {
		t.Fatalf("expected 3 connected sims, got %+v", totals)
	}
	if totals.PickupCount != 9 || totals.DropCount != 9 || totals.TurnMoveCount != 9 {
		t.Fatalf("expected compact counters to decode, got %+v", totals)
	}
	if totals.LooseFoodCount != 240 {
		t.Fatalf("expected loose food count to decode, got %+v", totals)
	}
	if totals.SentEvents != 33 {
		t.Fatalf("expected sent events to be derived from compact totals, got %+v", totals)
	}
}
