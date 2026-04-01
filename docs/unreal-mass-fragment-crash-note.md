# Unreal Mass Fragment Crash Note

## Context

After the fixed-step sim-rate refactor, Unreal crashed during live PIE with this stack shape:

- `UGatherersMassSubsystem::Tick`
- `UGatherersMassSubsystem::RunSimulationProcessorStep`
- `UE::Mass::Executor::Run`
- `UMassProcessor::CallExecute`
- `FWeakObjectPtr::operator=(FObjectPtr)`
- `FUObjectArray::AllocateSerialNumber`

The assertion was:

- `Assertion failed: Index >= 0 [File:Runtime/CoreUObject/Public/UObject/UObjectArray.h] [Line: 1091]`

## Current Hypothesis

The most likely issue is not the fixed-step scheduler itself, but storing `TWeakObjectPtr` inside Mass fragments:

- `FGatherersMassAntFragment::ProxyActor`
- `FGatherersMassFoodFragment::ProxyActor`

Those fragments are explicitly marked as accepted despite not being trivially copyable. That is risky in Mass, because fragment storage is optimized around simple data movement and relocation. A live PIE session may expose lifecycle or relocation behavior that short automation runs do not.

The crash stack pointing at `FWeakObjectPtr::operator=(FObjectPtr)` during Mass processor execution is consistent with that suspicion.

## Why This Is Suspicious

- Mass fragments are best kept as plain data.
- `TWeakObjectPtr` depends on UObject array bookkeeping and object serial numbers.
- PIE start/stop, world duplication, teardown, or fragment relocation may interact badly with UObject-backed fragment members.
- Our automation suite passed, but the editor crash happened in a longer interactive run, which suggests a lifecycle-sensitive bug.

## Recommended Fix Direction

Move proxy actor references out of Mass fragments.

Preferred direction:

- remove `ProxyActor` from `FGatherersMassAntFragment`
- remove `ProxyActor` from `FGatherersMassFoodFragment`
- keep any actor/visual proxy references in `UGatherersMassSubsystem`, keyed by `FMassEntityHandle`
- keep Mass fragments limited to simulation state only

That would better match Mass-style data design and should reduce the risk of UObject lifecycle issues inside fragment storage.

## Status

This is a diagnosis note, not yet a proven fix.

The next step should be to implement the fragment cleanup, rerun the Unreal suite, and then verify stability in a longer live PIE session.
