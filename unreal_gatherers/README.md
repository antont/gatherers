# unreal_gatherers

First-round Unreal 5.7 port scaffold for the `gatherers` simulation.

## Current scope

This first TDD slice is intentionally narrow:

- editor-only automation test module
- deterministic spawn plan with one ant and one food
- minimal `AAnt` and `AFood`
- startup integration through `Aunreal_gatherersGameModeBase`
- standalone `-game` launch into `SimBlank`

Out of scope for this round:

- ant movement or AI
- food pickup logic
- backend or networking integration
- visual polish
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

This builds `unreal_gatherersEditor` and runs the full `unreal_gatherers` automation namespace.

Run the current spawning subset directly:

```sh
"/Users/Shared/Epic Games/UE_5.7/Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor" \
  "/Users/tonialatalo/src/gatherers/unreal_gatherers/unreal_gatherers.uproject" \
  -ExecCmds="Automation RunTest unreal_gatherers.Spawning;Quit" \
  -unattended -nop4 -nosplash -NullRHI -stdout -FullStdOutLogOutput
```

Useful targeted reruns:

```sh
-ExecCmds="Automation RunTest unreal_gatherers.Placeholder.LoadsTestModule;Quit"
-ExecCmds="Automation RunTest unreal_gatherers.Spawning.SpawnPlanDefinesOneAntAndOneFood;Quit"
-ExecCmds="Automation RunTest unreal_gatherers.Spawning.WorldSpawnerCreatesAntAndFoodActors;Quit"
-ExecCmds="Automation RunTest unreal_gatherers.Spawning.StartupSmokeSpawnsOneAntAndOneFood;Quit"
```

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

and spawn one ant actor plus one food actor.
