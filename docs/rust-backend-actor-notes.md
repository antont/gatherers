## Purpose

This note describes how the Rust backend's concurrency model ended up, why it looks the way it does, and where actor-style patterns or other refinements might still be worth considering.

## Current Architecture

The Rust backend is a hybrid of three concurrency styles:

### Shared parallel ingest

- `Store` uses sharded `RwLock<HashMap<String, SimState>>` for sim-level state
- each `SimState` owns a stable `Vec<FoodSlot>` for food positions
- `food_id` is a `usize` on the wire, used directly as a slot index -- no string parsing, no HashMap lookup per food event
- `sim_food_snapshot` allocates the slot array once; `food_pickup` and `food_drop` mutate slots in place
- shard-level RwLock contention is low because different sims hash to different shards

### Actor-like analytics worker

- one background task owns refresh timing, demand tracking, and analytics computation
- it copies lightweight input data from the store under a brief read lock, then releases the lock
- heavy analytics (nearest-neighbor distance, occupied cell count) runs in `spawn_blocking` off the Tokio runtime
- it publishes results into a shared analytics snapshot

### Broadcast publication

- live counters are derived incrementally on every ingest event using `EventOutcome` from the store
- every event broadcasts the current snapshot to dashboard websocket subscribers via `tokio::sync::broadcast`
- analytics snapshots are updated independently and merged into the broadcast view

## Why This Shape

The architecture evolved through measured experiments rather than upfront design:

1. **Ingest-analytics coupling was the first bottleneck.** The original Rust backend held a store read lock during expensive analytics, starving writers. Copying raw data under a brief lock and computing outside it was the fix.

2. **Async-inappropriate locking, global store contention, and CPU placement each contributed measurably.** The learning-first experiment showed that replacing `std::sync::Mutex` with atomics, sharding the store, and offloading compute to `spawn_blocking` each moved the breakpoint upward.

3. **Live-analytics separation was the biggest architectural win.** Splitting cheap per-event live counters from heavy background analytics moved the breakpoint from ~160 to ~2000 clients. The failure mode shifted from stale data to TCP connection exhaustion.

4. **Stable food slot arrays removed the last per-event allocation in the hot path.** Replacing `HashMap<String, FoodPosition>` with `Vec<FoodSlot>` indexed by `usize` eliminated hashing, string allocation, and insert/remove overhead from every food event.

## What The Options Looked Like

Before implementation, five concurrency approaches were considered. Here is how they relate to what was actually built:

### Option 1: Hybrid with small refinements -- this is what was built

The current backend is essentially this option, taken further than originally expected. The sharded store, actor-like worker, and separate live/analytics caches are all refinements of the original hybrid shape.

### Option 2: More explicitly actor-like analysis side

The analytics worker already behaves like an actor: it has serialized execution, owns its refresh policy, and publishes results. It could be made more explicit with a mailbox-style message channel, but the current `Notify` + `AtomicBool` coordination works well and the added plumbing is not currently justified.

### Option 3: Concurrent map for authoritative state

Less relevant now. The per-sim food storage is a stable slot array with direct integer indexing. The remaining `HashMap<String, SimState>` only changes on new sim connections, not on the hot path. Replacing it with `DashMap` or similar would change very little.

### Option 4: Immutable published snapshots

Partially adopted. The analytics snapshot is already computed from a copied input and published for readers. The live snapshot is updated incrementally under a write lock. Moving the live snapshot toward `Arc`-swap publication could reduce read-side contention further, but the current approach already sustains exact live counts through 1500+ concurrent clients.

### Option 5: Full actor ownership of all state

Not pursued. The backend's performance improved precisely because ingest writes proceed in parallel across shards. A single actor owning all state would serialize the hot path.

## What Might Still Be Worth Changing

### If read-side contention becomes measurable

Move the published live snapshot from `RwLock<CachedLiveSnapshot>` to an `Arc`-swap or `ArcSwap` pattern. Readers would clone an `Arc` cheaply instead of holding a read lock. This is a small change that would make the read side fully lock-free.

### If the analytics worker needs richer coordination

Introduce explicit message types (`EventApplied`, `DemandRegistered`, `Shutdown`) instead of the current `Notify` + `AtomicBool` pattern. This would make the worker easier to test as a state machine and easier to extend with new coordination triggers.

### If sim-level isolation matters more

Move each `SimState` into its own `RwLock` or even a per-sim actor, removing shard-level contention entirely. Currently the sharding is sufficient, but if per-sim write pressure becomes uneven this could help.

## Summary

The Rust backend's concurrency model is:

- **parallel shared ingest** with sharded locks and stable per-sim slot arrays
- **actor-like analytics** with demand-driven scheduling and off-runtime compute
- **incremental live broadcast** on every event, decoupled from heavy analytics

This was not designed upfront. It was arrived at through a sequence of measured bottleneck fixes, each committed with breakpoint evidence. The architecture works well for the current workload and has clear, small next steps if specific pressures emerge.
