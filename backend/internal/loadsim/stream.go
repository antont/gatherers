package loadsim

import (
	"context"
	"strings"
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
	Interval  time.Duration
}

type Event struct {
	Type        string `json:"type"`
	SimID       string `json:"sim_id"`
	Seq         uint64 `json:"seq"`
	TimestampMS int64  `json:"timestamp_ms"`
	Payload     any    `json:"payload,omitempty"`
}

type ClientEventStream struct {
	opts        ClientOptions
	seq         uint64
	timestampMS int64
	x           float64
	y           float64
	step        int
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
	if opts.Interval <= 0 {
		opts.Interval = 20 * time.Millisecond
	}

	return &ClientEventStream{
		opts:        opts,
		timestampMS: 1_000 + opts.Seed,
		x:           opts.StartX,
		y:           opts.StartY,
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

	switch s.step % 3 {
	case 0:
		event := Event{
			Type:        "food_drop",
			SimID:       s.opts.SimID,
			Seq:         s.seq,
			TimestampMS: s.timestampMS,
			Payload: map[string]any{
				"food_id": s.opts.StartFood,
				"x":       s.x,
				"y":       s.y,
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
				"ant_id":       s.opts.StartAnt,
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
			Type:        "food_pickup",
			SimID:       s.opts.SimID,
			Seq:         s.seq,
			TimestampMS: s.timestampMS,
			Payload: map[string]any{
				"food_id": s.opts.StartFood,
			},
		}
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

func ingestWebsocketURL(baseURL string) string {
	switch {
	case strings.HasPrefix(baseURL, "https://"):
		return "wss://" + strings.TrimPrefix(baseURL, "https://") + "/ws/ingest"
	default:
		return "ws://" + strings.TrimPrefix(baseURL, "http://") + "/ws/ingest"
	}
}
