# BevyMass: Bevy-Scheduled ECS Simulation in Unreal

## Overview

The BevyMass variant runs the gatherers simulation as Rust `#[mass_system]` functions inside Unreal's Mass Entity framework, with an optional **Bevy ECS scheduling layer** that dispatches all systems in a single coordinated call. This gives Bevy-style resource-based parallelism analysis while keeping Unreal's entity storage, chunk iteration, and physics/rendering.

Two scheduling modes are available, controlled by `bUseBevyScheduling` on the subsystem:

| Mode | Scheduling | Collision Detection | Use case |
|------|-----------|-------------------|----------|
| **Bevy** (default) | Rust Bevy `Schedule` with `MassSystemStage` ordering | Rust system with C++ spatial query callback | Full Bevy-style dispatch |
| **Direct** | C++ pipeline calls each Rust system per-chunk | C++ `UGatherersBevyMassCollisionProcessor` | Fallback / comparison |

## Architecture

```
Level starts → AGatherersSimActivator::BeginPlay()
  → UGatherersBevyMassSubsystem::InitializeSimulation(ants, food, bounds, seed)
    → Spawns Mass entities (AntFragment + EncounterFragment, FoodFragment)
    → Creates ISM visualizers

Per frame → Tick() → RunSimulationProcessorStep(dt)
  → EnsureProcessorPipelines() (once)
    → Creates URustMassDynamicProcessor per #[mass_system]
    → If Bevy mode:
        → All processors set to cache-only (cache chunk pointers, skip Rust call)
        → URustMassScheduleCoordinator created with spatial query callback
    → If Direct mode:
        → Processors call Rust directly per-chunk
        → C++ collision processor in pipeline

  → Pipeline Execute:
    → Each processor caches its chunk data
    → Coordinator gathers all cached chunks into MassFrameDispatchData
    → Calls mass_frame_dispatch() → Rust Bevy schedule runs all systems
```

### Bevy Schedule Layout

The Bevy schedule uses `MassSystemStage(u32)` for explicit ordering:

| Stage | System | Type | Reads | Writes |
|-------|--------|------|-------|--------|
| 0 | `ant_movement` | `#[mass_system]` | — | `AntFragment` |
| 1 | `ant_collision_prepass` | Hand-written Bevy system | `AntFragment`, `MassSpatialQueryCallback` | `AntEncounterFragment` |
| 2 | `ant_food_decision` | `#[mass_system]` | `AntEncounterFragment` | `AntFragment`, `FoodFragment` |
| 4 | `ant_cooldown` | `#[mass_system]` | — | `AntFragment` |
| 6 | `ant_boundary_reflect` | `#[mass_system]` | — | `AntFragment` |

Systems at different stages run sequentially. Future entity types with independent fragment types could parallelize automatically within a stage.

### Data Flow (Bevy Mode)

```
C++ Pipeline Execute (sequential processors):
  ant_movement processor   → caches chunk pointers (cache-only)
  ant_food_decision proc   → caches chunk pointers (cache-only)
  ant_cooldown processor   → caches chunk pointers (cache-only)
  ant_boundary_reflect     → caches chunk pointers (cache-only)
  ─────────────────────────────────────────────────
  URustMassScheduleCoordinator::Execute()
    → Gathers all cached chunks into MassFrameDispatchData
    → Sets spatial_query_fn callback + pickup_radius
    → Calls mass_frame_dispatch(&data)

Rust mass_frame_dispatch():
  → Updates MassSpatialQueryCallback resource
  → Clears all MassChunks<T> resources
  → Populates MassChunks<T> from each system's chunk batch
  → Runs Bevy Schedule:
      Stage 0: ant_movement_bevy
        → Iterates MassChunks<AntFragment>, calls ant_movement()
      Stage 1: ant_collision_prepass_bevy
        → Iterates MassChunks<AntFragment> + MassChunks<AntEncounterFragment>
        → Per ant: calls C++ spatial_query_fn(prev_pos, curr_pos, radius)
        → C++ does UE SweepMulti / OverlapSphere, returns nearest food
      Stage 2: ant_food_decision_bevy
        → Iterates primary chunks + global MassChunks<FoodFragment>
        → Pickup/drop logic from pre-computed encounters
      Stage 4: ant_cooldown_bevy
      Stage 6: ant_boundary_reflect_bevy
```

