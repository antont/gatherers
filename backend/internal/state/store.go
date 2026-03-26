package state

import (
	"math"
	"sync"
	"time"
)

type FoodDrop struct {
	FoodID string
	X      float64
	Y      float64
}

type FoodPickup struct {
	FoodID string
}

type Summary struct {
	ConnectedSimCount           int     `json:"connected_sim_count"`
	LooseFoodCount              int     `json:"loose_food_count"`
	OccupiedCellCount           int     `json:"occupied_cell_count"`
	NearestNeighborMeanDistance float64 `json:"nearest_neighbor_mean_distance"`
	ElapsedSeconds              float64 `json:"elapsed_seconds"`
	EventsPerSecond             float64 `json:"events_per_second"`
}

type foodPosition struct {
	X float64
	Y float64
}

type simState struct {
	looseFood     map[string]foodPosition
	antCount      int
	connectedAt   time.Time
	totalEvents   int
	pickupCount   int
	dropCount     int
	turnMoveCount int
}

type Store struct {
	mu       sync.RWMutex
	cellSize float64
	sims     map[string]*simState
	now      func() time.Time
}

type DashboardSnapshotData struct {
	cellSize    float64
	sims        []SimSummary
	looseFood   []foodPosition
	startedAt   time.Time
	totalEvents int
}

type SimSummary struct {
	SimID          string `json:"sim_id"`
	AntCount       int    `json:"ant_count"`
	PickupCount    int    `json:"pickup_count"`
	DropCount      int    `json:"drop_count"`
	TurnMoveCount  int    `json:"turn_move_count"`
	LooseFoodCount int    `json:"loose_food_count"`
}

func NewStore(cellSize float64) *Store {
	return &Store{
		cellSize: cellSize,
		sims:     make(map[string]*simState),
		now:      time.Now,
	}
}

func (s *Store) RecordDrop(simID string, drop FoodDrop) {
	s.mu.Lock()
	defer s.mu.Unlock()

	state := s.ensureSim(simID)
	state.recordEvent(s.now())
	state.looseFood[drop.FoodID] = foodPosition{X: drop.X, Y: drop.Y}
	state.dropCount++
}

func (s *Store) RecordHello(simID string, antCount int) {
	s.mu.Lock()
	defer s.mu.Unlock()

	state := s.ensureSim(simID)
	state.antCount = antCount
	state.recordEvent(s.now())
}

func (s *Store) RecordFoodSnapshot(simID string, foods []FoodDrop) {
	s.mu.Lock()
	defer s.mu.Unlock()

	state := s.ensureSim(simID)
	state.recordEvent(s.now())
	state.looseFood = make(map[string]foodPosition, len(foods))
	for _, food := range foods {
		state.looseFood[food.FoodID] = foodPosition{X: food.X, Y: food.Y}
	}
}

func (s *Store) RecordPickup(simID string, pickup FoodPickup) {
	s.mu.Lock()
	defer s.mu.Unlock()

	state := s.ensureSim(simID)
	state.recordEvent(s.now())
	delete(state.looseFood, pickup.FoodID)
	state.pickupCount++
}

func (s *Store) RecordTurnMove(simID string) {
	s.mu.Lock()
	defer s.mu.Unlock()

	state := s.ensureSim(simID)
	state.recordEvent(s.now())
	state.turnMoveCount++
}

func (s *Store) GlobalSummary() Summary {
	return s.DashboardSnapshotData().Summary(s.now())
}

func (s *Store) ensureSim(simID string) *simState {
	state, ok := s.sims[simID]
	if ok {
		return state
	}

	state = &simState{
		looseFood: make(map[string]foodPosition),
	}
	s.sims[simID] = state
	return state
}

func (s *Store) SimSummaries() []SimSummary {
	return s.DashboardSnapshotData().SimSummaries()
}

func (s *Store) DashboardSnapshotData() DashboardSnapshotData {
	s.mu.RLock()
	defer s.mu.RUnlock()

	summaries := make([]SimSummary, 0, len(s.sims))
	positions := make([]foodPosition, 0)
	var startedAt time.Time
	totalEvents := 0
	for simID, sim := range s.sims {
		summaries = append(summaries, SimSummary{
			SimID:          simID,
			AntCount:       sim.antCount,
			PickupCount:    sim.pickupCount,
			DropCount:      sim.dropCount,
			TurnMoveCount:  sim.turnMoveCount,
			LooseFoodCount: len(sim.looseFood),
		})
		totalEvents += sim.totalEvents
		if !sim.connectedAt.IsZero() && (startedAt.IsZero() || sim.connectedAt.Before(startedAt)) {
			startedAt = sim.connectedAt
		}
		for _, pos := range sim.looseFood {
			positions = append(positions, pos)
		}
	}

	return DashboardSnapshotData{
		cellSize:    s.cellSize,
		sims:        summaries,
		looseFood:   positions,
		startedAt:   startedAt,
		totalEvents: totalEvents,
	}
}

func (d DashboardSnapshotData) SimSummaries() []SimSummary {
	summaries := make([]SimSummary, len(d.sims))
	copy(summaries, d.sims)
	return summaries
}

func (d DashboardSnapshotData) Summary(now time.Time) Summary {
	elapsedSeconds, eventsPerSecond := d.metrics(now)
	if len(d.looseFood) == 0 {
		return Summary{
			ConnectedSimCount: len(d.sims),
			ElapsedSeconds:    elapsedSeconds,
			EventsPerSecond:   eventsPerSecond,
		}
	}

	return Summary{
		ConnectedSimCount:           len(d.sims),
		LooseFoodCount:              len(d.looseFood),
		OccupiedCellCount:           occupiedCellCount(d.looseFood, d.cellSize),
		NearestNeighborMeanDistance: meanNearestNeighborDistance(d.looseFood),
		ElapsedSeconds:              elapsedSeconds,
		EventsPerSecond:             eventsPerSecond,
	}
}

func (s *simState) recordEvent(now time.Time) {
	if s.connectedAt.IsZero() {
		s.connectedAt = now
	}
	s.totalEvents++
}

func (d DashboardSnapshotData) metrics(now time.Time) (float64, float64) {
	if d.startedAt.IsZero() {
		return 0, 0
	}

	elapsedSeconds := now.Sub(d.startedAt).Seconds()
	if elapsedSeconds <= 0 {
		return 0, 0
	}

	return elapsedSeconds, float64(d.totalEvents) / elapsedSeconds
}

func occupiedCellCount(positions []foodPosition, cellSize float64) int {
	if cellSize <= 0 {
		cellSize = 1
	}

	cells := make(map[[2]int]struct{})
	for _, pos := range positions {
		cell := [2]int{
			int(math.Floor(pos.X / cellSize)),
			int(math.Floor(pos.Y / cellSize)),
		}
		cells[cell] = struct{}{}
	}
	return len(cells)
}

func meanNearestNeighborDistance(positions []foodPosition) float64 {
	if len(positions) < 2 {
		return 0
	}

	total := 0.0
	for i, pos := range positions {
		best := math.MaxFloat64
		for j, other := range positions {
			if i == j {
				continue
			}
			dx := pos.X - other.X
			dy := pos.Y - other.Y
			dist := math.Hypot(dx, dy)
			if dist < best {
				best = dist
			}
		}
		total += best
	}

	return total / float64(len(positions))
}
