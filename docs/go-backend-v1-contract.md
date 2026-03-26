# Go Backend V1 Contract

## Goal

Define the v1 contract between optional Rust simulation clients and the new Go backend before backend behavior is implemented.

This document is intentionally limited to:

- event schema
- sim identity
- aggregate metrics
- HTTP and WebSocket boundaries
- backpressure rules

It does not define durable persistence, auth, or production deployment details.

## Backend Scope

The Go service is a standalone backend under `backend/` that:

- accepts simulation events over WebSockets
- keeps live in-memory state for connected sims
- exposes HTTP JSON endpoints for current state and aggregate metrics
- serves a small built-in dashboard

The Rust simulation remains standalone by default and only connects when backend mode is enabled.

## Sim Identity

Each connected sim instance is identified by a client-provided `sim_id`.

V1 rules:

- `sim_id` must be unique for the lifetime of a process
- the Rust client should generate it at startup
- the server treats reconnects with the same `sim_id` as replacing the previous live connection
- the server stores only the current live state for a `sim_id`

Recommended hello payload fields:

- `sim_id`
- `sim_name`
- `source` such as `rust-bevy`
- `session_started_ms`
- `world_width`
- `world_height`
- `ant_count`
- `food_count`

Immediately after `sim_hello`, the client may send a startup snapshot message containing all currently loose food positions. This gives the backend an initial world view without waiting for later pickup/drop events.

## Transport

### WebSocket ingest

- Endpoint: `GET /ws/ingest`
- Client sends JSON messages only
- Server accepts one logical event envelope per message
- The first message from a client should be `sim_hello`
- The connection stays open for streaming simulation events and heartbeats

### HTTP endpoints

Planned v1 endpoints:

- `GET /healthz`
- `GET /api/sims`
- `GET /api/sims/{sim_id}`
- `GET /api/summary`
- `GET /`

`/` serves the minimal dashboard.

## Event Envelope

All ingest messages use a shared envelope:

```json
{
  "type": "food_pickup",
  "sim_id": "sim-123",
  "seq": 42,
  "timestamp_ms": 1735689600000,
  "payload": {}
}
```

Envelope fields:

- `type`: event kind
- `sim_id`: sim instance identity
- `seq`: client-local monotonic sequence number
- `timestamp_ms`: client event time in milliseconds
- `payload`: event-specific body

V1 ordering assumptions:

- sequence numbers are monotonic per `sim_id`
- the server does not attempt full replay or reordering
- if an old event arrives after a newer one for the same `sim_id`, the server may ignore it

## Event Types

### `sim_hello`

Sent once at connection start.

Payload:

```json
{
  "sim_name": "gatherers-local",
  "source": "rust-bevy",
  "session_started_ms": 1735689600000,
  "world_width": 1280,
  "world_height": 720,
  "ant_count": 26,
  "food_count": 80
}
```

### `sim_food_snapshot`

Sent once immediately after `sim_hello` to seed the backend with the loose food currently on the ground.

Payload:

```json
{
  "foods": [
    { "food_id": "food-1", "x": 412.5, "y": 218.0 },
    { "food_id": "food-2", "x": 398.0, "y": 227.5 }
  ]
}
```

Server effect:

- replace the sim's currently known loose-food positions with the provided snapshot
- do not count the snapshot itself as pickup/drop activity

### `sim_heartbeat`

Sent periodically while connected.

Payload:

```json
{
  "connected_ant_count": 26,
  "known_food_count": 80,
  "dropped_outbound_events": 0
}
```

### `food_pickup`

Sent when an ant picks up a loose food item.

Payload:

```json
{
  "ant_id": "ant-17",
  "food_id": "food-33",
  "x": 412.5,
  "y": 218.0,
  "direction_x": -0.91,
  "direction_y": 0.42,
  "frame": 884
}
```

Server effect:

- mark that food item as no longer loose on the ground
- update per-sim counters

### `food_drop`

Sent when an ant drops food.

Payload:

```json
{
  "ant_id": "ant-17",
  "food_id": "food-33",
  "x": 398.0,
  "y": 227.5,
  "direction_x": 0.37,
  "direction_y": -0.93,
  "frame": 885
}
```

Server effect:

- mark that food item as loose on the ground at the provided position
- update per-sim counters

### `ant_turn_move`

Sent when an ant changes direction. This event carries the ant position at the moment of the direction change.

Payload:

```json
{
  "ant_id": "ant-17",
  "x": 398.0,
  "y": 227.5,
  "direction_x": 0.37,
  "direction_y": -0.93,
  "frame": 885
}
```

V1 deliberately does not stream every ant position every frame by default.

### `sim_goodbye`

Optional best-effort disconnect event.

Payload:

```json
{
  "reason": "shutdown"
}
```

If it is absent, the server still considers the sim disconnected when the socket closes or heartbeats stop.

## Aggregate State

The backend keeps only in-memory live state for v1.

Per-sim state:

- sim metadata from `sim_hello`
- connection status
- last sequence number
- last heartbeat time
- pickup count
- drop count
- turn/move count
- current loose-food positions by `food_id`

Global aggregate state:

- currently connected sim count
- total live event rate
- aggregate loose-food positions across connected sims
- clustering metrics computed from loose-food positions

## Clustering Metrics

V1 should show whether food is collecting into piles or staying sparse.

Recommended metrics:

- `loose_food_count`
- `nearest_neighbor_mean_distance`
- `occupied_cell_count`
- `top_5_cells_share`

Definitions:

- use only loose food currently on the ground
- bucket food into fixed grid cells
- `top_5_cells_share` means the percentage of loose food contained in the 5 densest cells

These metrics are simple enough for v1 and easy to explain on the dashboard.

## Backpressure Rules

The Rust client must never block the simulation loop on network I/O.

Client-side v1 rule:

- buffer outbound events in a bounded queue
- if the queue is full, drop new outbound events and increment a local dropped-events counter

Server-side v1 rule:

- each connection is read independently
- decoded events are pushed into a bounded ingest path
- if the server cannot accept more events for a connection, it may close that connection rather than silently corrupt aggregate state

This keeps simulation runtime safe on the client side and keeps backend overload visible on the server side.

## Tentative Go Library Direction

Tentative v1 direction:

- `net/http` from the standard library for HTTP
- a small WebSocket library chosen during implementation review
- standard `testing` and `httptest` for tests

Library choice should stay open until the first architecture review checkpoint.

## Local Dev Expectations

Local v1 flow:

1. run the Go server
2. run fake Go clients first for standalone backend testing
3. later run the Rust sim with backend mode enabled
4. inspect `/api/*` endpoints and the built-in dashboard

## Non-Goals For V1

- database persistence
- auth or multi-user security
- historical replay
- exactly-once delivery
- cross-process clustering jobs
