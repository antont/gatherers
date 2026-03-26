# Go Backend

This directory contains the standalone Go backend for the Gatherers project.

V1 goals:

- ingest simulation events over WebSockets
- maintain live in-memory aggregate state
- expose JSON APIs and a small built-in dashboard

Current status:

- protocol and boundaries are defined in `../docs/go-backend-v1-contract.md`
- this package is only a non-behavioral skeleton so that strict TDD can start next

Expected package layout:

- `cmd/server` for the entrypoint
- `internal/config` for runtime configuration
- `internal/server` for app wiring
- `internal/state` for aggregate state logic
- `internal/ingest` for websocket ingest
- `internal/httpapi` for JSON and dashboard routes

The first meaningful backend behavior should be added only after a failing test is written and observed.
