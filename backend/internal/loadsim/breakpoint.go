package loadsim

import (
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"net/http"
	"strings"
	"sync"
	"sync/atomic"
	"time"

	"github.com/coder/websocket"
	"github.com/coder/websocket/wsjson"
)

type BreakpointFailureKind string

const (
	BreakpointFailureClientError   BreakpointFailureKind = "client_error"
	BreakpointFailureExactMismatch BreakpointFailureKind = "exact_mismatch"
	BreakpointFailureProgressStall BreakpointFailureKind = "progress_stall"
)

type BreakpointStep struct {
	Name              string
	BaseURL           string
	SimIDPrefix       string
	ClientCount       int
	ActivityTriplets  int
	Interval          time.Duration
	InitialAntCount   int
	InitialFoodCount  int
	Seed              int64
	SendTimeout       time.Duration
	SettleTimeout     time.Duration
	StallTimeout      time.Duration
	ProgressPollEvery time.Duration
}

type BreakpointTotals struct {
	SentEvents     int
	ConnectedSims  int
	PickupCount    int
	DropCount      int
	TurnMoveCount  int
	LooseFoodCount int
}

type BreakpointFailure struct {
	Kind         BreakpointFailureKind
	Message      string
	ClientErrors []string
}

type BreakpointStepResult struct {
	Step      BreakpointStep
	Expected  BreakpointTotals
	Observed  BreakpointTotals
	Failure   *BreakpointFailure
	StartedAt time.Time
	Elapsed   time.Duration
}

type BreakpointSearchConfig struct {
	Steps   []BreakpointStep
	RunStep func(context.Context, BreakpointStep) (BreakpointStepResult, error)
}

type BreakpointReport struct {
	LastGood *BreakpointStepResult
	FirstBad *BreakpointStepResult
	Results  []BreakpointStepResult
}

func RunBreakpointSearch(ctx context.Context, cfg BreakpointSearchConfig) (BreakpointReport, error) {
	if len(cfg.Steps) == 0 {
		return BreakpointReport{}, errors.New("breakpoint search requires at least one step")
	}
	if cfg.RunStep == nil {
		return BreakpointReport{}, errors.New("breakpoint search requires a step runner")
	}

	report := BreakpointReport{
		Results: make([]BreakpointStepResult, 0, len(cfg.Steps)),
	}

	for _, step := range cfg.Steps {
		result, err := cfg.RunStep(ctx, step)
		if err != nil {
			return report, err
		}
		report.Results = append(report.Results, result)
		if result.Failure != nil {
			failed := result
			report.FirstBad = &failed
			return report, nil
		}
		passed := result
		report.LastGood = &passed
	}

	return report, nil
}

func BuildClientCountRamp(start, max, increment int, base BreakpointStep) []BreakpointStep {
	if start <= 0 || max < start || increment <= 0 {
		return nil
	}

	steps := make([]BreakpointStep, 0, 1+(max-start)/increment)
	for clientCount := start; clientCount <= max; clientCount += increment {
		step := base
		step.ClientCount = clientCount
		if step.Name == "" {
			step.Name = fmt.Sprintf("clients-%d", clientCount)
		} else {
			step.Name = fmt.Sprintf("%s-%d", base.Name, clientCount)
		}
		steps = append(steps, step)
	}
	return steps
}

