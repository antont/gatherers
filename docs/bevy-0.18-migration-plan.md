# Gatherers: Port from Bevy 0.16 to 0.18

## Context

The Gatherers project is a 2D ant simulation built with Bevy 0.16 that runs natively and as WASM via Trunk. Bevy 0.18 introduced "cargo feature collections" -- a new idiomatic way to define 2D projects with `features = ["2d"]` instead of listing individual crate features. The goals are:

1. Migrate all APIs across two major versions (0.16 -> 0.17 -> 0.18)
2. Adopt the new 2D feature collection system
3. Produce a minimal WASM build
4. Make it an exemplary, idiomatic Bevy 0.18 project

## Migration Summary

The migration spans two Bevy releases. Key breaking changes that affect this project:

| Change | Introduced in | Files affected |
|--------|--------------|----------------|
| Events -> Messages (EventReader/Writer -> MessageReader/Writer) | 0.17 | collision.rs, main.rs, lib.rs, tests |
| BorderColor(color) -> BorderColor::all(color) | 0.17 | ui.rs |
| getrandom WASM config required | 0.17 | Cargo.toml, new .cargo/config.toml |
| remove_parent_in_place -> remove::\<ChildOf\> | 0.17-0.18 | main.rs, lib.rs |
| Cargo feature collections ("2d") | 0.18 | Cargo.toml |
| Input sources behind feature flags | 0.18 | Cargo.toml |
| Entity child commands renamed | 0.18 | main.rs, lib.rs |

## File-by-File Changes

### 1. `Cargo.toml`

- Bevy: `0.16` -> `0.18`
- Edition: `2021` -> `2024` (exemplary modern Rust)
- Features: Use mid-level 0.18 feature building blocks (2d minus unused -- see below)
- `derive_more`: `"0.99"` -> `{ version = "1.0", features = ["from"] }` (1.0 requires per-derive feature flags)
- `rand`: `"0.8"` -> `"0.9"` (required for edition 2024 compatibility; `gen` is reserved keyword)
- Add: `getrandom = { version = "0.3", features = ["wasm_js"] }` (WASM random support)
- Keep: `wasm-bindgen = "0.2"`, `log = "0.4"`

Features approach: **"2d" minus unused** -- use the mid-level building blocks of the `2d` collection, skipping `scene`, `audio`, and `picking` which this project doesn't use. Keep `webgl2` for broad browser compatibility.

```toml
[dependencies.bevy]
version = "0.18"
default-features = false
features = [
  "default_app",        # Core app infrastructure (logging, diagnostics, etc.)
  "default_platform",   # Platform support (windowing, keyboard, mouse)
  "2d_api",             # 2D types (Sprite, Camera2d, etc.)
  "2d_bevy_render",     # 2D rendering backend
  "ui",                 # Bevy UI (nodes, text, layout)
  "webgl2",             # WebGL2 for broad WASM browser support
]
```

This uses the new 0.18 feature collection building blocks idiomatically while keeping the WASM binary minimal. If any features are missing at compile time (e.g., diagnostics, keyboard input), we add them incrementally.

### 2. NEW: `.cargo/config.toml`

```toml
[target.wasm32-unknown-unknown]
rustflags = ['--cfg', 'getrandom_backend="wasm_js"']
```

### 3. `src/collision.rs`

- `#[derive(Debug, Event)]` -> `#[derive(Debug, Message)]` on `HitEvent`
- `app.add_event::<HitEvent<..>>()` -> `app.add_message::<HitEvent<..>>()`
- `EventWriter<HitEvent<A, B>>` -> `MessageWriter<HitEvent<A, B>>`
- `hits.write(...)` stays the same
- Removed unused `CollisionSystemLabel` struct and `ScheduleLabel` import

### 4. `src/main.rs`

- `EventReader<HitEvent<Food, Ant>>` -> `MessageReader<HitEvent<Food, Ant>>`
- `rand::thread_rng()` -> `rand::rng()` (rand 0.9)
- All `rng.gen_range(...)` -> `rng.random_range(...)` (rand 0.9)
- `commands.entity(carried_food).remove_parent_in_place()` -> `commands.entity(carried_food).remove::<ChildOf>()`
- Removed redundant required components from spawn bundles: `GlobalTransform::default()`, `Visibility::default()` (auto-populated by `Sprite` in 0.18)
- Removed dead code: `handle_window_resize` (empty placeholder)

### 5. `src/ui.rs`

- `BorderColor(Color::WHITE)` -> `BorderColor::all(Color::WHITE)`

### 6. `src/lib.rs`

Same changes as main.rs (this file duplicates components and systems for testing):
- `EventWriter` -> `MessageWriter`, `EventReader` -> `MessageReader`
- `rand::thread_rng()` -> `rand::rng()`, `gen_range` -> `random_range`
- `remove_parent_in_place()` -> `remove::<ChildOf>()`

### 7. `tests/simulation_tests.rs`

- `EventReader` -> `MessageReader` in collector systems
- `app.add_event::<HitEvent<Food, Ant>>()` -> `app.add_message::<HitEvent<Food, Ant>>()`

### 8. `src/boundary.rs`, `src/config.rs`, `src/spatial_index.rs`

No migration changes needed. These use stable APIs (Window queries, Resource, Vec2, Entity, HashMap).

### 9. `Trunk.toml`

No changes needed.

## Risks and Verification

**Things to test carefully:**
- `remove::<ChildOf>()` must preserve the child entity's world-space position (food should stay where it was dropped, not jump to parent's local origin). If it doesn't, we need to manually compute and set the world transform before detaching.
- Feature flags: with `default-features = false`, we may discover missing features at compile time. Will iterate.
- `MinimalPlugins` in tests may behave differently with `SingleThreadedExecutor` -- system execution order matters.
- WASM: `getrandom` config is essential; without it, `rand` calls will panic at runtime.

**Verification steps:**
1. `cargo check` -- compiles without errors
2. `cargo test` -- all 5 simulation tests pass
3. `cargo run` -- native build works, ants gather food, speed controls work
4. `trunk serve` -- WASM build works in browser
5. Check WASM binary size compared to previous build
