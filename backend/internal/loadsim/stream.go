package loadsim

import (
	"context"
	"fmt"
	"strings"
	"sync"
	"time"

	"github.com/coder/websocket"
	"github.com/coder/websocket/wsjson"
)

type ClientOptions struct {
	SimID     string
	SimName   string
	Seed      int64
	StartX    float64
	StartY    float64
	StartFood string
	StartAnt  string
	InitialAntCount int
	InitialFoodCount int
	Interval  time.Duration
}

type Event struct {
	Type        string `json:"type"`
	SimID       string `json:"sim_id"`
	Seq         uint64 `json:"seq"`
	TimestampMS int64  `json:"timestamp_ms"`
	Payload     any    `json:"payload,omitempty"`
}

type RunOptions struct {
	BaseURL     string
	ClientCount int
	Duration    time.Duration
	Interval    time.Duration
	Seed        int64
	InitialAntCount int
	InitialFoodCount int
	SimIDPrefix string
}

type ClientEventStream struct {
	opts        ClientOptions
	seq         uint64
	timestampMS int64
	x           float64
	y           float64
	step        int
	foodIDs     []string
	foodIndex   int
	antIDs      []string
	antIndex    int
}

func NewClientEventStream(opts ClientOptions) *ClientEventStream {
	if opts.SimName == "" {
		opts.SimName = opts.SimID
	}
	if opts.StartFood == "" {
		opts.StartFood = opts.SimID + "-food"
	}
	if opts.StartAnt == "" {
		opts.StartAnt = opts.SimID + "-ant"
	}
	if opts.InitialAntCount <= 0 {
		opts.InitialAntCount = 26
	}
	if opts.InitialFoodCount <= 0 {
		opts.InitialFoodCount = 80
	}
	if opts.Interval <= 0 {
		opts.Interval = 20 * time.Millisecond
	}

	return &ClientEventStream{
		opts:        opts,
		timestampMS: 1_000 + opts.Seed,
		x:           opts.StartX,
		y:           opts.StartY,
		antIDs:      buildAntIDs(opts.StartAnt, opts.InitialAntCount),
		foodIDs:     buildFoodIDs(opts.StartFood, opts.InitialFoodCount),
	}
}

func (s *ClientEventStream) NextEvents(ctx context.Context, count int) []Event {
	events := make([]Event, 0, count)
	for len(events) < count {
		select {
		case <-ctx.Done():
			return events
		default:
			events = append(events, s.NextEvent())
		}
	}
	return events
}

func (s *ClientEventStream) NextEvent() Event {
	s.seq++
	advance := s.opts.Interval.Milliseconds()
	if advance < 1 {
		advance = 1
	}
	s.timestampMS += advance

	if s.seq == 1 {
		return Event{
			Type:        "sim_hello",
			SimID:       s.opts.SimID,
			Seq:         s.seq,
			TimestampMS: s.timestampMS,
			Payload: map[string]any{
				"sim_name": s.opts.SimName,
			},
		}
	}
	if s.seq == 2 {
		return Event{
			Type:        "sim_food_snapshot",
			SimID:       s.opts.SimID,
			Seq:         s.seq,
			TimestampMS: s.timestampMS,
			Payload: map[string]any{
				"foods": buildStartupFoods(s.foodIDs, s.opts.StartX, s.opts.StartY),
			},
		}
	}

	switch s.step % 3 {
	case 0:
		event := Event{
			Type:        "food_pickup",
			SimID:       s.opts.SimID,
			Seq:         s.seq,
			TimestampMS: s.timestampMS,
			Payload: map[string]any{
				"food_id": s.currentFoodID(),
				"ant_id":  s.currentAntID(),
			},
		}
		s.step++
		return event
	case 1:
		s.x += 1
		s.y += 2
		event := Event{
			Type:        "ant_turn_move",
			SimID:       s.opts.SimID,
			Seq:         s.seq,
			TimestampMS: s.timestampMS,
			Payload: map[string]any{
				"ant_id":       s.currentAntID(),
				"x":            s.x,
				"y":            s.y,
				"direction_x":  1.0,
				"direction_y":  0.0,
				"frame":        s.seq,
			},
		}
		s.step++
		return event
	default:
		event := Event{
			Type:        "food_drop",
			SimID:       s.opts.SimID,
			Seq:         s.seq,
			TimestampMS: s.timestampMS,
			Payload: map[string]any{
				"food_id": s.currentFoodID(),
				"ant_id":  s.currentAntID(),
				"x":       s.x,
				"y":       s.y,
			},
		}
		s.foodIndex++
		s.antIndex++
		s.step++
		return event
	}
}

