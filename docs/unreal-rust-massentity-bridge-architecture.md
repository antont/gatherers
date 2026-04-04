# Gatherers MassEntity-Rust Bridge Architecture

An ant colony simulation running on UE5's MassEntity ECS, where C++ owns the entity lifecycle and rendering, but Rust owns the simulation math — connected via zero-copy FFI.

## Overview

```
┌─────────────────────────────────────────────────────────┐
│  Unreal Engine (C++)                                    │
│                                                         │
│  UGatherersRustSubsystem                                │
│  ├── Entity spawning (ants + food)                      │
│  ├── Fixed-timestep tick loop                           │
│  ├── ISM visual sync (spheres for ants/food)            │
│  └── Sweep queries (UWorld::SweepMulti for food)        │
│                                                         │
│  Processors (MassEntity pipeline):                      │
│  ┌──────────────────────┐  ┌────────────────────────┐   │
│  │ AntMovementProcessor │  │ FoodInteractionProc    │   │
│  │                      │  │                        │   │
│  │ ForEachEntityChunk → │  │ sweep query (C++) →    │   │
│  │ mass_ant_movement()  │  │ mass_ant_food_decision()│  │
│  │ (entire chunk)       │  │ (per ant)              │   │
│  │                      │  │ ← decision code        │   │
│  │                      │  │ apply entity ops (C++) │   │
│  └──────────┬───────────┘  └──────────┬─────────────┘   │
│             │ FFI fn ptr              │ FFI fn ptr       │
├─────────────┼─────────────────────────┼─────────────────┤
│             ▼                         ▼                  │
│  gatherers-sim (Rust crate, no engine deps)             │
│                                                         │
│  ffi.rs          ← extern "C" wrappers                  │
│  movement.rs     ← ant_movement_system(&mut [Ant], dt)  │
│  food_decision.rs← ant_food_decision(&mut Ant, enc)     │
│  fragments.rs    ← #[repr(C)] structs matching C++      │
└─────────────────────────────────────────────────────────┘
```

## Zero-Copy FFI

C++ fragments and Rust structs share identical memory layout via `#[repr(C)]` + `static_assert` on every field offset:

```
FGatherersMassAntFragment (C++)     AntFragment (Rust)
───────────────────────────────     ──────────────────
offset  0: FVector Position         [f64; 3] position
offset 24: FVector PreviousPosition [f64; 3] previous_position
offset 48: FVector Direction        [f64; 3] direction
offset 72: FMassEntityHandle        [i32; 2] carried_food_handle
offset 80: float PickupCooldown     f32 pickup_cooldown_remaining_seconds
offset 84: float MovementSpeed      f32 movement_speed
offset 88: float TurnJitterRadians  f32 turn_jitter_radians
offset 92: int32 RandomSeed         i32 random_seed
Total: 96 bytes, align 8
```

C++ passes `&AntFragments[0].Position.X` directly — no serialization, no copies. Rust receives it as `*mut AntFragment` and operates on a slice in-place.

## Function Pointer Registration

At DLL load, Rust populates a `RustBindings` struct with function pointers:

```
RustBindings {
    tick, begin_play, allocate,
    mass_bob_process,              // spike demo
    mass_ant_movement,             // → rust_mass_ant_movement
    mass_ant_food_decision,        // → rust_mass_ant_food_decision
}
```

The `implement_unreal_module!` macro wires these in `register_unreal_bindings()`. C++ processors read them from `FRustPluginModule::Plugin.Rust`.

## Split Responsibility for Food Interaction

This is the key design decision. Food pickup/drop requires both pure logic (should the ant pick up?) and UE entity operations (mark food as carried, update ISM collision). These are split:

**Rust decides:**
- Is the ant carrying food? Is there an encounter? Is cooldown expired?
- Returns action code: `0` = nothing, `1` = pick up, `2` = drop
- Mutates ant fragment: snaps position to encounter, computes turn direction (180deg + jitter via LCG RNG), sets/clears carried handle, sets cooldown

**C++ applies:**
- Saves old `CarriedFoodEntity` before calling Rust (Rust clears it on drop)
- Drop (2): marks old carried food `bIsLoose = true`, sets food position
- PickUp (1): marks nearby food `bIsLoose = false`
- These require `FMassEntityManager` access which stays in C++

## What Stays in C++

- USTRUCT fragment/tag definitions (required by MassEntity reflection)
- Processor UCLASS shells + `ConfigureQueries`
- `UWorld::SweepMultiByChannel` for food collision
- Entity create/destroy, tag add/remove
- ISM visual sync (instanced mesh rendering)
- Time accumulation (trivial)

## What's in Rust (gatherers-sim)

- Movement math: position update, boundary reflection, cooldown decrement
- Food decision logic: pickup/drop rules, turn direction computation
- LCG RNG matching `FRandomStream` for deterministic behavior
- All simulation constants
- Zero engine dependencies — testable standalone with 45 unit tests

## Simulation Loop

```
Tick(DeltaTime)
  ├── Fixed-timestep accumulator (adaptive step size from bounds)
  ├── Per step:
  │   ├── TimeAccumulationProcessor (C++, trivial counter)
  │   ├── AntMovementProcessor → Rust (batch, whole chunks)
  │   └── FoodInteractionProcessor → C++ sweep + Rust decision + C++ entity ops
  └── VisualSyncProcessor (update ISM transforms from fragment positions)
```

## Repository Layout

The implementation lives in the `gatherers-bridge` branch of `unreal-rust`:

- `gatherers-sim/` — Rust crate (fragments, movement, food_decision, ffi)
- `RustPlugin/Source/RustMassGatherers/` — C++ MassEntity module (fragments, processors, subsystem, simulation helpers, runtime constants)
- `RustPlugin/Source/RustPlugin/Bindings.h` — shared FFI types (RustBindings struct)
- `unreal-ffi/src/lib.rs` — Rust-side FFI type definitions
- `unreal-module/src/lib.rs` — module macro with processor registration
- `unreal-rust-example/src/lib.rs` — wires gatherers-sim FFI functions into the module
