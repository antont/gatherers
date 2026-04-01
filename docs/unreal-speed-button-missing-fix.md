# Unreal Speed Control Button Not Showing

## Symptom

When running the sim in the editor (PIE), only the 3D view appears — no speed control button/widget is visible.

## Root Cause

The `SimBlank` level has a **World Settings GameMode Override** set to `BP_SimGameMode`, a Blueprint that derives from the engine's `AGameModeBase` rather than from `Aunreal_gatherersGameModeBase`.

This means:
- `BP_SimGameMode` uses `BP_SimPlayerController` (engine `APlayerController`)
- `AGatherersPlayerController::BeginPlay()` never runs
- The `UGatherersTimeControlWidget` is never created or added to the viewport

The C++ chain that creates the widget:
1. `Aunreal_gatherersGameModeBase` constructor sets `PlayerControllerClass = AGatherersPlayerController`
2. `AGatherersPlayerController::BeginPlay()` calls `CreateWidget<UGatherersTimeControlWidget>()` and `AddToViewport()`

`DefaultEngine.ini` correctly sets `GlobalDefaultGameMode=/Script/unreal_gatherers.unreal_gatherersGameModeBase`, but the per-level Blueprint override takes precedence.

## Fix

Clear the GameMode override in the level so it falls through to the C++ class:

1. Open `SimBlank` level
2. **Window → World Settings**
3. Set **GameMode Override** to `None`

Alternatively, re-parent `BP_SimGameMode` to inherit from `Aunreal_gatherersGameModeBase` and `BP_SimPlayerController` from `AGatherersPlayerController`.

## Related Files

- `Source/unreal_gatherers/Private/Input/GatherersPlayerController.cpp` — widget creation
- `Source/unreal_gatherers/unreal_gatherersGameModeBase.cpp` — PlayerControllerClass assignment
- `Content/SimBlank/Blueprints/BP_SimGameMode.uasset` — overriding Blueprint
- `Config/DefaultEngine.ini` line 7 — correct global default
