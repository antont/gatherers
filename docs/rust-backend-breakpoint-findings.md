# Rust Backend Breakpoint Findings

## Purpose

This note records one concrete breakpoint search run against the current Rust backend on one local machine.

It is not a universal backend limit.

The reported breakpoint depends on:

- machine performance
- current backend code
- test workload shape
- timeout settings

## Workload Used

The breakpoint runner used the same fixed workload shape as the recorded Go run while ramping `client_count`:

- startup food count: `80`
- activity triplets per client: `20`
- interval: `5ms`
- one-dimensional ramp: `client_count` only
- send timeout: `30s`
- settle timeout: `30s`
- stall timeout: `5s`

Each client step sends:

- `sim_hello`
- `sim_food_snapshot`
- then `20` repetitions of:
  - `food_pickup`
  - `ant_turn_move`
  - `food_drop`

That means each client sends `62` events total for the step.

## Findings

### Coarse pass with 30s send/settle timeouts

Observed threshold:

- last good: `100` clients
- first bad: `110` clients

### Narrowing pass with 30s send/settle timeouts

Observed threshold:

- last good: `100` clients
- first bad: `101` clients

### Fresh confirmation sweep

Observed threshold:

- last good: `99` clients
- first bad: `100` clients

Fresh run detail:

- observed totals at `100`: `SentEvents=52 ConnectedSims=6 PickupCount=15 DropCount=15 TurnMoveCount=15 LooseFoodCount=80`

## Learning-First Experiment Results

Starting from the baseline above, the Rust backend was then improved in four measured steps:

| Step | Change | Last good | First bad | Delta vs previous |
|---|---|---:|---:|---:|
| Baseline | pre-experiment reference | 99 | 100 | - |
| 1 | async-friendly refresh coordination | 107 | 108 | +8 |
| 2 | shard store writes by sim | 118 | 119 | +11 |
| 3 | move snapshot compute off Tokio workers | 128 | 129 | +10 |
| 4 | Go-style demand worker and adaptive cadence | 155 | 160 | +27 |

The biggest single move came from step 4.

That matters because it means the Rust backend was not only paying for low-level locking and runtime-thread placement. The refresh policy itself was a major part of the gap.

After the full learning-first sequence, the current practical breakpoint for this workload on this machine is:

- last good: `155`
- first bad: `160`

That is a large improvement over the original `99-100` baseline, but it still trails the current Go result on the same workload.

## Live Vs Analytics Separation Result

After splitting cheap live ingest-derived counters from heavy analytics, the externally observed breakpoint moved dramatically again.

### Initial separation measurement

- strongest passing configuration in the first new sweep: `1000` clients
- strongest passing configuration in the higher sweep: `1600` clients
- first observed failure in the higher sweep: `1800` clients

### Post-consistency-fix measurement

The initial separation had correctness issues: the live cache used arithmetic deltas that could diverge from the authoritative store on duplicate or idempotent food events, and dashboard websocket subscribers missed live-only updates. After fixing those:

- `Store::apply_event()` returns an `EventOutcome` with the authoritative per-sim `loose_food_count`
- the live cache sets counts from the outcome instead of guessing with `+1` / `saturating_sub(1)`
- every event broadcasts to dashboard websocket subscribers, not only analytics-affecting events
- the `broadcast::Receiver` handles `Lagged` errors gracefully instead of disconnecting

Measured result after the fix:

- strongest passing in coarse sweep: `1200` clients
- strongest passing in narrowing sweep: `1500` clients (all steps 1200-1500 passed)
- strongest passing in high-range sweep: `2000` clients
- first observed failure: `2250` clients

At `2000`, the observed totals still matched exactly:

- `SentEvents=124000`
- `ConnectedSims=2000`
- `PickupCount=40000`
- `DropCount=40000`
- `TurnMoveCount=40000`
- `LooseFoodCount=160000`

### Failure shape

The failure at `2250` was purely connection-level:

- failure kind: `client_error`
- specific shape: many websocket dial timeouts while the load generator was still trying to open `/ws/ingest` connections

No `exact_mismatch` failures were observed at any client count. Every connected client's events were correctly and promptly visible through the live counters.

## Historical Failure Shape

Before the live-vs-analytics separation, the failing steps reported:

- failure kind: `exact_mismatch`
- observed totals: sometimes effectively zero, and on a fresh confirmation run partial but far-from-complete progress

This should be read carefully.

It does **not** mean the backend cleanly processed almost the whole step and only dropped a tiny suffix of events.

Instead, for those steps and timeout windows, the runner never observed the expected sim totals through the backend API before the deadline expired. In some runs that looked like almost no visible progress; in the freshest confirmation run it looked like limited progress that still stalled far short of the expected totals.

