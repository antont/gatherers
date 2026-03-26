package state

import "math"

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
}

type foodPosition struct {
	X float64
	Y float64
}

type simState struct {
	looseFood      map[string]foodPosition
	pickupCount    int
	dropCount      int
	turnMoveCount  int
}

type Store struct {
	cellSize float64
	sims     map[string]*simState
}

type SimSummary struct {
	SimID         string `json:"sim_id"`
	PickupCount   int    `json:"pickup_count"`
	DropCount     int    `json:"drop_count"`
	TurnMoveCount int    `json:"turn_move_count"`
	LooseFoodCount int   `json:"loose_food_count"`
}

func NewStore(cellSize float64) *Store {
	return &Store{
		cellSize: cellSize,
		sims:     make(map[string]*simState),
	}
}

func (s *Store) RecordDrop(simID string, drop FoodDrop) {
	state := s.ensureSim(simID)
	state.looseFood[drop.FoodID] = foodPosition{X: drop.X, Y: drop.Y}
	state.dropCount++
}

func (s *Store) RecordHello(simID string) {
	s.ensureSim(simID)
}

func (s *Store) RecordPickup(simID string, pickup FoodPickup) {
	state := s.ensureSim(simID)
	delete(state.looseFood, pickup.FoodID)
	state.pickupCount++
}

func (s *Store) RecordTurnMove(simID string) {
	state := s.ensureSim(simID)
	state.turnMoveCount++
}

func (s *Store) GlobalSummary() Summary {
	positions := s.allLooseFood()
	if len(positions) == 0 {
		return Summary{}
	}

	return Summary{
		ConnectedSimCount:           len(s.sims),
		LooseFoodCount:              len(positions),
		OccupiedCellCount:           occupiedCellCount(positions, s.cellSize),
		NearestNeighborMeanDistance: meanNearestNeighborDistance(positions),
	}
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
	summaries := make([]SimSummary, 0, len(s.sims))
	for simID, sim := range s.sims {
		summaries = append(summaries, SimSummary{
			SimID:          simID,
			PickupCount:    sim.pickupCount,
			DropCount:      sim.dropCount,
			TurnMoveCount:  sim.turnMoveCount,
			LooseFoodCount: len(sim.looseFood),
		})
	}
	return summaries
}

func (s *Store) allLooseFood() []foodPosition {
	positions := make([]foodPosition, 0)
	for _, sim := range s.sims {
		for _, pos := range sim.looseFood {
			positions = append(positions, pos)
		}
	}
	return positions
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
