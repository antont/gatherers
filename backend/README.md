# Go Backend

This directory contains the standalone Go backend for the Gatherers project.

V1 goals:

- ingest simulation events over WebSockets
- maintain live in-memory aggregate state
- expose JSON APIs and a small built-in dashboard

Protocol and scope are defined in `../docs/go-backend-v1-contract.md`.

## Layout

- `cmd/server` for the entrypoint
- `internal/config` for runtime configuration
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
