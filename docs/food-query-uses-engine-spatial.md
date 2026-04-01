# Food query: intentionally uses engine spatial systems

## Decision

The food proximity query (`QueryLooseFoodEntitiesAlongSweep`, `QueryLooseFoodEntitiesOverlappingSphere`) deliberately uses Unreal's built-in spatial queries (ISM `GetInstancesOverlappingBox`, `SweepMultiByChannel`) rather than maintaining a custom spatial index.

This is an explicit design choice: the Bevy sim has its own `spatial_index.rs`, but the Unreal port should use the best the engine offers for collision/hit detection. Building a parallel spatial index would duplicate what the engine already provides and miss the point of porting to Unreal.

## Tradeoff

This means simulation processors read from ISM component state (a UObject) during the sim loop. This is acceptable because:
- It's a read-only query, not a write
- The ISM spatial data is synced from fragment positions at the end of each frame
- At high sim rates, queries between visual syncs use slightly stale spatial data (positions from last frame's visual sync, not the current sub-step). In practice this is fine because food doesn't move on its own.

## Not a TODO

This is not something to "fix." It's the intended architecture for the Unreal port.