func RunBackendBreakpointStep(ctx context.Context, step BreakpointStep) (BreakpointStepResult, error) {
	step = normalizedBreakpointStep(step)
	if step.BaseURL == "" {
		return BreakpointStepResult{}, errors.New("breakpoint step requires a base URL")
	}

	startedAt := time.Now()
	plannedSentEvents := step.ClientCount * (2 + step.ActivityTriplets*3)
	actualPrefix := fmt.Sprintf("%s-%d", step.SimIDPrefix, startedAt.UnixNano())
	step.SimIDPrefix = actualPrefix

	sendCtx, cancel := context.WithTimeout(ctx, step.SendTimeout)
	defer cancel()

	start := make(chan struct{})
	errCh := make(chan error, step.ClientCount)
	totalsCh := make(chan BreakpointTotals, step.ClientCount)
	doneCh := make(chan struct{})
	var sentCounters atomicBreakpointTotals
	var wg sync.WaitGroup

	for i := 0; i < step.ClientCount; i++ {
		wg.Add(1)
		go func(index int) {
			defer wg.Done()
			<-start

			clientTotals, err := runBreakpointClient(sendCtx, step, index, &sentCounters)
			totalsCh <- clientTotals
			errCh <- err
		}(i)
	}

	close(start)
	go func() {
		wg.Wait()
		close(totalsCh)
		close(errCh)
		close(doneCh)
	}()

	var lastObserved BreakpointTotals
	lastProgressAt := time.Now()
	progressTicker := time.NewTicker(step.ProgressPollEvery)
	defer progressTicker.Stop()

	var progressFailure *BreakpointFailure
	for {
		select {
		case <-doneCh:
			goto completed
		case <-progressTicker.C:
			observed, err := fetchBreakpointTotals(sendCtx, step.BaseURL, step.SimIDPrefix, step.InitialFoodCount)
			if err != nil {
				return BreakpointStepResult{}, err
			}
			if observed != lastObserved {
				lastObserved = observed
				lastProgressAt = time.Now()
			}
			if sentCounters.SentEvents() < int64(plannedSentEvents) && time.Since(lastProgressAt) >= step.StallTimeout {
				progressFailure = &BreakpointFailure{
					Kind:    BreakpointFailureProgressStall,
					Message: fmt.Sprintf("backend made no observable progress for %s while clients were still sending", step.StallTimeout),
				}
				cancel()
				<-doneCh
				goto completed
			}
		case <-sendCtx.Done():
			<-doneCh
			goto completed
		}
	}

completed:
	expected, clientErrors := collectBreakpointResults(totalsCh, errCh)
	observed, err := waitForBreakpointTotals(sendCtx, step.BaseURL, step.SimIDPrefix, step.InitialFoodCount, expected, step.SettleTimeout)
	if err != nil {
		return BreakpointStepResult{}, err
	}

	result := BreakpointStepResult{
		Step:      step,
		Expected:  expected,
		Observed:  observed,
		StartedAt: startedAt,
		Elapsed:   time.Since(startedAt),
	}

	if progressFailure != nil {
		progressFailure.ClientErrors = clientErrors
		result.Failure = progressFailure
		return result, nil
	}
	if len(clientErrors) > 0 {
		result.Failure = &BreakpointFailure{
			Kind:         BreakpointFailureClientError,
			Message:      "one or more load clients failed while sending events",
			ClientErrors: clientErrors,
		}
		return result, nil
	}
	if observed != expected {
		result.Failure = &BreakpointFailure{
			Kind:    BreakpointFailureExactMismatch,
			Message: "backend totals did not match successfully sent totals before settle timeout",
		}
	}

	return result, nil
}

func normalizedBreakpointStep(step BreakpointStep) BreakpointStep {
	if step.Name == "" {
		step.Name = "breakpoint"
	}
	if step.SimIDPrefix == "" {
		step.SimIDPrefix = strings.ReplaceAll(step.Name, " ", "-")
	}
	if step.ClientCount <= 0 {
		step.ClientCount = 1
	}
	if step.ActivityTriplets <= 0 {
		step.ActivityTriplets = 20
	}
	if step.Interval <= 0 {
		step.Interval = 5 * time.Millisecond
	}
	if step.InitialAntCount <= 0 {
		step.InitialAntCount = 26
	}
	if step.InitialFoodCount <= 0 {
		step.InitialFoodCount = 80
	}
	if step.SendTimeout <= 0 {
		step.SendTimeout = 5 * time.Second
	}
	if step.SettleTimeout <= 0 {
		step.SettleTimeout = 5 * time.Second
	}
	if step.StallTimeout <= 0 {
		step.StallTimeout = 1500 * time.Millisecond
	}
	if step.ProgressPollEvery <= 0 {
		step.ProgressPollEvery = 100 * time.Millisecond
	}
	return step
}

type atomicBreakpointTotals struct {
	sentEvents     atomic.Int64
	connectedSims  atomic.Int64
	pickupCount    atomic.Int64
	dropCount      atomic.Int64
	turnMoveCount  atomic.Int64
	looseFoodCount atomic.Int64
}

func (t *atomicBreakpointTotals) record(event Event, initialFoodCount int) {
	t.sentEvents.Add(1)
	switch event.Type {
	case "sim_hello":
		t.connectedSims.Add(1)
	case "sim_food_snapshot":
		t.looseFoodCount.Add(int64(initialFoodCount))
	case "food_pickup":
		t.pickupCount.Add(1)
		t.looseFoodCount.Add(-1)
	case "food_drop":
		t.dropCount.Add(1)
		t.looseFoodCount.Add(1)
	case "ant_turn_move":
		t.turnMoveCount.Add(1)
	}
}

func (t *atomicBreakpointTotals) SentEvents() int64 {
	return t.sentEvents.Load()
}

