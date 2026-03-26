# Go Backend Breakpoint Findings

## Purpose

This note records one concrete breakpoint search run against the current Go backend on one local machine.

It is not a universal backend limit.

The reported breakpoint depends on:

- machine performance
- current backend code
- test workload shape
- timeout settings

## Workload Used

The breakpoint runner used this fixed workload while ramping `client_count`:

- startup food count: `80`
- activity triplets per client: `20`
- interval: `5ms`
- one-dimensional ramp: `client_count` only

Each client step sends:

- `sim_hello`
- `sim_food_snapshot`
- then `20` repetitions of:
  - `food_pickup`
  - `ant_turn_move`
  - `food_drop`

That means each client sends `62` events total for the step.

## Findings

### First pass with 10s send/settle timeouts

Observed threshold:

- last good: `255` clients
- first bad: `260` clients

### Narrower pass with 30s send/settle timeouts

Observed threshold:

- last good: `268` clients
- first bad: `269` clients

## Interpretation

The important result is that the first failure moved upward when the timeouts were relaxed.

That means the initial `260` breakpoint was not a pure “events are definitely lost at 260” conclusion. It was a timeout-bound breakpoint for this workload and these time budgets.

With more generous timeouts, the same backend and workload still completed up to `268` clients, and then failed at `269`.

So the current practical breakpoint for this exact local run was:

- around `269` concurrent clients

But more precisely, this means:

- at `268`, the step completed and the backend caught up within the configured window
- at `269`, the step no longer became observable through the backend within the configured window

## Failure Shape

The failing step reported:

- failure kind: `exact_mismatch`
- observed totals: all zero before timeout

This should be read carefully.

It does **not** mean the backend cleanly processed part of the step and dropped a small suffix of events.

Instead, for that step and timeout window, the runner never observed the expected sim totals through the backend API before the deadline expired.

So this finding is best described as:

- a timeout-bound load breakpoint

not yet as:

- a proven minimal event-loss breakpoint under unlimited wait

## Commands Used

Initial coarse search:

```bash
cd backend
go run ./cmd/breakpoint \
  --start-clients 100 \
  --max-clients 1000 \
  --step 100 \
  --timeout 120s \
  --send-timeout 10s \
  --settle-timeout 10s \
  --stall-timeout 3s
```

First narrowing pass:

```bash
cd backend
go run ./cmd/breakpoint \
  --start-clients 225 \
  --max-clients 300 \
  --step 25 \
  --timeout 120s \
  --send-timeout 10s \
  --settle-timeout 10s \
  --stall-timeout 3s
```

Second narrowing pass:

```bash
cd backend
go run ./cmd/breakpoint \
  --start-clients 255 \
  --max-clients 275 \
  --step 5 \
  --timeout 120s \
  --send-timeout 10s \
  --settle-timeout 10s \
  --stall-timeout 3s
```

Longer-timeout confirmation:

```bash
cd backend
go run ./cmd/breakpoint \
  --start-clients 255 \
  --max-clients 270 \
  --step 5 \
  --timeout 180s \
  --send-timeout 30s \
  --settle-timeout 30s \
  --stall-timeout 5s
```

Final pinpoint run:

```bash
cd backend
go run ./cmd/breakpoint \
  --start-clients 266 \
  --max-clients 270 \
  --step 1 \
  --timeout 220s \
  --send-timeout 30s \
  --settle-timeout 30s \
  --stall-timeout 5s
```

## What To Vary Next

If future runs want more insight instead of just one threshold, the next useful experiments are:

- vary `activity-triplets` to separate sustained event pressure from connection count
- vary `startup-food-count` to isolate startup payload pressure
- vary `interval` to separate message rate from concurrency
- compare in-process backend runs with a manually started backend via `--base-url`

Those experiments would help answer which dimension is actually dominating the breakpoint.
