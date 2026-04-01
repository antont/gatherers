# Unreal Mass Processor Crash Note

## Crash Signature

```
Assertion failed: Index >= 0 [File:Runtime/CoreUObject/Public/UObject/UObjectArray.h] [Line: 1091]

FUObjectArray::AllocateSerialNumber(int)
FWeakObjectPtr::operator=(FObjectPtr)
UMassProcessor::CallExecute(FMassEntityManager&, FMassExecutionContext&)
UE::Mass::Executor::RunProcessorsView(...)
UE::Mass::Executor::Run(FMassRuntimePipeline&, ...)
UGatherersMassSubsystem::RunSimulationProcessorStep(float)
UGatherersMassSubsystem::Tick(float)
```

## Root Cause

The `FMassRuntimePipeline` members on `UGatherersMassSubsystem` were **not marked `UPROPERTY()`**:

```cpp
// BEFORE (broken) — GC cannot see into these members:
FMassRuntimePipeline SimulationProcessorPipeline;
FMassRuntimePipeline VisualProcessorPipeline;
```

`FMassRuntimePipeline` is a `USTRUCT()` containing `UPROPERTY() TArray<TObjectPtr<UMassProcessor>> Processors`. The processor UObjects stored inside are only protected from garbage collection if GC can **traverse the full UPROPERTY chain** from a root UObject. Without `UPROPERTY()` on the subsystem member, GC never reaches the pipeline's internal `Processors` array.

When GC collects a processor, its UObject slot is freed. On the next `Executor::Run`, `CallExecute` is invoked on the stale processor. In editor/development builds, `CallExecute` calls `Context.DebugSetProcessor(this)` which assigns `this` (the GC'd processor pointer) to a `TWeakObjectPtr<UMassProcessor>`. The `FWeakObjectPtr::operator=(FObjectPtr)` then calls `GUObjectArray.ObjectToIndex()` on the freed object, getting a negative index, which triggers the `Index >= 0` assertion.

## Why It Was Intermittent

The crash only happens after GC collects the processor instances. In short automation test runs, GC may not trigger before the test ends. In longer interactive PIE sessions (or with high sim-rate multipliers generating many frames of work), GC has more opportunity to run and collect the unrooted processors.

## Fix

```cpp
// AFTER (fixed) — GC traverses into the pipeline structs:
UPROPERTY(Transient)
FMassRuntimePipeline SimulationProcessorPipeline;

UPROPERTY(Transient)
FMassRuntimePipeline VisualProcessorPipeline;
```

`Transient` is appropriate because these pipelines are runtime-only state that should not be serialized.

## Earlier Hypotheses (Superseded)

### Hypothesis 1: TWeakObjectPtr in Mass fragments

The initial theory was that storing `TWeakObjectPtr<AAnt>` and `TWeakObjectPtr<AFood>` inside Mass fragments caused the crash, because fragment storage is optimized for trivially-copyable data.

This was addressed in commit `0e7924f` by moving proxy actor references into subsystem-owned `TMap`s. While this was a correct architectural improvement (Mass fragments should contain only plain data), it did not fix the crash because the root cause was the missing `UPROPERTY()` on the pipeline members.

### Hypothesis 2: UObject operations inside Mass processors

A later theory suggested that calling `DetachFromActor` / `SetActorLocation` on food proxy actors from within a `ForEachEntityChunk` loop triggered engine-internal `FWeakObjectPtr` bookkeeping that hit stale state. While calling heavyweight UObject operations from within Mass processors is not ideal practice, this was not the cause of this specific crash.

## Key Takeaway

Any `USTRUCT()` containing `UPROPERTY()` UObject references must itself be reached through a `UPROPERTY()` chain from a root UObject, or GC will not traverse it. This applies to `FMassRuntimePipeline` and any similar engine structs that own UObjects internally.
