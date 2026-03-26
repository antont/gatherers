package loadsim

import (
	"context"
	"testing"
	"time"
)

func TestClientEventStreamStartsWithHelloAndDeterministicEvents(t *testing.T) {
	stream := NewClientEventStream(ClientOptions{
		SimID:      "sim-007",
		Seed:       7,
		StartX:     10,
		StartY:     20,
		StartFood:  "food-007",
		StartAnt:   "ant-007",
		Interval:   5 * time.Millisecond,
	})

	ctx, cancel := context.WithTimeout(context.Background(), 50*time.Millisecond)
	defer cancel()

	events := stream.NextEvents(ctx, 5)
	if len(events) != 5 {
		t.Fatalf("expected 5 events, got %d", len(events))
	}

	if events[0].Type != "sim_hello" {
		t.Fatalf("expected first event to be sim_hello, got %q", events[0].Type)
	}

	for i, event := range events {
		if event.SimID != "sim-007" {
			t.Fatalf("expected event %d sim_id to be sim-007, got %q", i, event.SimID)
		}
		if event.Seq != uint64(i+1) {
			t.Fatalf("expected event %d seq to be %d, got %d", i, i+1, event.Seq)
		}
	}

	wantTypes := []string{"sim_hello", "food_drop", "ant_turn_move", "food_pickup", "food_drop"}
	for i, want := range wantTypes {
		if events[i].Type != want {
			t.Fatalf("expected event %d type %q, got %q", i, want, events[i].Type)
		}
	}
}
