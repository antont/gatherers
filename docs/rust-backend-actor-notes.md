## Purpose

This note summarizes how the current Rust backend relates to actor-style ownership and other Rust concurrency options, and which direction looks most promising if this backend evolves further.

The goal is not to argue that the current design is wrong. The goal is to clarify which next-step simplifications would actually fit this codebase well.

## Current Shape

The Rust backend today is a hybrid:

- ingest mutates authoritative shared state in `Store`
- `Store` uses sharded `RwLock<HashMap<String, SimState>>` for concurrent writes by sim
- each `SimState` owns a stable `Vec<FoodSlot>` indexed by stringified slot ID, eliminating per-event HashMap allocation for food tracking
- app state keeps separately published live and analytics snapshots
- one demand-driven refresh worker serializes heavy analytics recomputation
- dashboard updates are broadcast through one shared fanout channel

That means the system already has one strongly actor-like component:

- the analytics refresh worker has one serialized execution context
- it owns refresh timing and demand handling
- it decides when heavy analytics are recomputed
- it publishes analytics-derived updates

But the backend is not a pure actor system, because the authoritative ingest state is still shared-memory state protected by locks.

So, just like the Go backend, the current Rust backend is best described as:

- shared-memory ingest/store ownership
- plus an actor-like analysis worker

## What Rust Changes About The Discussion

Rust changes the trade-offs somewhat compared with Go.

In Go, the main question was whether the analysis side should be made more explicitly actor-shaped for clarity.

In Rust, there is an additional strong option:

- publish immutable snapshots for readers

That option is especially attractive in Rust because immutable shared data wrapped in `Arc` is ergonomic and safe, and because the current problem is largely about separating:

- cheap hot-path ingest mutation
- from safe concurrent reads
- from heavy background analytics

So the Rust design space is not just:

- mutexes vs actors

It is more like:

- shared mutable state with locks
- actor ownership
- concurrent map structures
- immutable published snapshots

## Option 1: Keep The Current Hybrid, With Small Refinements

Shape:

- keep the sharded `Store` as authoritative mutable state
- keep the analytics worker actor-like
- keep live and analytics caches as separately published derived state
- continue to use `RwLock` around mutable shared structures

Benefits:

- smallest change from current code
- already tested and understood
- preserves the successful performance result where live counters stay exact through high load
- keeps the code easy to map to the Go version

Costs:

- read locks still exist on the published cache objects
- ownership remains split between shared state and one worker
- future coordination logic may become harder to reason about if more caches or workers are added

This is a good default if the goal is stability and parity rather than deeper architectural change.

## Option 2: Make Only The Analysis Side More Explicitly Actor-Like

Shape:

- keep ingest writing to the shared store
- make the analytics/live publication side more explicitly mailbox-driven
- let one task own:
  - demand state
  - refresh cadence
  - stale/fresh transitions
  - published analytics snapshot
  - possibly published live snapshot too

Possible message types could look like:

- `EventApplied`
- `DashboardWatcherAdded`
- `DashboardWatcherRemoved`
- `ApiDemand`
- `RefreshNow`
- `Shutdown`

Benefits:

- clearer ownership of refresh policy and publication behavior
- easier to test as a state machine
- easier to log and reason about transitions
- removes some coordination from shared mutable fields

Costs:

- authoritative ingest state is still shared-memory state
- adds channel/message plumbing
- does not eliminate store locking

This is the most direct Rust equivalent of the “make analysis more actor-like” idea from the Go note.

## Option 3: Use A Concurrent Map For Authoritative State

Examples in Rust would include designs based on structures such as:

- `DashMap`
- `scc::HashMap`
- similar fine-grained concurrent containers

Shape:

- replace the sharded `Vec<RwLock<HashMap<...>>>`
- let the concurrent data structure internalize some of the synchronization

Benefits:

- less custom sharding code
- concurrent reads and writes become more direct at the container layer
- may simplify some hot-path mutation code

Costs:

- the synchronization does not disappear; it just moves inside the data structure
- whole-world analytics still need a consistent materialized read view
- iteration semantics and snapshot consistency become more subtle
- it adds a dependency and a different mental model without clearly solving the publish/snapshot question

For this backend, this is now less compelling than before, because the per-sim food storage has already moved from `HashMap<String, FoodPosition>` to a stable `Vec<FoodSlot>`. The remaining HashMap is only the sim-level index (`String -> SimState`), which changes rarely (only on new sim connections). The hot-path mutation is now direct slot writes within a stable array, which is already well-suited to the sharded RwLock model.

## Option 4: Publish Immutable Snapshots For Readers

This is the most Rust-specific and, in many ways, the most interesting option.

Shape:

- keep one authoritative mutable store for ingest writes
- build immutable live snapshots and immutable analytics snapshots from that state
- publish them by swapping an `Arc` to the latest snapshot
- readers use the latest published immutable snapshot without taking a traditional read lock on the payload itself

This can be implemented with patterns such as:

- `Arc` plus swap behind a small lock
- `ArcSwap`
- similar read-copy-update style publication

Benefits:

- readers always see a coherent view
- no reader can observe partially mutated structures
- read-side access becomes extremely cheap
- it matches the current architectural split very naturally:
  - mutable ingest state
  - separately published live view
  - separately published analytics view

Costs:

- publishing requires cloning or rebuilding snapshot data
- this is best for published views, not as the raw ingest store itself
- writes still need synchronization in the authoritative store

This is not a pure actor model, but it solves the exact thing the backend cares about most:

- ingest should keep moving
- readers should always see a valid consistent view
- heavy analytics should be independently publishable

## Option 5: Make The Whole Store Actor-Owned

Shape:

- one task owns all mutable simulation state
- ingest handlers send event messages to that task
- API/dashboard reads become request/response messages or subscribe to published snapshots
- shared mutable maps mostly disappear from the application layer

Benefits:

- very strong ownership story
- no shared mutable store across tasks
- some classes of race simply disappear

Costs:

- much larger rewrite
- more queueing and request/response plumbing
- higher risk of creating one central bottleneck if not designed carefully
- less useful if the actual workload benefits from parallel writes across sims

For this backend, this feels too large and too constraining for the current goals.

The backend’s load story improved precisely because writes can proceed across sharded state while analytics is detached. A single fully actor-owned store would simplify ownership, but it would also serialize much more of the hot path.

## Best Fit For This Backend

The best next-step Rust direction is not “actors everywhere.”

It is:

- keep the authoritative ingest store shared and parallel-friendly
- keep the analytics worker actor-like
- if the read/publication side needs further cleanup, move published snapshots toward immutable snapshot swapping rather than deeper shared read locking

In other words, the strongest Rust-specific refinement is:

- **shared mutable raw state**
- **actor-like analysis scheduling**
- **immutable published views for readers**

That combination fits the current backend especially well because it preserves the architecture that already worked well in measurement:

- ingest stays cheap
- live counters stay exact and prompt
- analytics can lag independently
- readers can be made simpler and safer

## Why This Looks Better Than The Alternatives

Why not full actor ownership of all state?

- it is a much larger rewrite
- it would likely serialize too much of the ingest path
- it solves more problems than this backend currently has

Why not jump to a concurrent-map crate first?

- the per-sim food storage is already a stable slot array, not a HashMap
- the remaining sim-level HashMap changes rarely (only on new connections)
- the hard part here is coherent publication of views, not only concurrent insertion/removal

Why not just stay exactly as-is forever?

- that is a valid choice for now
- but if the code grows, immutable published snapshots would likely make the read side easier to reason about than more shared read locks and more mutable cache objects

## Recommended Interpretation

The Rust backend should currently be viewed as:

- a parallel shared-memory ingest/store layer
- plus an actor-like analytics/publication layer

If it evolves further, the most promising refinement is:

- **not** a full actor rewrite
- **not** “find a more magical concurrent map”
- **but** making the published read side more explicitly immutable and snapshot-based

That would likely improve clarity and robustness more than pushing the entire backend into a pure actor model.