So this finding is best described as:

- a timeout-bound load breakpoint

not yet as:

- a proven minimal event-loss breakpoint under unlimited wait

## Baseline Commands Used

Coarse search:

```bash
cd backend
go run ./cmd/breakpoint \
  --base-url http://127.0.0.1:18220 \
  --start-clients 100 \
  --max-clients 160 \
  --step 10 \
  --timeout 180s \
  --send-timeout 30s \
  --settle-timeout 30s \
  --stall-timeout 5s
```

First narrowing pass:

```bash
cd backend
go run ./cmd/breakpoint \
  --base-url http://127.0.0.1:18220 \
  --start-clients 101 \
  --max-clients 110 \
  --step 1 \
  --timeout 220s \
  --send-timeout 30s \
  --settle-timeout 30s \
  --stall-timeout 5s
```

Fresh confirmation sweep:

```bash
cd backend
go run ./cmd/breakpoint \
  --base-url http://127.0.0.1:18230 \
  --start-clients 99 \
  --max-clients 102 \
  --step 1 \
  --timeout 220s \
  --send-timeout 30s \
  --settle-timeout 30s \
  --stall-timeout 5s
```

## Comparison With Go

The recorded Go note in `docs/go-backend-breakpoint-findings.md` reached:

- last good: `268` clients
- first bad: `269` clients

The learning-first Rust result before the live-vs-analytics split reached:

- last good: `155` clients
- first bad: `160` clients

After the live-vs-analytics split and consistency fix, the Rust backend:

- matched exact live totals through `2000` clients in the measured sweep
- first showed failure at `2250`, and that failure was `client_error` during websocket dialing rather than stale live totals

The current Rust backend outperforms the recorded Go backend on the exact-count live-counter dimension that the breakpoint harness measures. The trade-off is that this measures a deliberately different architecture: live counters stay fresh while heavy analytics are allowed to lag independently.

## Commands Used

Final coarse search after Go-style cadence:

```bash
cd backend
go run ./cmd/breakpoint \
  --base-url http://127.0.0.1:18237 \
  --start-clients 125 \
  --max-clients 250 \
  --step 25 \
  --timeout 220s \
  --send-timeout 30s \
  --settle-timeout 30s \
  --stall-timeout 5s
```

Final narrowing pass after Go-style cadence:

```bash
cd backend
go run ./cmd/breakpoint \
  --base-url http://127.0.0.1:18238 \
  --start-clients 150 \
  --max-clients 175 \
  --step 5 \
  --timeout 220s \
  --send-timeout 30s \
  --settle-timeout 30s \
  --stall-timeout 5s
```

Live-vs-analytics separation sweep:

```bash
cd backend
go run ./cmd/breakpoint \
  --base-url http://127.0.0.1:18239 \
  --start-clients 150 \
  --max-clients 350 \
  --step 25 \
  --timeout 220s \
  --send-timeout 30s \
  --settle-timeout 30s \
  --stall-timeout 5s
```

Higher-range confirmation (pre-consistency-fix):

```bash
cd backend
go run ./cmd/breakpoint \
  --base-url http://127.0.0.1:18239 \
  --start-clients 1200 \
  --max-clients 2000 \
  --step 200 \
  --timeout 220s \
  --send-timeout 30s \
  --settle-timeout 30s \
  --stall-timeout 5s
```

Post-consistency-fix coarse sweep:

```bash
cd backend
go run ./cmd/breakpoint \
  --base-url http://127.0.0.1:18270 \
  --start-clients 200 \
  --max-clients 2000 \
  --step 200 \
  --timeout 300s \
  --send-timeout 30s \
  --settle-timeout 30s \
  --stall-timeout 5s
```

Post-consistency-fix high-range sweep:

```bash
cd backend
go run ./cmd/breakpoint \
  --base-url http://127.0.0.1:18272 \
  --start-clients 1500 \
  --max-clients 3000 \
  --step 250 \
  --timeout 300s \
  --send-timeout 30s \
  --settle-timeout 30s \
  --stall-timeout 5s
```

## What To Vary Next

If future runs want more insight, the next useful experiments are:

- add explicit analytics-lag measurements under the breakpoint workload, since the main live-counter bottleneck has moved far outward
- repeat the high-range Rust search with multiple fresh backend starts per point to reduce edge variance
- vary `activity-triplets` to separate sustained event pressure from connection count
- vary `startup-food-count` to isolate startup payload pressure
- vary `interval` to separate message rate from concurrency
- profile connection setup and accept backlog behavior in the `2000-2250` range, because the first observed failure is websocket dial timeout rather than stale live totals
