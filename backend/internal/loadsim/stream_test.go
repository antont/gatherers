package loadsim

import (
	"context"
	"encoding/json"
	"fmt"
	"strings"
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

	wantTypes := []string{"sim_hello", "sim_food_snapshot", "food_pickup", "ant_turn_move", "food_drop"}
	for i, want := range wantTypes {
		if events[i].Type != want {
			t.Fatalf("expected event %d type %q, got %q", i, want, events[i].Type)
		}
	}

	if !strings.Contains(mustJSON(t, events[1]), "\"foods\"") {
		t.Fatalf("expected startup snapshot payload to include foods, got %+v", events[1])
	}
	if !strings.Contains(mustJSON(t, events[0]), "\"ant_count\":26") {
		t.Fatalf("expected startup hello payload to include ant_count, got %+v", events[0])
	}
}

func TestClientEventStreamRotatesAcrossMultipleAnts(t *testing.T) {
	stream := NewClientEventStream(ClientOptions{
		SimID:            "sim-ants",
		Seed:             9,
		StartX:           0,
		StartY:           0,
		StartFood:        "food-ants",
		StartAnt:         "ant-ants",
		InitialFoodCount: 12,
		InitialAntCount:  4,
		Interval:         5 * time.Millisecond,
	})

	ctx, cancel := context.WithTimeout(context.Background(), 50*time.Millisecond)
	defer cancel()

	events := stream.NextEvents(ctx, 14)
	ants := map[string]struct{}{}
	for _, event := range events {
		json := mustJSON(t, event)
		for i := 0; i < 4; i++ {
			id := fmt.Sprintf("ant-ants-%03d", i)
			if strings.Contains(json, id) {
				ants[id] = struct{}{}
			}
		}
	}

	if len(ants) < 3 {
		t.Fatalf("expected fake stream to rotate across multiple ant ids, got %v from %+v", ants, events)
	}
}

func TestStartupFoodSnapshotUsesClusteredPositions(t *testing.T) {
	stream := NewClientEventStream(ClientOptions{
		SimID:            "sim-foods",
		Seed:             3,
		StartX:           0,
		StartY:           0,
		StartFood:        "food-cluster",
		InitialFoodCount: 20,
		InitialAntCount:  2,
		Interval:         5 * time.Millisecond,
	})

	snapshot := stream.NextEvents(context.Background(), 2)[1]
	payload, ok := snapshot.Payload.(map[string]any)
	if !ok {
		t.Fatalf("expected snapshot payload map, got %#v", snapshot.Payload)
	}
	foods, ok := payload["foods"].([]map[string]any)
	if !ok {
		t.Fatalf("expected typed foods payload, got %#v", payload["foods"])
	}

	cells := map[string]int{}
	for _, food := range foods {
		x := int(food["x"].(float64) / 50)
		y := int(food["y"].(float64) / 50)
		cells[fmt.Sprintf("%d:%d", x, y)]++
	}

	maxCellCount := 0
	for _, count := range cells {
		if count > maxCellCount {
			maxCellCount = count
		}
	}

	if maxCellCount < 3 {
		t.Fatalf("expected clustered startup foods with repeated cells, got cells=%v foods=%v", cells, foods)
	}
}

func mustJSON(t *testing.T, value any) string {
	t.Helper()

	data, err := json.Marshal(value)
	if err != nil {
		t.Fatalf("expected value to marshal: %v", err)
	}
	return string(data)
}
