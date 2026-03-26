# Rust Backend Notes

## Purpose

This note explains the current Rust backend direction, why the stack was chosen, how it compares with the Go backend, and where the transport seam for future raw TCP now lives.

## Library Choices

The Rust parity backend currently uses:

- `tokio` for async runtime and background tasks
- `axum` for HTTP routes and WebSocket upgrades
- `serde` and `serde_json` for the shared JSON event envelope
- `tokio::sync::broadcast` for dashboard fanout
- `std::sync::{Mutex, RwLock}` for small in-memory shared state

### Why `axum` and `tokio`

This stack maps closely to the current Go backend:

- one process
- one HTTP router
- WebSocket ingest plus dashboard fanout
- in-memory state
- one cached snapshot path for readers

It also keeps the Rust backend easy to run as a standalone process and easy to test both:

- in-process from Rust integration tests
- externally from the existing Go black-box tests

## Shared Protocol

The Rust backend reuses the same logical protocol as the Go backend:

- same event envelope fields
- same event type names
- same HTTP route names
- same WebSocket route names

One important compatibility detail from implementation was that the Rust side must accept the looser payload shapes already used by the Go tests and fake clients. For example:

- `sim_hello` may contain only `sim_name`
- `food_pickup` may contain only `food_id`
- `food_drop` may omit ant and direction metadata

So the Rust backend now matches the protocol as actually exercised, not only the richer client payloads produced by `src/net.rs`.

## Transport-Independent Ingest Seam

The current seam for future transport expansion is:

- `backend-rust/src/ingest.rs`

That module exposes a transport-independent entrypoint that accepts a decoded `EventEnvelope` and applies it to shared state. Today it is called by the WebSocket handler. Later, native-only raw TCP should call the same ingest core after its own framing/decoding layer.

That means future TCP should change:

- transport framing

but not:

- event schema
- store mutation logic
- snapshot invalidation logic

## Raw TCP Status

Raw TCP is intentionally deferred.

Reasons:

- WebSocket parity is enough to reuse the strongest existing Go tests now.
- The browser dashboard and existing Rust client already fit naturally with WebSockets.
- Adding raw TCP too early would mix transport experimentation with parity work and make comparison harder.

Planned direction for later:

- native-only TCP mode
- same JSON envelope
- framing-only difference from WebSocket
- same `handle_ingest_event` path after decode

## Comparison Status

### Go tests reused unchanged against Rust

These external black-box tests were verified against the Rust backend by setting `GATHERERS_BACKEND_BASE_URL`:

- `TestSummaryReflectsEventsIngestedOverWebSocket`
- `TestDashboardPageBootstrapsRealtimeWebsocketUi`
- `TestDashboardWebsocketReceivesUpdatedSnapshotAfterIngest`
- `TestDashboardWebsocketStartsWithCachedSnapshotThenCatchesUp`
- `TestFakeClientsPopulatePerSimSummaries`
- `TestStressHundredFakeClientsDeliversExactEventTotals`
- `TestFindBreakpointUnderRampLoad` with a small ramp config

### What required adaptation

Two kinds of adaptation were needed:

1. Test harness adaptation:
   - `backend/internal/server/server_api_test.go` had to move onto the shared `newTestTarget` base-URL abstraction so those tests can target an external backend process.

2. Rust contract adaptation:
   - the Rust payload models had to accept the lighter request bodies already used by the Go tests and fake clients.

No Go test logic had to be rewritten for the Rust backend after the base-URL refactor.

## Current Comparison Result

### Go backend

The Go backend already has a recorded local breakpoint note in `docs/go-backend-breakpoint-findings.md`, including a workload that reached a timeout-bound breakpoint around `269` clients on one machine.

### Rust backend

The Rust backend has currently been verified at two levels:

- focused black-box Go tests pass against it
- the Go exact-count stress test with `100` fake clients passes against it
- the Go breakpoint wrapper passes in a small smoke configuration up to `4` clients

That means the Rust backend is already compatible enough for apples-to-apples harness reuse, but it does **not** yet have a heavy breakpoint characterization comparable to the Go backend note.

## Remaining Gaps Before Raw TCP

- run the same larger breakpoint search against Rust that was already recorded for Go
- decide whether the Rust backend should copy the Go adaptive cadence exactly or only semantically
- decide whether external Go test reuse should eventually spawn fresh Rust processes automatically for zero-state-sensitive tests
- add a documented native-only TCP framing choice
