# Move proxy actor sync out of simulation processor

## Current state

`FGatherersFoodInteractionProcessor::Execute` (GatherersSimulationProcessors.cpp:160-163) performs UObject operations inside the simulation loop:

```cpp
if (AFood* CarriedFoodProxy = MassSubsystem.GetFoodProxyActor(AntFragment.CarriedFoodEntity))
{
    CarriedFoodProxy->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
    CarriedFoodProxy->SetActorLocation(AntFragment.Position);
}
```

This violates the sim/visual layering: simulation processors should only modify fragment data, and all UObject side-effects should happen in the visual sync step.

## Why it matters

- Simulation processors run N times per frame (up to thousands at high speed multipliers). UObject operations are expensive and redundant when repeated — only the final state matters for rendering.
- Mixing UObject writes into the sim loop creates a dependency between simulation correctness and the UObject/rendering subsystem state.
- The visual sync step (`SyncVisualInstances`) already handles food positioning via `ComputeFoodVisualPosition`, so the proxy actor manipulation is redundant for ISM-based visuals.

## Suggested fix

- Remove the `DetachFromActor` / `SetActorLocation` calls from `FGatherersFoodInteractionProcessor::Execute`.
- In the visual sync step (which runs once per frame after all sim steps), sync proxy actor positions from fragment data for any entities that have proxy actors.
- The food interaction processor should only modify `FGatherersMassFoodFragment` fields (`bIsLoose`, `Position`) and `FGatherersMassAntFragment` fields (`CarriedFoodEntity`, `Direction`, cooldown).
