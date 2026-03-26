package server

import (
	"context"
	"fmt"
	"net/http"
	"os"
	"testing"
	"time"

	"github.com/antont/gatherers/backend/internal/config"
	"github.com/antont/gatherers/backend/internal/loadsim"
	"github.com/coder/websocket"
	"github.com/coder/websocket/wsjson"
)

func TestLongStressDashboardReflectsSustainedLoad(t *testing.T) {
	if os.Getenv("GATHERERS_RUN_LONG_STRESS") == "" {
		t.Skip("set GATHERERS_RUN_LONG_STRESS=1 to run sustained load coverage")
	}

	srv := New(config.Config{})
	target := newTestTarget(t, srv.Handler())
	defer target.closeFn()

	ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
	defer cancel()

	dashboardConn, _, err := websocket.Dial(ctx, websocketURL(target.baseURL, "/ws/dashboard"), nil)
	if err != nil {
		t.Fatalf("expected dashboard websocket endpoint to accept connection: %v", err)
	}
	defer dashboardConn.Close(websocket.StatusNormalClosure, "")

	var initial dashboardSnapshotMessage
	if err := wsjson.Read(ctx, dashboardConn, &initial); err != nil {
		t.Fatalf("expected initial dashboard snapshot: %v", err)
	}

	err = loadsim.Run(ctx, loadsim.RunOptions{
		BaseURL:     target.baseURL,
		ClientCount: 25,
		Duration:    2 * time.Second,
		Interval:    25 * time.Millisecond,
		Seed:        99,
		SimIDPrefix: "long",
	})
	if err != nil {
		t.Fatalf("expected long stress load to complete successfully: %v", err)
	}

	update := waitForDashboardSnapshot(t, ctx, dashboardConn, func(snapshot dashboardSnapshotMessage) bool {
		return snapshot.Summary.ConnectedSimCount >= 25 &&
			snapshot.Summary.LooseFoodCount >= 10
	})

	if update.Summary.ConnectedSimCount < 25 {
		t.Fatalf("expected at least 25 connected sims, got %d", update.Summary.ConnectedSimCount)
	}

	summary := fetchSummary(t, target.baseURL)
	if summary.ConnectedSimCount < 25 {
		t.Fatalf("expected summary endpoint to report at least 25 connected sims, got %d", summary.ConnectedSimCount)
	}

	resp, err := http.Get(target.baseURL + "/api/sims")
	if err != nil {
		t.Fatalf("expected sims endpoint to respond: %v", err)
	}
	defer resp.Body.Close()
}

func TestFindBreakpointUnderRampLoad(t *testing.T) {
	if os.Getenv("GATHERERS_FIND_BREAKPOINT") == "" {
		t.Skip("set GATHERERS_FIND_BREAKPOINT=1 to run breakpoint search")
	}

	srv := New(config.Config{})
	target := newTestTarget(t, srv.Handler())
	defer target.closeFn()

	ctx, cancel := context.WithTimeout(context.Background(), breakpointEnvDuration("GATHERERS_BREAKPOINT_TEST_TIMEOUT", 30*time.Second))
	defer cancel()

	report, err := loadsim.RunBreakpointSearch(ctx, buildBreakpointSearchConfig(target.baseURL))
	if err != nil {
		t.Fatalf("expected breakpoint search to complete cleanly: %v", err)
	}

	if report.FirstBad != nil {
		t.Fatalf(
			"breakpoint found at client_count=%d after last_good=%v: reason=%s message=%s expected=%+v observed=%+v client_errors=%v",
			report.FirstBad.Step.ClientCount,
			describeBreakpointResult(report.LastGood),
			report.FirstBad.Failure.Kind,
			report.FirstBad.Failure.Message,
			report.FirstBad.Expected,
			report.FirstBad.Observed,
			report.FirstBad.Failure.ClientErrors,
		)
	}

	t.Logf("no breakpoint found up to %s", describeBreakpointResult(report.LastGood))
}

func describeBreakpointResult(result *loadsim.BreakpointStepResult) string {
	if result == nil {
		return "none"
	}
	return fmt.Sprintf("client_count=%d expected=%+v observed=%+v", result.Step.ClientCount, result.Expected, result.Observed)
}
