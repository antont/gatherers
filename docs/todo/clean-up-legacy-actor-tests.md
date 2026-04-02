# Clean up legacy actor-based tests

The Unreal project was converted from actor-based simulation to Mass ECS.
Some test files still exercise the old actor path which is unused in production.

## Tests to remove or rework

- **AntPickupActor.spec.cpp** — PIE tests using AAnt/AFood actors directly, fully superseded by Mass pickup/drop/cooldown tests
- **WorldSpawner.spec.cpp** — first test (`FGatherersWorldSpawnerAutomationTest`) tests old actor spawning with `bSpawnActorVisuals = true`; the second test is the Mass version and should stay

## Context

The old actor code (AAnt, AFood, bSpawnActorVisuals path) still compiles and works,
so the tests pass — they just test a code path nobody uses in the actual game.

If the repo is reorganized to keep both actor and ECS versions as separate
projects/levels, these tests could stay with the actor version instead of
being deleted.
