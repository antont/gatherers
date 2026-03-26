# Go Backend Actor Notes

## Purpose

This note summarizes how the current Go backend relates to the actor model, and where the design could move further in that direction if clearer ownership or stronger robustness becomes important.

## Current Shape

The backend is not implemented as a full actor system, but one important part already behaves a lot like an actor:

- the aggregation worker is a single goroutine
- it is woken by messages/signals
- it owns the refresh policy
- it serializes expensive aggregate recomputation
- it publishes snapshots to dashboard subscribers

That is already actor-like in the sense of:

- one logical owner
- one serialized execution context
- message-driven wakeups
- no concurrent execution inside that component

## Where It Is Still Not Fully Actor-Like

The aggregation worker does not fully own all of the data it reasons about.

Instead:

- ingest handlers mutate shared store state directly
- the store is protected by `sync.RWMutex`
- the worker asks the store for copied snapshot inputs

So the current design is a hybrid:

- actor-like scheduling for analysis
- shared-memory locking for authoritative state

This is a reasonable Go design, but it means reasoning is split between:

- channel/message flow
- mutex-protected shared state

## What A More Actor-Like Design Would Mean

The most natural next step would not be “actors everywhere.” It would be to make the analysis side more explicitly actor-shaped.

That could mean:

- define a dedicated analysis goroutine as the sole owner of aggregate-analysis state
- send it explicit messages such as:
  - `MarkDirty`
  - `DashboardWatcherAdded`
  - `DashboardWatcherRemoved`
  - `APIRead`
  - `RefreshNow`
  - `Shutdown`
- keep refresh cadence, demand tracking, and cached snapshot ownership inside that goroutine
- remove some of the mutex-protected coordination fields around refresh state

In that version, the analysis component would look less like “a goroutine plus shared bookkeeping” and more like “an actor with a mailbox.”

## Possible Directions

### Option 1: Keep the store shared, make only analysis more actor-like

This is the smallest conceptual shift.

Shape:

- ingest still updates the shared store under mutex
- analysis goroutine fully owns:
  - dirty state
  - demand state
  - timer/cadence state
  - cached snapshot
  - dashboard broadcasts
- all analysis coordination happens via a typed command channel

Benefits:

- clearer ownership of refresh logic
- fewer refresh-related mutex interactions
- easier to reason about worker state transitions
- likely the best fit if the store remains shared

Costs:

- store locking still exists
- there is still a hybrid model rather than pure actors

### Option 2: Make the store itself actor-owned

This would be a much larger change.

Shape:

- one goroutine owns all mutable simulation state
- ingest handlers send event messages to that goroutine
- dashboard/API queries are answered by request/response messages
- mutable maps are no longer shared directly across goroutines

Benefits:

- much less mutex-based shared memory
- very strong ownership story
- easier to reason about some classes of races

Costs:

- more message plumbing
- request/response complexity for reads
- potentially harder integration with HTTP handlers
- may become less idiomatic than the current Go shape for a backend this size

For this project, this feels possible but probably larger than needed right now.

## Best Go Practice?

In Go, the current design is already quite normal:

- goroutines for concurrent work
- channels for signaling
- mutexes for shared maps and cached state

Go does not push developers toward a large actor framework in the way Akka-based systems do. The common Go style is usually:

- use mutexes where shared memory is simple and local
- use channels where ownership handoff or serialized workers are clearer

So “more actor-like” is possible and can be good, but it is not automatically more idiomatic or more robust unless it also makes ownership and state transitions meaningfully simpler.

## Recommended Interpretation For This Backend

The current backend can be viewed as:

- a shared-memory ingest/store layer
- plus an actor-like analysis layer

If this area evolves further, the most promising refinement is:

- keep ingest/store roughly as-is
- make the analysis worker more explicitly actor-shaped with a typed command channel and sole ownership of refresh/demand/cache state

That would likely improve clarity more than a full rewrite into actors for everything.

## Why This Might Be Worth It Later

The main reasons to push the analysis side further toward an actor are:

- fewer refresh-state mutex interactions
- clearer lifecycle and demand transitions
- easier logging and observability of worker decisions
- easier to test worker behavior as a message-driven state machine

The main reason not to do it yet is that the current design is already working, tested, and reasonably idiomatic for Go.

So the idea is less “the current code is wrong” and more:

- the current analysis worker is already actor-like
- there is room to make that actor boundary more explicit if the coordination logic grows