func runBreakpointClient(ctx context.Context, step BreakpointStep, index int, totals *atomicBreakpointTotals) (BreakpointTotals, error) {
	wsURL := ingestWebsocketURL(step.BaseURL)
	conn, _, err := websocket.Dial(ctx, wsURL, nil)
	if err != nil {
		return BreakpointTotals{}, err
	}
	defer conn.Close(websocket.StatusNormalClosure, "")

	stream := NewClientEventStream(ClientOptions{
		SimID:            fmt.Sprintf("%s-%03d", step.SimIDPrefix, index),
		SimName:          fmt.Sprintf("%s-%03d", step.SimIDPrefix, index),
		Seed:             step.Seed + int64(index),
		StartX:           float64(index),
		StartY:           float64(index),
		StartFood:        fmt.Sprintf("%s-food-%03d", step.SimIDPrefix, index),
		StartAnt:         fmt.Sprintf("%s-ant-%03d", step.SimIDPrefix, index),
		InitialAntCount:  step.InitialAntCount,
		InitialFoodCount: step.InitialFoodCount,
		Interval:         step.Interval,
	})

	local := BreakpointTotals{}
	for i := 0; i < 2+step.ActivityTriplets*3; i++ {
		event := stream.NextEvent()
		if err := wsjson.Write(ctx, conn, event); err != nil {
			if ctx.Err() != nil {
				return local, ctx.Err()
			}
			return local, err
		}
		totals.record(event, step.InitialFoodCount)
		local.record(event, step.InitialFoodCount)
	}

	return local, nil
}

func (t *BreakpointTotals) record(event Event, initialFoodCount int) {
	t.SentEvents++
	switch event.Type {
	case "sim_hello":
		t.ConnectedSims++
	case "sim_food_snapshot":
		t.LooseFoodCount += initialFoodCount
	case "food_pickup":
		t.PickupCount++
		t.LooseFoodCount--
	case "food_drop":
		t.DropCount++
		t.LooseFoodCount++
	case "ant_turn_move":
		t.TurnMoveCount++
	}
}

func collectBreakpointResults(totalsCh <-chan BreakpointTotals, errCh <-chan error) (BreakpointTotals, []string) {
	var expected BreakpointTotals
	for totals := range totalsCh {
		expected.SentEvents += totals.SentEvents
		expected.ConnectedSims += totals.ConnectedSims
		expected.PickupCount += totals.PickupCount
		expected.DropCount += totals.DropCount
		expected.TurnMoveCount += totals.TurnMoveCount
		expected.LooseFoodCount += totals.LooseFoodCount
	}

	var clientErrors []string
	for err := range errCh {
		if err != nil && !errors.Is(err, context.Canceled) && !errors.Is(err, context.DeadlineExceeded) {
			clientErrors = append(clientErrors, err.Error())
		}
	}

	return expected, clientErrors
}

func waitForBreakpointTotals(ctx context.Context, baseURL, simIDPrefix string, initialFoodCount int, expected BreakpointTotals, timeout time.Duration) (BreakpointTotals, error) {
	deadline := time.Now().Add(timeout)
	var last BreakpointTotals
	for time.Now().Before(deadline) {
		observed, err := fetchBreakpointTotals(ctx, baseURL, simIDPrefix, initialFoodCount)
		if err != nil {
			return BreakpointTotals{}, err
		}
		last = observed
		if observed == expected {
			return observed, nil
		}
		select {
		case <-ctx.Done():
			return last, nil
		case <-time.After(100 * time.Millisecond):
		}
	}
	return last, nil
}

type breakpointSimSummary struct {
	SimID          string `json:"sim_id"`
	PickupCount    int    `json:"pickup_count"`
	DropCount      int    `json:"drop_count"`
	TurnMoveCount  int    `json:"turn_move_count"`
	LooseFoodCount int    `json:"loose_food_count"`
}

func fetchBreakpointTotals(ctx context.Context, baseURL, simIDPrefix string, initialFoodCount int) (BreakpointTotals, error) {
	req, err := http.NewRequestWithContext(ctx, http.MethodGet, baseURL+"/api/sims", nil)
	if err != nil {
		return BreakpointTotals{}, err
	}

	resp, err := http.DefaultClient.Do(req)
	if err != nil {
		return BreakpointTotals{}, err
	}
	defer resp.Body.Close()

	var sims []breakpointSimSummary
	if err := json.NewDecoder(resp.Body).Decode(&sims); err != nil {
		return BreakpointTotals{}, err
	}

	var totals BreakpointTotals
	for _, sim := range sims {
		if !strings.HasPrefix(sim.SimID, simIDPrefix) {
			continue
		}
		totals.ConnectedSims++
		totals.PickupCount += sim.PickupCount
		totals.DropCount += sim.DropCount
		totals.TurnMoveCount += sim.TurnMoveCount
		totals.LooseFoodCount += sim.LooseFoodCount
	}
	if initialFoodCount > 0 {
		numerator := totals.LooseFoodCount + totals.PickupCount - totals.DropCount
		if numerator >= 0 && numerator%initialFoodCount == 0 {
			snapshotCount := numerator / initialFoodCount
			totals.SentEvents = totals.ConnectedSims + snapshotCount + totals.PickupCount + totals.DropCount + totals.TurnMoveCount
		}
	}
	return totals, nil
}
