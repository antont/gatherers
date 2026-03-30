# unreal_gatherers

Unreal 5.7 port of the `gatherers` simulation.

## Current scope

Current implemented slice:

- editor-only automation test module
- deterministic spawn plan with one ant and two foods
- `AAnt`/`AFood` repeated gather loop
- startup integration through `Aunreal_gatherersGameModeBase`
- standalone `-game` launch into `SimBlank`
- visual/manual editor inspection path

Still out of scope:

- backend or networking integration
- packaged build automation
- Functional Testing plugin and Gauntlet

## Build the editor target

```sh
"/Users/Shared/Epic Games/UE_5.7/Engine/Build/BatchFiles/Mac/Build.sh" \
  unreal_gatherersEditor Mac Development \
  -Project="/Users/tonialatalo/src/gatherers/unreal_gatherers/unreal_gatherers.uproject"
```

## Run the Unreal automation tests

Recommended entrypoint:

```sh
./scripts/test_unreal.sh
```

This builds `unreal_gatherersEditor` and runs the non-visual default `default.unreal_gatherers` automation namespace.

Run a focused subset directly:

```sh
"/Users/Shared/Epic Games/UE_5.7/Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor" \
  "/Users/tonialatalo/src/gatherers/unreal_gatherers/unreal_gatherers.uproject" \
  -ExecCmds="Automation RunTest default.unreal_gatherers.Spawning;Quit" \
  -unattended -nop4 -nosplash -NullRHI -stdout -FullStdOutLogOutput
```

Useful targeted reruns:

```sh
-ExecCmds="Automation RunTest default.unreal_gatherers.Placeholder.LoadsTestModule;Quit"
-ExecCmds="Automation RunTest default.unreal_gatherers.Spawning.SpawnPlanDefinesOneAntAndTwoFoods;Quit"
-ExecCmds="Automation RunTest default.unreal_gatherers.Spawning.WorldSpawnerCreatesAntAndTwoFoodActors;Quit"
-ExecCmds="Automation RunTest default.unreal_gatherers.Simulation.AntMovesAndPicksUpFoodInWorld;Quit"
-ExecCmds="Automation RunTest default.unreal_gatherers.Simulation.AntDropsFoodBackIntoWorld;Quit"
-ExecCmds="Automation RunTest default.unreal_gatherers.Simulation.AntDropsFoodTwiceInSameEditorSession;Quit"
-ExecCmds="Automation RunTest supplemental.unreal_gatherers.Spawning.StartupSmokeSpawnsOneAntAndTwoFoods;Quit"
```

## Run the visual gather demo

The visual/manual pickup path is intentionally separate from `./scripts/test_unreal.sh` so the default automation suite stays clean and rerunnable.

Run this scenario from the editor automation UI under:

`manual.unreal_gatherers.Visual.AntFirstDropLeavesWorldForInspection`

That visual/manual test loads `SimBlank` into a clean editor world, frames the viewport around the two-food layout, advances the ant through pickup and return, and leaves the first dropped-food state behind for inspection.

## Refresh editor indexing

To regenerate Unreal's clang compilation database for Cursor/clangd:

```sh
./scripts/generate_unreal_clangdb.sh
```

This writes `unreal_gatherers/compile_commands.json`, which is ignored from git because it contains machine-local paths. The repo includes workspace settings plus `unreal_gatherers/.clangd` so Cursor should use that database for navigation and diagnostics once it exists. If the editor does not refresh automatically, reload the Cursor window after running the script.

## Run standalone game mode

This launches the project outside the editor UI while still using the editor executable:

```sh
"/Users/Shared/Epic Games/UE_5.7/Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor" \
  "/Users/tonialatalo/src/gatherers/unreal_gatherers/unreal_gatherers.uproject" \
  -game -stdout -FullStdOutLogOutput -NoSplash
```

The current startup path should load:

- `/Game/SimBlank/Levels/SimBlank`
- `unreal_gatherersGameModeBase`

and spawn one ant actor plus two food actors.