func SendEvents(ctx context.Context, baseURL string, events []Event) error {
	wsURL := ingestWebsocketURL(baseURL)
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

func Run(ctx context.Context, opts RunOptions) error {
	if opts.ClientCount <= 0 {
		return nil
	}
	if opts.Interval <= 0 {
		opts.Interval = 20 * time.Millisecond
	}
	if opts.SimIDPrefix == "" {
		opts.SimIDPrefix = "sim"
	}
	if opts.InitialFoodCount <= 0 {
		opts.InitialFoodCount = 80
	}
	if opts.InitialAntCount <= 0 {
		opts.InitialAntCount = 26
	}

	runCtx := ctx
	cancel := func() {}
	if opts.Duration > 0 {
		runCtx, cancel = context.WithTimeout(ctx, opts.Duration)
	}
	defer cancel()

	errCh := make(chan error, opts.ClientCount)
	var wg sync.WaitGroup
	for i := 0; i < opts.ClientCount; i++ {
		wg.Add(1)
		go func(index int) {
			defer wg.Done()
			errCh <- runClient(runCtx, opts, index)
		}(i)
	}

	wg.Wait()
	close(errCh)

	for err := range errCh {
		if err != nil && err != context.DeadlineExceeded && err != context.Canceled {
			return err
		}
	}
	return nil
}

func ingestWebsocketURL(baseURL string) string {
	switch {
	case strings.HasPrefix(baseURL, "https://"):
		return "wss://" + strings.TrimPrefix(baseURL, "https://") + "/ws/ingest"
	default:
		return "ws://" + strings.TrimPrefix(baseURL, "http://") + "/ws/ingest"
	}
}

func runClient(ctx context.Context, opts RunOptions, index int) error {
	wsURL := ingestWebsocketURL(opts.BaseURL)
	conn, _, err := websocket.Dial(ctx, wsURL, nil)
	if err != nil {
		return err
	}
	defer conn.Close(websocket.StatusNormalClosure, "")

	stream := NewClientEventStream(ClientOptions{
		SimID:     fmt.Sprintf("%s-%03d", opts.SimIDPrefix, index),
		SimName:   fmt.Sprintf("%s-%03d", opts.SimIDPrefix, index),
		Seed:      opts.Seed + int64(index),
		StartX:    float64(index),
		StartY:    float64(index),
		StartFood: fmt.Sprintf("%s-food-%03d", opts.SimIDPrefix, index),
		StartAnt:  fmt.Sprintf("%s-ant-%03d", opts.SimIDPrefix, index),
		InitialAntCount: opts.InitialAntCount,
		InitialFoodCount: opts.InitialFoodCount,
		Interval:  opts.Interval,
	})

	if err := wsjson.Write(ctx, conn, stream.NextEvent()); err != nil {
		if ctx.Err() != nil {
			return ctx.Err()
		}
		return err
	}

	ticker := time.NewTicker(opts.Interval)
	defer ticker.Stop()

	for {
		select {
		case <-ctx.Done():
			return ctx.Err()
		case <-ticker.C:
			if err := wsjson.Write(ctx, conn, stream.NextEvent()); err != nil {
				if ctx.Err() != nil {
					return ctx.Err()
				}
				return err
			}
		}
	}
}

func (s *ClientEventStream) currentFoodID() string {
	return s.foodIDs[s.foodIndex%len(s.foodIDs)]
}

func (s *ClientEventStream) currentAntID() string {
	return s.antIDs[s.antIndex%len(s.antIDs)]
}

func buildAntIDs(base string, count int) []string {
	if count <= 1 {
		return []string{base}
	}

	ids := make([]string, 0, count)
	for i := 0; i < count; i++ {
		ids = append(ids, fmt.Sprintf("%s-%03d", base, i))
	}
	return ids
}

func buildFoodIDs(base string, count int) []string {
	if count <= 1 {
		return []string{base}
	}

	ids := make([]string, 0, count)
	for i := 0; i < count; i++ {
		ids = append(ids, fmt.Sprintf("%s-%03d", base, i))
	}
	return ids
}

func buildStartupFoods(foodIDs []string, startX, startY float64) []map[string]any {
	foods := make([]map[string]any, 0, len(foodIDs))
	for i, foodID := range foodIDs {
		cluster := i % 5
		ring := i / 5
		baseX := startX + float64(cluster*120)
		baseY := startY + float64((cluster%3)*90)
		offsetX := float64((ring%3)*9 - 9)
		offsetY := float64((ring/3)*9 - 9)
		foods = append(foods, map[string]any{
			"food_id": foodID,
			"x":       baseX + offsetX,
			"y":       baseY + offsetY,
		})
	}
	return foods
}
