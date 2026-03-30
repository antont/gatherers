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
bool FObservedWorldState::HasSingleAntAndFood() const
{
	return Ants.Num() == 1 && Foods.Num() == 1;
}

AAnt* FObservedWorldState::GetSingleAnt() const
{
	return Ants.Num() == 1 ? Ants[0] : nullptr;
}

AFood* FObservedWorldState::GetSingleFood() const
{
	return Foods.Num() == 1 ? Foods[0] : nullptr;
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
	Test.TestEqual(*(CountPrefix + TEXT("food count")), WorldState.Foods.Num(), 1);

	AAnt* Ant = WorldState.GetSingleAnt();
	AFood* Food = WorldState.GetSingleFood();
	if (Ant == nullptr || Food == nullptr)
	{
		return;
	}

	Test.TestTrue(*(CountPrefix + TEXT("food attaches to the ant")), Food->GetAttachParentActor() == Ant);
	Test.TestTrue(
		*(CountPrefix + TEXT("ant moves from its spawn point")),
		!Ant->GetActorLocation().Equals(Plan.AntSpawns[0].GetLocation(), PositionTolerance));
	Test.TestTrue(
		*(CountPrefix + TEXT("food no longer remains at its spawn point")),
		!Food->GetActorLocation().Equals(Plan.FoodSpawns[0].GetLocation(), PositionTolerance));
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
	AFood* Food = WorldState.GetSingleFood();

	const bool bHasExpectedCounts = WorldState.HasSingleAntAndFood();
	const bool bAntMoved = Ant != nullptr
		&& !Ant->GetActorLocation().Equals(Plan.AntSpawns[0].GetLocation(), PositionTolerance);
	const bool bFoodAttachedToAnt = Ant != nullptr && Food != nullptr && Food->GetAttachParentActor() == Ant;

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
