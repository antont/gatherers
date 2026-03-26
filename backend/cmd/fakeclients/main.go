package main

import (
	"context"
	"flag"
	"log"
	"os"
	"os/signal"
	"syscall"
	"time"

	"github.com/antont/gatherers/backend/internal/loadsim"
)

func main() {
	var (
		baseURL     = flag.String("base-url", "http://127.0.0.1:8080", "target backend base URL")
		clientCount = flag.Int("clients", 100, "number of fake clients")
		duration    = flag.Duration("duration", 20*time.Second, "how long to keep sending events")
		interval    = flag.Duration("interval", 100*time.Millisecond, "per-client event interval")
		seed        = flag.Int64("seed", 1, "deterministic seed offset")
		simIDPrefix = flag.String("sim-id-prefix", "fake", "prefix for generated sim IDs")
	)
	flag.Parse()

	ctx, stop := signal.NotifyContext(context.Background(), os.Interrupt, syscall.SIGTERM)
	defer stop()

	if err := loadsim.Run(ctx, loadsim.RunOptions{
		BaseURL:     *baseURL,
		ClientCount: *clientCount,
		Duration:    *duration,
		Interval:    *interval,
		Seed:        *seed,
		SimIDPrefix: *simIDPrefix,
	}); err != nil && err != context.Canceled && err != context.DeadlineExceeded {
		log.Fatalf("fakeclients failed: %v", err)
	}
}
