#include "TestLogic/GatherersWorldAssertions.h"

#include "Actors/Ant.h"
#include "Actors/Food.h"
#include "EngineUtils.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"
#include "Misc/PackageName.h"
#include "Simulation/GatherersSpawnPlan.h"
#include "Tests/AutomationCommon.h"

namespace GatherersWorldAssertions
{
bool FObservedWorldState::HasSingleAntAndTwoFoods() const
{
	return Ants.Num() == 1 && Foods.Num() == 2;
}

AAnt* FObservedWorldState::GetSingleAnt() const
{
	return Ants.Num() == 1 ? Ants[0] : nullptr;
}

int32 FObservedWorldState::CountAttachedFoods() const
{
	int32 AttachedFoodCount = 0;
	for (AFood* Food : Foods)
	{
		if (Food != nullptr && Food->GetAttachParentActor() != nullptr)
		{
			++AttachedFoodCount;
		}
	}

	return AttachedFoodCount;
}

AFood* FObservedWorldState::GetFirstAttachedFood() const
{
	for (AFood* Food : Foods)
	{
		if (Food != nullptr && Food->GetAttachParentActor() != nullptr)
		{
			return Food;
		}
	}

	return nullptr;
}

FObservedWorldState Observe(UWorld* World)
{
	FObservedWorldState WorldState;
	if (World == nullptr)
	{
		return WorldState;
	}

	for (TActorIterator<AAnt> It(World); It; ++It)
	{
		WorldState.Ants.Add(*It);
	}

	for (TActorIterator<AFood> It(World); It; ++It)
	{
		WorldState.Foods.Add(*It);
	}

	return WorldState;
}

void AssertPickupState(
	FAutomationTestBase& Test,
	const FObservedWorldState& WorldState,
	const FGatherersSpawnPlan& Plan,
	const FString& LabelPrefix,
	float PositionTolerance)
{
	const FString CountPrefix = LabelPrefix.IsEmpty() ? TEXT("") : LabelPrefix + TEXT(" ");
	Test.TestEqual(*(CountPrefix + TEXT("ant count")), WorldState.Ants.Num(), 1);
	Test.TestEqual(*(CountPrefix + TEXT("food count")), WorldState.Foods.Num(), 2);

	AAnt* Ant = WorldState.GetSingleAnt();
	AFood* AttachedFood = WorldState.GetFirstAttachedFood();
	if (Ant == nullptr || AttachedFood == nullptr)
	{
		return;
	}

	Test.TestEqual(*(CountPrefix + TEXT("attached food count")), WorldState.CountAttachedFoods(), 1);
	Test.TestTrue(*(CountPrefix + TEXT("one food attaches to the ant")), AttachedFood->GetAttachParentActor() == Ant);
	Test.TestTrue(
		*(CountPrefix + TEXT("ant moves from its spawn point")),
		!Ant->GetActorLocation().Equals(Plan.AntSpawns[0].GetLocation(), PositionTolerance));
	Test.TestTrue(
		*(CountPrefix + TEXT("attached food no longer remains at the forward spawn point")),
		!AttachedFood->GetActorLocation().Equals(Plan.FoodSpawns[0].GetLocation(), PositionTolerance));
}

bool PollForPickupState(
	FAutomationTestBase& Test,
	UWorld* World,
	const FGatherersSpawnPlan& Plan,
	double StartTimeSeconds,
	double TimeoutSeconds,
	const FString& LabelPrefix,
	float PositionTolerance)
{
	Test.TestNotNull(*(LabelPrefix + TEXT(" world should exist")), World);
	if (World == nullptr)
	{
		return true;
	}

	const FObservedWorldState WorldState = Observe(World);
	AAnt* Ant = WorldState.GetSingleAnt();
	AFood* AttachedFood = WorldState.GetFirstAttachedFood();

	const bool bHasExpectedCounts = WorldState.HasSingleAntAndTwoFoods();
	const bool bAntMoved = Ant != nullptr
		&& !Ant->GetActorLocation().Equals(Plan.AntSpawns[0].GetLocation(), PositionTolerance);
	const bool bFoodAttachedToAnt = Ant != nullptr && AttachedFood != nullptr
		&& AttachedFood->GetAttachParentActor() == Ant
		&& WorldState.CountAttachedFoods() == 1;

	if (!(bHasExpectedCounts && bAntMoved && bFoodAttachedToAnt))
	{
		if (FPlatformTime::Seconds() - StartTimeSeconds < TimeoutSeconds)
		{
			return false;
		}
	}

	AssertPickupState(Test, WorldState, Plan, LabelPrefix, PositionTolerance);
	return true;
}

bool PollForPIEToEnd(
	FAutomationTestBase& Test,
	const FString& MapName,
	double StartTimeSeconds,
	double TimeoutSeconds,
	const FString& LabelPrefix)
{
	UWorld* TestWorld = AutomationCommon::GetAnyGameWorld();
	if (TestWorld == nullptr)
	{
		return true;
	}

	FString ShortMapName = FPackageName::GetShortName(MapName);
	ShortMapName = FPaths::GetBaseFilename(ShortMapName);
	if (TestWorld->GetName() != ShortMapName)
	{
		return true;
	}

	if (FPlatformTime::Seconds() - StartTimeSeconds < TimeoutSeconds)
	{
		return false;
	}

	Test.AddError(FString::Printf(
		TEXT("%s PIE map '%s' should be torn down before the next rerun"),
		*LabelPrefix,
		*ShortMapName));
	return true;
}
}
