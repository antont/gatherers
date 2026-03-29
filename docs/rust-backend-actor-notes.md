## Purpose

This note describes the final Rust backend concurrency model after the packed food-slot refactor and the later fix that restored the cheap live/expensive analytics split.

The main point is: the backend is no longer a sharded shared-state store. It is now a hybrid of per-sim ownership, cheap published live state, and one detached analytics worker.

## Current Architecture

The Rust backend is a hybrid of three concurrency styles:

### Per-sim owned ingest state

- each sim connection uses an `Arc<SimHandle>`
- the ingest WebSocket task caches that handle after the first event
- steady-state events do not go through a shared map lookup
- `SimHandle` owns per-sim counters as atomics
- `SimHandle` owns a fixed food-slot array installed once from `sim_food_snapshot`
- each food slot is one packed `AtomicU64`
- `food_id` is a `usize` on the wire and is used directly as the slot index

That means the hot path for a food event is:

- update one packed food slot with one atomic store
- update a few per-sim atomic counters
- return the new per-sim live values needed by the app-layer live cache

### Cheap published live state

- app state owns a small `live_snapshot` cache behind `RwLock<LiveCacheState>`
- every event updates only the touched sim row plus aggregate live counters
- this work is O(1) per event
- dashboard pushes read from the published live cache, not from a whole-registry scan

This is the crucial split that was briefly broken and then restored:

- immediate live data stays cheap
- heavy analytics stays detached

### Actor-like detached analytics worker

- one background task owns refresh timing, demand tracking, and analytics publication
- it wakes only when there is demand and analytics work is marked dirty
- it scans packed atomic food slots through the registry
- heavy analytics (nearest-neighbor distance, occupied cell count) runs in `spawn_blocking`
- it publishes results into a separate analytics snapshot

The analytics worker is actor-like because it has one serialized control flow that owns:

- refresh cadence
- demand state
- staleness transitions
- analytics publication

## What The Shared Registry Still Does

There is still one shared registry:

- `RwLock<HashMap<String, Arc<SimHandle>>>`

But its role is now much smaller than before.

It is used for:

- first creation of a sim handle
- cloning all handles for analytics scans
- occasional read-side enumeration

It is **not** on the steady-state ingest hot path after a connection has cached its handle.

## Why This Shape

The architecture evolved through measured experiments rather than upfront design:

1. **Ingest and analytics had to be decoupled first.**
   Heavy neighbor calculations originally blocked state access and made live updates stale.

2. **The per-sim food representation had to become cheaper and simpler.**
   Replacing map-based food storage with stable slot storage removed hashing and per-event structure churn.

3. **The slot update itself had to become atomic as one unit.**
   Packing `(x, y, present/absent)` into one `AtomicU64` avoided torn mixed-field reads such as new `x` with old `y`.

4. **The live/analytics split had to be preserved even after the storage refactor.**
   A temporary regression happened when watched live events rebuilt whole snapshots. The final fix restored the old idea:
   cheap immediate live cache updates, expensive detached analytics recomputation.

## What Was Actually Built

The final design is best described as:

- **per-sim owned atomic ingest state**
- **cheap app-layer live publication**
- **one actor-like detached analytics worker**

This is not a pure actor model and not a pure shared-memory design.

It is a practical hybrid:

- per-sim writes are direct and cheap
- live reads come from a small published cache
- analytics reads come from the authoritative atomic slots
- expensive math is completely off the ingest path

## What Changed Relative To Earlier Notes

Earlier versions of this note talked about:

- sharded `RwLock<HashMap<String, SimState>>`
- `SimState` owning a plain `Vec<FoodSlot>`
- event-path shard write locks

That is no longer the design.

The important updated facts are:

- there is no shard lock on steady-state event processing
- the sim connection writes through its cached `Arc<SimHandle>`
- food updates are one packed atomic write
- live state is published through a small cache
- analytics remains detached and demand-driven

## What Might Still Be Worth Changing

### If read-side contention becomes measurable

Move the published live snapshot from `RwLock<CachedLiveSnapshot>` to an `Arc`-swap style publication so readers can grab the latest live snapshot without taking a read lock.

### If the analytics worker needs richer coordination

Replace the current `Notify` + `AtomicBool` style with explicit message types such as:

- `EventApplied`
- `DashboardWatcherAdded`
- `ApiDemand`
- `Shutdown`

That would make the worker more explicitly actor-shaped and easier to test as a state machine.

### If sim lifetime management becomes more important

Push more lifecycle rules into the per-sim handle layer, especially around disconnect cleanup and any future per-sim background tasks.

## Summary

The Rust backend's concurrency model is now:

- **per-sim owned ingest** through cached `Arc<SimHandle>`
- **packed atomic food-slot storage** for one-write/one-read food state
- **cheap published live cache** updated immediately on every event
- **actor-like detached analytics** running only in the background

This preserves the important architectural rule learned earlier:

- update cheap live data immediately
- keep expensive global analysis off the event path
