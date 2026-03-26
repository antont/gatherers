package main

import (
	"context"
	"flag"
	"fmt"
	"net/http/httptest"
	"os"
	"strings"
	"time"

	"github.com/antont/gatherers/backend/internal/config"
	"github.com/antont/gatherers/backend/internal/loadsim"
	backendserver "github.com/antont/gatherers/backend/internal/server"
)

func main() {
	var (
		baseURL           = flag.String("base-url", "", "existing backend base URL; omit to spawn an in-process backend")
		startClients      = flag.Int("start-clients", 25, "starting client count")
		maxClients        = flag.Int("max-clients", 100, "maximum client count")
		stepClients       = flag.Int("step", 25, "client-count increment")
		interval          = flag.Duration("interval", 5*time.Millisecond, "per-event timestamp interval")
		initialFoodCount  = flag.Int("startup-food-count", 80, "startup food count per client")
		initialAntCount   = flag.Int("ant-count", 26, "startup ant count per client")
		activityTriplets  = flag.Int("activity-triplets", 20, "pickup/move/drop triplets per client")
		sendTimeout       = flag.Duration("send-timeout", 5*time.Second, "per-step send timeout")
		settleTimeout     = flag.Duration("settle-timeout", 5*time.Second, "per-step settle timeout")
		stallTimeout      = flag.Duration("stall-timeout", 1500*time.Millisecond, "stall timeout while clients are still sending")
		progressPollEvery = flag.Duration("poll-interval", 100*time.Millisecond, "backend polling interval during a step")
		globalTimeout     = flag.Duration("timeout", 30*time.Second, "overall search timeout")
		seed              = flag.Int64("seed", 700, "base seed for deterministic client generation")
	)
	flag.Parse()

	runBaseURL, closeFn, spawned, err := resolveBaseURL(strings.TrimRight(*baseURL, "/"))
	if err != nil {
		fmt.Fprintf(os.Stderr, "error: %v\n", err)
		os.Exit(2)
	}
	defer closeFn()

	if spawned {
		fmt.Printf("spawned in-process backend at %s\n", runBaseURL)
	} else {
		fmt.Printf("using backend at %s\n", runBaseURL)
	}

	base := loadsim.BreakpointStep{
		Name:              "breakpoint",
		BaseURL:           runBaseURL,
		Interval:          *interval,
		InitialFoodCount:  *initialFoodCount,
		InitialAntCount:   *initialAntCount,
		ActivityTriplets:  *activityTriplets,
		SendTimeout:       *sendTimeout,
		SettleTimeout:     *settleTimeout,
		StallTimeout:      *stallTimeout,
		ProgressPollEvery: *progressPollEvery,
		Seed:              *seed,
	}
	steps := loadsim.BuildClientCountRamp(*startClients, *maxClients, *stepClients, base)
	if len(steps) == 0 {
		fmt.Fprintln(os.Stderr, "error: no breakpoint steps were generated")
		os.Exit(2)
	}

	ctx, cancel := context.WithTimeout(context.Background(), *globalTimeout)
	defer cancel()

	report, err := loadsim.RunBreakpointSearch(ctx, loadsim.BreakpointSearchConfig{
		Steps: steps,
		RunStep: func(ctx context.Context, step loadsim.BreakpointStep) (loadsim.BreakpointStepResult, error) {
			fmt.Printf("running step: client_count=%d interval=%s startup_food=%d triplets=%d\n", step.ClientCount, step.Interval, step.InitialFoodCount, step.ActivityTriplets)
			return loadsim.RunBackendBreakpointStep(ctx, step)
		},
	})
	if err != nil {
		fmt.Fprintf(os.Stderr, "search failed: %v\n", err)
		os.Exit(2)
	}

	if report.FirstBad != nil {
		fmt.Printf("last good: %s\n", describeResult(report.LastGood))
		fmt.Printf("first bad: %s\n", describeResult(report.FirstBad))
		fmt.Printf("failure reason: %s - %s\n", report.FirstBad.Failure.Kind, report.FirstBad.Failure.Message)
		if len(report.FirstBad.Failure.ClientErrors) > 0 {
			fmt.Printf("client errors: %v\n", report.FirstBad.Failure.ClientErrors)
		}
		os.Exit(1)
	}

	fmt.Printf("strongest passing configuration: %s\n", describeResult(report.LastGood))
}

func resolveBaseURL(baseURL string) (string, func(), bool, error) {
	if baseURL != "" {
		return baseURL, func() {}, false, nil
	}

	srv := backendserver.New(config.Config{})
	server := httptest.NewServer(srv.Handler())
	return server.URL, server.Close, true, nil
}

func describeResult(result *loadsim.BreakpointStepResult) string {
	if result == nil {
		return "none"
	}
	return fmt.Sprintf(
		"client_count=%d expected=%+v observed=%+v elapsed=%s",
		result.Step.ClientCount,
		result.Expected,
		result.Observed,
		result.Elapsed.Round(time.Millisecond),
	)
}
