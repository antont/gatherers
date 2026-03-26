# Rust Backend

This directory contains the standalone Rust backend built to compare against the Go backend under the same HTTP and WebSocket contract.

## Current scope

- same JSON event envelope as `docs/go-backend-v1-contract.md`
- `GET /healthz`
- `GET /api/summary`
- `GET /api/sims`
- `GET /`
- `GET /ws/ingest`
- `GET /ws/dashboard`
- in-memory state only
- cached snapshot behavior with eventual consistency

## Run locally

```bash
cd backend-rust
cargo run
```

Use a custom bind address:

```bash
cd backend-rust
GATHERERS_BACKEND_RUST_ADDR=127.0.0.1:18082 cargo run
```

## Run the Rust test suite

```bash
cd backend-rust
cargo test
```

## Reuse Go black-box tests against Rust

Start the Rust backend first:

```bash
cd backend-rust
GATHERERS_BACKEND_RUST_ADDR=127.0.0.1:18082 cargo run
```

Then point the Go tests at it:

```bash
cd backend
GATHERERS_BACKEND_BASE_URL=http://127.0.0.1:18082 \
go test ./internal/server -run TestSummaryReflectsEventsIngestedOverWebSocket -count=1
```

Examples that were verified unchanged against Rust:

```bash
cd backend
GATHERERS_BACKEND_BASE_URL=http://127.0.0.1:18082 \
go test ./internal/server -run TestDashboardPageBootstrapsRealtimeWebsocketUi -count=1
```

```bash
cd backend
GATHERERS_BACKEND_BASE_URL=http://127.0.0.1:18082 \
go test ./internal/server -run TestFakeClientsPopulatePerSimSummaries -count=1
```

```bash
cd backend
GATHERERS_BACKEND_BASE_URL=http://127.0.0.1:18082 \
go test ./internal/server -run TestStressHundredFakeClientsDeliversExactEventTotals -count=1
```

For tests that require a pristine initial snapshot, use a fresh Rust backend process per Go test invocation. The external-base-URL mode shares one backend process, so state persists across tests within that process.

## Breakpoint wrapper against Rust

```bash
cd backend
GATHERERS_BACKEND_BASE_URL=http://127.0.0.1:18082 \
GATHERERS_FIND_BREAKPOINT=1 \
GATHERERS_BREAKPOINT_START_CLIENTS=2 \
GATHERERS_BREAKPOINT_MAX_CLIENTS=4 \
GATHERERS_BREAKPOINT_STEP=2 \
GATHERERS_BREAKPOINT_ACTIVITY_TRIPLETS=4 \
GATHERERS_BREAKPOINT_SEND_TIMEOUT=3s \
GATHERERS_BREAKPOINT_SETTLE_TIMEOUT=3s \
GATHERERS_BREAKPOINT_TEST_TIMEOUT=10s \
go test ./internal/server -run TestFindBreakpointUnderRampLoad -count=1 -v
```

## Raw TCP later

Raw TCP ingest is intentionally deferred.

The current transport-independent seam is `src/ingest.rs`, where decoded envelopes are handed to shared state/update logic. When native-only TCP is added later, it should reuse the same envelope types and ingest core, changing only the framing layer.