### Spatial Query Callback

The collision pre-pass is a Rust system that calls back into C++ for Unreal Engine physics queries:

```rust
// Rust side (gatherers-bevy-mass/src/systems.rs)
pub fn ant_collision_prepass_impl(
    ants: &MassQueryRef<AntFragment>,
    encounters: &mut MassQueryMut<AntEncounterFragment>,
    query_fn: Option<MassSpatialQueryFn>,
    pickup_radius: f32,
) {
    for (ant, enc) in ants.iter().zip(encounters.iter_mut()) {
        enc.has_encounter = false;
        if let Some(callback) = query_fn {
            let mut result = MassSpatialQueryResult { .. };
            unsafe { callback(prev_pos, curr_pos, radius, &mut result) };
            if result.has_encounter { *enc = result; }
        }
    }
}
```

```cpp
// C++ side (GatherersBevyMassSubsystem.cpp)
// Static callback set before each frame's dispatch
uint32_t SpatialQueryCallback(const double* PrevPos, const double* CurrPos,
                               float Radius, MassSpatialQueryResult* Out) {
    // Stationary: FoodISM->GetInstancesOverlappingSphere()
    // Moving: World->SweepMultiByChannel()
    // Filters: only loose food, finds nearest by distance
}
```

This pattern — **Rust for ECS logic, C++ for engine services** — mirrors how Bevy systems receive events from physics plugins like `bevy_rapier`.

## Key Types

### FFI Layer (`unreal-ffi/src/lib.rs` ↔ `Bindings.h`)

| Rust | C++ | Size | Purpose |
|------|-----|------|---------|
| `MassFrameDispatchData` | `MassFrameDispatchData` | 32 bytes | dt + system batches + spatial callback |
| `MassSystemChunkBatch` | `MassSystemChunkBatch` | 16 bytes | One system's cached chunk pointers |
| `MassSpatialQueryResult` | `MassSpatialQueryResult` | 40 bytes | Collision query result per ant |
| `MassSpatialQueryFn` | `MassSpatialQueryFn` | fn ptr | Callback: Rust → C++ physics |

### Bevy Resources (`unreal-api/src/mass.rs`)

| Resource | Purpose |
|----------|---------|
| `MassChunks<T>` | Per-fragment-type cached chunk pointers. Bevy scheduler uses resource access for parallelism. |
| `MassDeltaTime` | Frame delta time |
| `MassSpatialQueryCallback` | Optional C++ physics callback + pickup radius |
| `MassSystemStage(u32)` | `SystemSet` for explicit stage ordering |

### Registration

Each `#[mass_system]` function generates two registrations via `inventory`:

1. **`MassSystemRegistration`** — `extern "C"` wrapper for direct C++ dispatch
2. **`MassBevySystemRegistration`** — Bevy wrapper with `init_resources`, `clear_resources`, `populate_resources`, `add_to_schedule`

The proc macro generates type-erased closures that know their fragment types at compile time, resolving the problem of populating generic `MassChunks<T>` from untyped FFI data.

## Query Types

The `#[mass_system]` macro maps user-facing query syntax to concrete types:

| User writes | Generated type | C++ access mode | Scope |
|---|---|---|---|
| `MassQuery<&T>` | `MassQueryRef<T>` | ReadOnly | Per-chunk |
| `MassQuery<&mut T>` | `MassQueryMut<T>` | ReadWrite | Per-chunk |
| `MassQueryAll<&T>` | `MassQueryAllRef<T>` | ReadOnly | All entities (zero-copy chunked) |
| `MassQueryAll<&mut T>` | `MassQueryAllMut<T>` | ReadWrite | All entities (zero-copy chunked) |

Global queries (`MassQueryAll`) use zero-copy access into Mass Entity chunk memory via `MassGlobalFragmentChunks` descriptors — no copying of fragment data.

## Files

### Rust

