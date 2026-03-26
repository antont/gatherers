package state

import (
	"reflect"
	"testing"
	"time"
)

func TestStoreTracksLooseFoodAcrossDropAndPickup(t *testing.T) {
	store := NewStore(50)

	store.RecordDrop("sim-a", FoodDrop{
		FoodID: "food-1",
		X:      100,
		Y:      100,
	})
	store.RecordDrop("sim-a", FoodDrop{
		FoodID: "food-2",
		X:      110,
		Y:      100,
	})

	summary := store.GlobalSummary()
	if summary.LooseFoodCount != 2 {
		t.Fatalf("expected 2 loose food items after drops, got %d", summary.LooseFoodCount)
	}

	store.RecordPickup("sim-a", FoodPickup{
		FoodID: "food-1",
	})

	summary = store.GlobalSummary()
	if summary.LooseFoodCount != 1 {
		t.Fatalf("expected 1 loose food item after pickup, got %d", summary.LooseFoodCount)
	}
}

func TestGlobalSummaryDistinguishesClusteredFromSparseFood(t *testing.T) {
	clustered := NewStore(50)
	sparse := NewStore(50)

	clusteredDrops := []FoodDrop{
		{FoodID: "c1", X: 10, Y: 10},
		{FoodID: "c2", X: 12, Y: 12},
		{FoodID: "c3", X: 14, Y: 14},
		{FoodID: "c4", X: 16, Y: 16},
		{FoodID: "c5", X: 18, Y: 18},
		{FoodID: "c6", X: 20, Y: 20},
	}
	for _, drop := range clusteredDrops {
		clustered.RecordDrop("sim-a", drop)
	}

	sparseDrops := []FoodDrop{
		{FoodID: "s1", X: 0, Y: 0},
		{FoodID: "s2", X: 100, Y: 0},
		{FoodID: "s3", X: 200, Y: 0},
		{FoodID: "s4", X: 300, Y: 0},
		{FoodID: "s5", X: 400, Y: 0},
		{FoodID: "s6", X: 500, Y: 0},
	}
	for _, drop := range sparseDrops {
		sparse.RecordDrop("sim-a", drop)
	}

	clusteredSummary := clustered.GlobalSummary()
	sparseSummary := sparse.GlobalSummary()

	if clusteredSummary.OccupiedCellCount >= sparseSummary.OccupiedCellCount {
		t.Fatalf(
			"expected clustered food to occupy fewer cells than sparse food, got clustered=%d sparse=%d",
			clusteredSummary.OccupiedCellCount,
			sparseSummary.OccupiedCellCount,
		)
	}

	if clusteredSummary.NearestNeighborMeanDistance >= sparseSummary.NearestNeighborMeanDistance {
		t.Fatalf(
			"expected clustered food to have a smaller nearest-neighbor mean distance than sparse food, got clustered=%f sparse=%f",
			clusteredSummary.NearestNeighborMeanDistance,
			sparseSummary.NearestNeighborMeanDistance,
		)
	}
}

func TestGlobalSummaryReportsElapsedAndEventRate(t *testing.T) {
	store := NewStore(50)
	currentTime := time.Unix(1_000, 0)
	store.now = func() time.Time { return currentTime }

	store.RecordHello("sim-a", 26)

	currentTime = currentTime.Add(2 * time.Second)
	store.RecordDrop("sim-a", FoodDrop{
		FoodID: "food-1",
		X:      10,
		Y:      10,
	})

	summary := store.GlobalSummary()
	if summary.ElapsedSeconds != 2 {
		t.Fatalf("expected elapsed seconds to be 2, got %f", summary.ElapsedSeconds)
	}
	if summary.EventsPerSecond != 1 {
		t.Fatalf("expected events per second to be 1, got %f", summary.EventsPerSecond)
	}
}

func TestDashboardSnapshotDataMatchesCurrentStoreViews(t *testing.T) {
	store := NewStore(50)
	currentTime := time.Unix(2_000, 0)
	store.now = func() time.Time { return currentTime }

	store.RecordHello("sim-a", 26)
	store.RecordFoodSnapshot("sim-a", []FoodDrop{
		{FoodID: "food-1", X: 10, Y: 10},
		{FoodID: "food-2", X: 20, Y: 20},
	})
	store.RecordPickup("sim-a", FoodPickup{FoodID: "food-1"})
	store.RecordTurnMove("sim-a")

	currentTime = currentTime.Add(3 * time.Second)
	store.RecordHello("sim-b", 13)
	store.RecordDrop("sim-b", FoodDrop{FoodID: "food-3", X: 100, Y: 100})

	snapshot := store.DashboardSnapshotData()
	gotSummary := snapshot.Summary(currentTime)
	gotSims := snapshot.SimSummaries()

	if wantSummary := store.GlobalSummary(); !reflect.DeepEqual(gotSummary, wantSummary) {
		t.Fatalf("expected snapshot summary %+v to match current store summary %+v", gotSummary, wantSummary)
	}

	if wantSims := store.SimSummaries(); !reflect.DeepEqual(gotSims, wantSims) {
		t.Fatalf("expected snapshot sims %+v to match current store sims %+v", gotSims, wantSims)
	}
}
