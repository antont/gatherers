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

	bool HasSingleAntAndTwoFoods() const;
	AAnt* GetSingleAnt() const;
	int32 CountAttachedFoods() const;
	AFood* GetFirstAttachedFood() const;
};

struct FObservedMassVisualState
{
	TArray<FVector> AntPositions;
	TArray<FVector> FoodPositions;
	TArray<FVector> AntVisualPositions;
	TArray<FVector> FoodVisualPositions;
	int32 LooseFoodCount = 0;

	bool HasSingleAntAndTwoFoods() const;
	bool HasSingleAntAndOneFood() const;
	bool HasCarriedFoodVisual(float CarriedFoodHeight, float PositionTolerance = KINDA_SMALL_NUMBER) const;
	bool SingleAntAndFoodVisualsMatchSimulation(
		float CarriedFoodHeight,
		float PositionTolerance = KINDA_SMALL_NUMBER) const;
};

FObservedWorldState Observe(UWorld* World);
FObservedMassVisualState ObserveMassVisuals(UWorld* World);

bool PollForPickupState(
	FAutomationTestBase& Test,
	UWorld* World,
	const FGatherersSpawnPlan& Plan,
	double StartTimeSeconds,
	double TimeoutSeconds,
	const FString& LabelPrefix,
	float PositionTolerance = KINDA_SMALL_NUMBER);

bool PollForMassPickupState(
	FAutomationTestBase& Test,
	UWorld* World,
	const FGatherersSpawnPlan& Plan,
	double StartTimeSeconds,
	double TimeoutSeconds,
	const FString& LabelPrefix,
	float CarriedFoodHeight,
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

void AssertFirstDropState(
	FAutomationTestBase& Test,
	const FObservedWorldState& WorldState,
	const FGatherersSpawnPlan& Plan,
	const FString& LabelPrefix,
	float PositionTolerance = KINDA_SMALL_NUMBER);

void AssertMassFirstDropState(
	FAutomationTestBase& Test,
	const FObservedMassVisualState& VisualState,
	const FGatherersSpawnPlan& Plan,
	const FString& LabelPrefix,
	float PositionTolerance = KINDA_SMALL_NUMBER);

void AssertMassPickupState(
	FAutomationTestBase& Test,
	const FObservedMassVisualState& VisualState,
	const FGatherersSpawnPlan& Plan,
	const FString& LabelPrefix,
	float CarriedFoodHeight,
	float PositionTolerance = KINDA_SMALL_NUMBER);
}