| File | Role |
|------|------|
| `unreal-ffi/src/lib.rs` | FFI type definitions shared with C++ |
| `unreal-api/src/mass.rs` | Query types, `MassChunks<T>`, `MassSchedule`, `MassSystemStage`, `MassSpatialQueryCallback` |
| `unreal-api-derive/src/mass_system.rs` | Proc macro: generates extern "C" + Bevy wrappers + registrations |
| `unreal-module/src/mass_system_registry.rs` | `build_bevy_schedule()`, `mass_frame_dispatch()`, global schedule init |
| `gatherers-bevy-mass/src/fragments.rs` | `AntFragment`, `AntEncounterFragment`, `FoodFragment` with `#[derive(MassFragment)]` |
| `gatherers-bevy-mass/src/systems.rs` | 5 systems: movement, collision prepass, food decision, cooldown, boundary reflect |

### C++

| File | Role |
|------|------|
| `RustPlugin/Bindings.h` | C++ mirror of FFI types (cbindgen-style) |
| `RustPlugin/RustMassDynamicProcessor.h/.cpp` | Dynamic processor per `#[mass_system]`, cache-only mode |
| `RustPlugin/RustMassScheduleCoordinator.h/.cpp` | Gathers cached chunks, calls `mass_frame_dispatch` |
| `RustMassGatherers/GatherersBevyMassSubsystem.h/.cpp` | Subsystem: entity spawning, pipeline setup, spatial query callback, visualization |
| `RustMassGatherers/GatherersBevyMassCollisionProcessor.h/.cpp` | C++ collision processor (used in direct mode) |
| `RustMassGatherers/GatherersSimActivator.h/.cpp` | Level actor that calls `InitializeSimulation()` on BeginPlay |
| `RustMassGatherers/GatherersMassRuntime.h` | Constants: `GatherersMassPickupRadius`, `GatherersMassCarriedFoodHeight` |

### Tests

| File | Tests |
|------|-------|
| `gatherers-bevy-mass/src/systems.rs` | 27 Rust tests: unit tests per system + Bevy schedule integration + collision prepass with mock callbacks |
| `RustPluginTests/RustMassGatherers.spec.cpp` | 13 UE automation tests: registration, layout, simulation (both modes), food pickup, cooldown, boundary reflection |

## Running

Open the **GatherersBevyMass** level in the Unreal Editor and press Play. The `AGatherersSimActivator` actor (with `SimType = BevyMass`) calls `InitializeSimulation()` on the subsystem. Default: 100 ants, 50 food.

To switch to direct mode, select the `GatherersBevyMassSubsystem` in the World Outliner and uncheck `bUseBevyScheduling`.

### Tests

```bash
# Rust tests (27 system tests + 22 macro tests + 21 FFI tests)
cd unreal-rust && cargo test -p gatherers-bevy-mass -p unreal-api-derive -p unreal-ffi

# UE automation tests (run from editor)
# supplemental.RustPlugin.Gatherers.*
```

## Design Decisions

**Why Bevy scheduling on top of Mass Entity?** Mass Entity's chunk iteration is excellent for cache-friendly access, but its processor ordering is implicit (dependency-based). The Bevy schedule provides explicit stage ordering via `MassSystemStage` and resource-based conflict detection, making it easier to reason about system interactions and eventually parallelize independent systems.

**Why a spatial query callback instead of pure Rust collision?** UE's `SweepMultiByChannel` and ISM `GetInstancesOverlappingSphere` use the engine's optimized spatial acceleration structures (BVH). Reimplementing these in Rust would duplicate the physics engine. The callback pattern keeps Rust systems pure logic while leveraging UE's spatial infrastructure.

**Why zero-copy chunks?** Fragment data lives in Mass Entity's chunk memory. `MassChunks<T>` stores pointers directly into this memory — no copies during `populate_resources`. The Bevy schedule runs while these pointers are valid (same frame, game thread only).

**Why `Option<MassSpatialQueryFn>` in `MassFrameDispatchData`?** The callback is nullable — when running without food entities or in test scenarios, no spatial queries are needed. The Rust collision system gracefully handles `None` by clearing all encounters.
