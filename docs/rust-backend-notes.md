# Rust Backend Notes

## Purpose

This note explains the current Rust backend direction, why the stack was chosen, how it compares with the Go backend, where the transport seam for future raw TCP now lives, and how the snapshot path was decoupled from ingest after reproducing the first heavy-load failure mode.

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

## Snapshot Decoupling

The first Rust parity version repeated the same architectural mistake the Go backend originally had in milder form:

- ingest writes took the store write lock
- demand-driven refresh took the store read lock
- while still holding that read lock, refresh computed expensive derived metrics such as nearest-neighbor distance

That meant refresh could block writers for far longer than the cheap raw-state copy itself required.

The current snapshot path now matches the fixed Go shape more closely:

1. ingest mutates raw state and marks cached data dirty
2. a demand-triggered refresh task takes the store read lock only long enough to copy `DashboardSnapshotData`
3. the read lock is released
4. expensive derived metrics are computed from that copied data
5. the cached snapshot is replaced and broadcast to dashboard watchers

This keeps the public HTTP and WebSocket contract the same, while removing the long lock hold that previously starved writes under load.

## Failure Reproduction And Evidence

Two new regressions were added specifically around this issue:

- `backend-rust/src/app.rs`: `refresh_does_not_hold_store_lock_long_enough_to_starve_writes`
- `backend-rust/tests/ingest_decoupling_stress.rs`: `heavy_ingest_with_polling_keeps_api_sims_responsive_and_exact`

Before the refactor, the targeted lock test showed a single refresh blocking writes for roughly half a second on a large snapshot. The first heavy-load regression also failed while `/api/sims` polling was active.

The shared Go breakpoint harness was then rerun against a fresh Rust backend process. That changed the observed behavior from an API timeout-style collapse at the first heavy step to:

- exact through `100` clients in the reused Go exact-count stress test
- exact through `100` clients in the reused Go breakpoint regression ramp
- first observed breakpoint moving up to `105` clients, and presenting as an exact-count mismatch rather than `/api/sims` timing out

That does not mean the Rust backend has no breakpoint. It means the specific ingest-vs-refresh lock coupling that originally caused request starvation was removed, and the next remaining limit now appears as ordinary throughput capacity instead.

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

These external black-box tests were verified against fresh Rust backend instances by setting `GATHERERS_BACKEND_BASE_URL`:

- `TestDashboardWebsocketStartsWithCachedSnapshotThenCatchesUp`
- `TestSummaryReadStartsDemandAndEventuallyRefreshesCachedSnapshot`
- `TestStressHundredFakeClientsDeliversExactEventTotals`
- `TestExternalBackendRampLoadKeepsAPISimsResponsive`

### What required adaptation

Two kinds of adaptation were needed:

1. Test harness adaptation:
   - `backend/internal/server/server_api_test.go` had to move onto the shared `newTestTarget` base-URL abstraction so those tests can target an external backend process.

2. Rust contract adaptation:
   - the Rust payload models had to accept the lighter request bodies already used by the Go tests and fake clients.

No Go test logic had to be rewritten for the Rust backend after the base-URL refactor.

## Stable Food Slot Storage

The authoritative per-sim food representation was refactored from a dynamic `HashMap<String, FoodPosition>` to a stable `Vec<FoodSlot>` indexed by slot position.

Key properties:

- `sim_food_snapshot` establishes a fixed-size slot array; the order of foods in the snapshot payload determines slot identity
- `food_id` in `food_pickup` and `food_drop` events is a stringified slot index (e.g. `"0"`, `"17"`)
- `food_pickup` clears a slot in place without removing it
- `food_drop` restores a slot with a new position
- malformed or out-of-range food IDs are silently ignored
- `loose_food_count` is maintained as an explicit counter, not derived from container length
- analytics scans the slot array directly, collecting positions from present slots

This eliminates per-event HashMap allocation, improves data locality for analytics scans, and removes the possibility of count drift from HashMap insert/remove semantics.

The Go loadsim was updated to emit stringified-index food IDs so the breakpoint harness is compatible with both backends.

## Current Comparison Result

### Go backend

The Go backend already has a recorded local breakpoint note in `docs/go-backend-breakpoint-findings.md`, including a workload that reached a timeout-bound breakpoint around `269` clients on one machine.

### Rust backend

The Rust backend has extensive post-optimization evidence:

- full Rust test suite (27 tests) passes locally
- reused Go lazy-refresh API and dashboard tests pass against fresh Rust backend instances
- the ingest decoupling stress test verifies exact counts through 40 concurrent clients
- the Go breakpoint harness proves exact live totals through `1500` clients
- first observed failure at `1750` clients is purely connection-level (WebSocket dial timeouts), not data staleness or mismatch

## Remaining Gaps Before Raw TCP

- decide whether external Go test reuse should eventually spawn fresh Rust processes automatically for zero-state-sensitive tests
- add a documented native-only TCP framing choice
