#pragma once

#include "CoreMinimal.h"

class AAnt;
class AFood;
class FAutomationTestBase;
class UWorld;
struct FGatherersSpawnPlan;

namespace GatherersWorldAssertions
{
struct FObservedWorldState
{
	TArray<AAnt*> Ants;
	TArray<AFood*> Foods;

	bool HasSingleAntAndFood() const;
	AAnt* GetSingleAnt() const;
	AFood* GetSingleFood() const;
};

FObservedWorldState Observe(UWorld* World);

bool PollForPickupState(
	FAutomationTestBase& Test,
	UWorld* World,
	const FGatherersSpawnPlan& Plan,
	double StartTimeSeconds,
	double TimeoutSeconds,
	const FString& LabelPrefix,
	float PositionTolerance = KINDA_SMALL_NUMBER);

bool PollForPIEToEnd(
	FAutomationTestBase& Test,
	const FString& MapName,
	double StartTimeSeconds,
	double TimeoutSeconds,
	const FString& LabelPrefix);

void AssertPickupState(
	FAutomationTestBase& Test,
	const FObservedWorldState& WorldState,
	const FGatherersSpawnPlan& Plan,
	const FString& LabelPrefix,
	float PositionTolerance = KINDA_SMALL_NUMBER);
}
