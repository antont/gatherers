#include "TestLogic/GatherersWorldAssertions.h"

#include "Actors/Ant.h"
#include "Actors/Food.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "EngineUtils.h"
#include "HAL/PlatformTime.h"
#include "MassEntitySubsystem.h"
#include "MassEntityView.h"
#include "Misc/AutomationTest.h"
#include "Misc/PackageName.h"
#include "Simulation/GatherersAntSimulation.h"
#include "Simulation/GatherersMassSubsystem.h"
#include "Simulation/GatherersSpawnPlan.h"
#include "Tests/AutomationCommon.h"

namespace GatherersWorldAssertions
{
namespace
{
TArray<FVector> CollectInstanceLocations(const UInstancedStaticMeshComponent* Instances)
{
	TArray<FVector> Locations;
	if (Instances == nullptr)
	{
		return Locations;
	}

	for (int32 InstanceIndex = 0; InstanceIndex < Instances->GetInstanceCount(); ++InstanceIndex)
	{
		FTransform InstanceTransform;
		if (Instances->GetInstanceTransform(InstanceIndex, InstanceTransform, true))
		{
			Locations.Add(InstanceTransform.GetLocation());
		}
	}

	return Locations;
}

bool ContainsLocation(const TArray<FVector>& Locations, const FVector& ExpectedLocation, float PositionTolerance)
{
	for (const FVector& Location : Locations)
	{
		if (Location.Equals(ExpectedLocation, PositionTolerance))
		{
			return true;
		}
	}

	return false;
}
}

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

bool FObservedMassVisualState::HasSingleAntAndTwoFoods() const
{
	return AntPositions.Num() == 1 && FoodPositions.Num() == 2;
}

bool FObservedMassVisualState::HasSingleAntAndOneFood() const
{
	return AntPositions.Num() == 1 && FoodPositions.Num() == 1
		&& AntVisualPositions.Num() == 1 && FoodVisualPositions.Num() == 1;
}

bool FObservedMassVisualState::HasCarriedFoodVisual(float CarriedFoodHeight, float PositionTolerance) const
{
	if (AntPositions.Num() != 1)
	{
		return false;
	}

	return ContainsLocation(
		FoodVisualPositions,
		AntPositions[0] + ComputeCarriedFoodRelativeLocation(CarriedFoodHeight),
		PositionTolerance);
}

bool FObservedMassVisualState::SingleAntAndFoodVisualsMatchSimulation(
	float CarriedFoodHeight,
	float PositionTolerance) const
{
	if (!HasSingleAntAndOneFood())
	{
		return false;
	}

	const bool bAntMatches = AntPositions[0].Equals(AntVisualPositions[0], PositionTolerance);
	if (!bAntMatches)
	{
		return false;
	}

	if (LooseFoodCount > 0)
	{
		return FoodPositions[0].Equals(FoodVisualPositions[0], PositionTolerance);
	}

	return FoodVisualPositions[0].Equals(
		AntPositions[0] + ComputeCarriedFoodRelativeLocation(CarriedFoodHeight),
		PositionTolerance);
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

FObservedMassVisualState ObserveMassVisuals(UWorld* World)
{
	FObservedMassVisualState VisualState;
	if (World == nullptr)
	{
		return VisualState;
	}

	UGatherersMassSubsystem* MassSubsystem = World->GetSubsystem<UGatherersMassSubsystem>();
	UMassEntitySubsystem* MassEntitySubsystem = World->GetSubsystem<UMassEntitySubsystem>();
	if (MassSubsystem == nullptr || MassEntitySubsystem == nullptr)
	{
		return VisualState;
	}

	FMassEntityManager& EntityManager = MassEntitySubsystem->GetMutableEntityManager();

	for (const FMassEntityHandle Entity : MassSubsystem->ManagedAntEntities)
	{
		if (!EntityManager.IsEntityValid(Entity))
		{
			continue;
		}

		FMassEntityView AntView(EntityManager, Entity);
		const FGatherersMassAntFragment& AntFragment = AntView.GetFragmentData<FGatherersMassAntFragment>();
		VisualState.AntPositions.Add(AntFragment.Position);
	}

	for (const FMassEntityHandle Entity : MassSubsystem->ManagedFoodEntities)
	{
		if (!EntityManager.IsEntityValid(Entity))
		{
			continue;
		}

		FMassEntityView FoodView(EntityManager, Entity);
		const FGatherersMassFoodFragment& FoodFragment = FoodView.GetFragmentData<FGatherersMassFoodFragment>();
		VisualState.FoodPositions.Add(FoodFragment.Position);
		if (FoodFragment.bIsLoose)
		{
			++VisualState.LooseFoodCount;
		}
	}

	VisualState.AntVisualPositions = CollectInstanceLocations(MassSubsystem->GetAntVisualComponent());
	VisualState.FoodVisualPositions = CollectInstanceLocations(MassSubsystem->GetFoodRepresentationComponent());

	return VisualState;
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

void AssertFirstDropState(
	FAutomationTestBase& Test,
	const FObservedWorldState& WorldState,
	const FGatherersSpawnPlan& Plan,
	const FString& LabelPrefix,
	float PositionTolerance)
{
	const FString CountPrefix = LabelPrefix.IsEmpty() ? TEXT("") : LabelPrefix + TEXT(" ");
	Test.TestEqual(*(CountPrefix + TEXT("ant count")), WorldState.Ants.Num(), 1);
	Test.TestEqual(*(CountPrefix + TEXT("food count")), WorldState.Foods.Num(), 2);
	Test.TestEqual(*(CountPrefix + TEXT("attached food count after first drop")), WorldState.CountAttachedFoods(), 0);

	AAnt* Ant = WorldState.GetSingleAnt();
	if (Ant == nullptr)
	{
		return;
	}

	bool bFoundDroppedFoodAwayFromForwardSpawn = false;
	for (AFood* Food : WorldState.Foods)
	{
		if (Food == nullptr)
		{
			continue;
		}

		bFoundDroppedFoodAwayFromForwardSpawn |=
			!Food->GetActorLocation().Equals(Plan.FoodSpawns[0].GetLocation(), PositionTolerance);
	}

	Test.TestTrue(
		*(CountPrefix + TEXT("ant moved from its spawn point before the first drop")),
		!Ant->GetActorLocation().Equals(Plan.AntSpawns[0].GetLocation(), PositionTolerance));
	Test.TestTrue(
		*(CountPrefix + TEXT("one loose food sits away from the original forward spawn after the first drop")),
		bFoundDroppedFoodAwayFromForwardSpawn);
}

void AssertMassPickupState(
	FAutomationTestBase& Test,
	const FObservedMassVisualState& VisualState,
	const FGatherersSpawnPlan& Plan,
	const FString& LabelPrefix,
	float CarriedFoodHeight,
	float PositionTolerance)
{
	const FString CountPrefix = LabelPrefix.IsEmpty() ? TEXT("") : LabelPrefix + TEXT(" ");
	Test.TestEqual(*(CountPrefix + TEXT("ant entity count")), VisualState.AntPositions.Num(), 1);
	Test.TestEqual(*(CountPrefix + TEXT("food entity count")), VisualState.FoodPositions.Num(), 2);
	Test.TestEqual(*(CountPrefix + TEXT("ant visual instance count")), VisualState.AntVisualPositions.Num(), 1);
	Test.TestEqual(*(CountPrefix + TEXT("food visual instance count")), VisualState.FoodVisualPositions.Num(), 2);
	Test.TestTrue(
		*(CountPrefix + TEXT("one food is visibly carried by the ant")),
		VisualState.HasCarriedFoodVisual(CarriedFoodHeight, PositionTolerance));

	if (VisualState.AntPositions.Num() != 1)
	{
		return;
	}

	Test.TestTrue(
		*(CountPrefix + TEXT("ant moves from its spawn point")),
		!VisualState.AntPositions[0].Equals(Plan.AntSpawns[0].GetLocation(), PositionTolerance));
}

void AssertMassFirstDropState(
	FAutomationTestBase& Test,
	const FObservedMassVisualState& VisualState,
	const FGatherersSpawnPlan& Plan,
	const FString& LabelPrefix,
	float PositionTolerance)
{
	const FString CountPrefix = LabelPrefix.IsEmpty() ? TEXT("") : LabelPrefix + TEXT(" ");
	Test.TestEqual(*(CountPrefix + TEXT("ant entity count")), VisualState.AntPositions.Num(), 1);
	Test.TestEqual(*(CountPrefix + TEXT("food entity count")), VisualState.FoodPositions.Num(), 2);
	Test.TestEqual(*(CountPrefix + TEXT("ant visual instance count")), VisualState.AntVisualPositions.Num(), 1);
	Test.TestEqual(*(CountPrefix + TEXT("food visual instance count")), VisualState.FoodVisualPositions.Num(), 2);
	Test.TestFalse(
		*(CountPrefix + TEXT("no food remains visibly carried after the first drop")),
		VisualState.HasCarriedFoodVisual(20.0f, PositionTolerance));

	if (VisualState.AntPositions.Num() != 1)
	{
		return;
	}

	Test.TestTrue(
		*(CountPrefix + TEXT("ant moved from its spawn point before the first drop")),
		!VisualState.AntPositions[0].Equals(Plan.AntSpawns[0].GetLocation(), PositionTolerance));

	bool bFoundDroppedFoodAwayFromForwardSpawn = false;
	for (const FVector& FoodVisualPosition : VisualState.FoodVisualPositions)
	{
		bFoundDroppedFoodAwayFromForwardSpawn |=
			!FoodVisualPosition.Equals(Plan.FoodSpawns[0].GetLocation(), PositionTolerance);
	}

	Test.TestTrue(
		*(CountPrefix + TEXT("one loose food visual sits away from the original forward spawn after the first drop")),
		bFoundDroppedFoodAwayFromForwardSpawn);
}

bool PollForMassPickupState(
	FAutomationTestBase& Test,
	UWorld* World,
	const FGatherersSpawnPlan& Plan,
	double StartTimeSeconds,
	double TimeoutSeconds,
	const FString& LabelPrefix,
	float CarriedFoodHeight,
	float PositionTolerance)
{
	Test.TestNotNull(*(LabelPrefix + TEXT(" world should exist")), World);
	if (World == nullptr)
	{
		return true;
	}

	const FObservedMassVisualState VisualState = ObserveMassVisuals(World);
	const bool bHasExpectedCounts = VisualState.HasSingleAntAndTwoFoods();
	const bool bAntMoved = VisualState.AntPositions.Num() == 1
		&& !VisualState.AntPositions[0].Equals(Plan.AntSpawns[0].GetLocation(), PositionTolerance);
	const bool bFoodVisiblyCarried = VisualState.HasCarriedFoodVisual(CarriedFoodHeight, PositionTolerance);

	if (!(bHasExpectedCounts && bAntMoved && bFoodVisiblyCarried))
	{
		if (FPlatformTime::Seconds() - StartTimeSeconds < TimeoutSeconds)
		{
			return false;
		}
	}

	AssertMassPickupState(Test, VisualState, Plan, LabelPrefix, CarriedFoodHeight, PositionTolerance);
	return true;
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
