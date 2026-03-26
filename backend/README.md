# Go Backend

This directory contains the standalone Go backend for the Gatherers project.

V1 goals:

- ingest simulation events over WebSockets
- maintain live in-memory aggregate state
- expose JSON APIs and a small built-in dashboard

Protocol and scope are defined in `../docs/go-backend-v1-contract.md`.

## Layout

- `cmd/server` for the entrypoint
- `cmd/fakeclients` for long-lived manual load generation against a live backend
- `internal/config` for runtime configuration
- `internal/loadsim` for reusable fake-client streams and runners shared by tests and commands
- `internal/server` for HTTP and WebSocket wiring
- `internal/state` for aggregate state logic
- `internal/ingest` reserved for further ingest-specific code
- `internal/httpapi` reserved for further HTTP-specific code
- `web` reserved for dashboard assets if the HTML grows beyond inline rendering

## Local development

Run the backend:

```bash
cd backend
go run ./cmd/server
```

Run the backend test suite:

```bash
cd backend
go test ./...
```

Fast CI-safe backend checks:

```bash
cd backend
go test ./...
go test -race ./internal/server -run TestStressHundredFakeClients -count=1
```

Run the race-checked stress slice:

```bash
cd backend
go test -race ./internal/server -run TestStressHundredFakeClients -count=1
```

Target a manually running backend instead of `httptest`:

```bash
cd backend
GATHERERS_BACKEND_BASE_URL=http://127.0.0.1:18080 \
go test -race ./internal/server -run TestStressHundredFakeClients -count=1
```

The fake-client integration test supports the same override:

```bash
cd backend
GATHERERS_BACKEND_BASE_URL=http://127.0.0.1:18080 \
go test ./internal/server -run TestFakeClientsPopulatePerSimSummaries -count=1
```

Run the opt-in sustained load coverage:

```bash
cd backend
GATHERERS_RUN_LONG_STRESS=1 \
go test ./internal/server -run TestLongStressDashboardReflectsSustainedLoad -count=1
```

## Realtime dashboard demo

Start the backend on a known port:

```bash
cd backend
GATHERERS_BACKEND_ADDR=:18080 go run ./cmd/server
```

Then open [`http://127.0.0.1:18080`](http://127.0.0.1:18080) in a browser. The page now subscribes to `/ws/dashboard` and updates live without reload.

Drive realistic long-lived fake traffic from a second terminal:

```bash
cd backend
go run ./cmd/fakeclients \
  --base-url http://127.0.0.1:18080 \
  --clients 100 \
  --duration 20s \
  --interval 100ms \
  --startup-food-count 80 \
  --seed 1 \
  --sim-id-prefix demo
```

Each fake client now sends:

- `sim_hello`
- `sim_food_snapshot` with its startup loose-food positions
- then ongoing mixed `food_pickup`, `ant_turn_move`, and `food_drop` events

The dashboard should show:

- the connected sim count climbing toward the requested client count
- loose food and clustering metrics changing over time
- the per-sim table updating while load is still running

For a smaller local smoke demo, lower `--clients` or shorten `--duration`.

## Running the Rust sim against the backend

Start the Go backend first:

```bash
cd backend
go run ./cmd/server
```

Then, in the repo root, enable backend mode for the Rust sim:

```bash
GATHERERS_BACKEND_WS_URL=ws://127.0.0.1:8080/ws/ingest cargo run
```

When backend mode is enabled, the Rust sim sends:

- `sim_hello` once at startup
- `sim_food_snapshot` immediately after hello with all current loose-food positions
- collision-driven pickup/drop/turn events after that

If `GATHERERS_BACKEND_WS_URL` is unset, the Bevy sim runs standalone and does not try to connect.

## Current architecture choices

- Go HTTP stack: `net/http`
- Go WebSocket stack: `github.com/coder/websocket`
- Rust WebSocket client: `ewebsock`
- Backend state: in-memory only for v1, protected by `sync.RWMutex`

This keeps the stack small and idiomatic while still supporting native and WASM Rust builds.

## Hosting split

The static sim frontend and the Go backend are deployed separately:

- static/web sim: existing GCS-based flow from the repo root
- Go backend: separate service deployment such as Cloud Run

The backend is intentionally not folded into the static deploy scripts.

## TDD workflow

When extending backend behavior, prefer small RED/GREEN commits:

1. add a meaningful failing test
2. run it and capture proof of RED
3. commit the failing test state
4. implement the smallest fix
5. commit GREEN once the targeted tests pass

The existing commit history on this branch follows that pattern for:

- aggregation state
- API/WebSocket behavior
- dashboard behavior
- fake-client integration
- concurrent stress handling
- realtime dashboard updates
- sustained opt-in load coverage
