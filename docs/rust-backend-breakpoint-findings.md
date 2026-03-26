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

## Failure Shape

The failing steps reported:

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

The final measured Rust result after the learning-first sequence reached:

- last good: `155` clients
- first bad: `160` clients

So, for this workload on this machine, the current Go backend still clearly outperforms the current Rust backend, but the gap is now much smaller than it was at the original baseline.

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

## What To Vary Next

If future runs want more insight instead of just one threshold, the next useful experiments are:

- repeat the final Rust search with multiple fresh backend starts per point to reduce edge variance
- vary `activity-triplets` to separate sustained event pressure from connection count
- vary `startup-food-count` to isolate startup payload pressure
- vary `interval` to separate message rate from concurrency
- profile the Rust backend during the failing `155-160` range to find the next dominant bottleneck after cadence matching
