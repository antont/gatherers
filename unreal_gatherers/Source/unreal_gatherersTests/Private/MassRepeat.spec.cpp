#include "Actors/Ant.h"
#include "Actors/Food.h"
#include "Editor.h"
#include "MassEntitySubsystem.h"
#include "MassEntityView.h"
#include "Math/RandomStream.h"
#include "Misc/AutomationTest.h"
#include "Simulation/GatherersAntSimulation.h"
#include "Simulation/GatherersMassSubsystem.h"
#include "Simulation/GatherersSpawnPlan.h"
#include "Simulation/GatherersWorldSpawner.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGatherersMassRepeatAutomationTest,
	"default.unreal_gatherers.Mass.AntCanPickUpAgainAfterDropCooldown",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGatherersMassRepeatAutomationTest::RunTest(const FString& Parameters)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	TestNotNull(TEXT("editor world should exist"), World);

	if (World == nullptr)
	{
		return false;
	}

	UGatherersMassSubsystem* MassSubsystem = World->GetSubsystem<UGatherersMassSubsystem>();
	TestNotNull(TEXT("gatherers Mass subsystem should exist"), MassSubsystem);

	if (MassSubsystem == nullptr)
	{
		return false;
	}

	MassSubsystem->ResetSimulation();

	FGatherersSpawnPlan Plan;
	Plan.bUseFullSimulationMode = true;
	Plan.RandomSeedBase = 123;
	Plan.PlayAreaBounds = FBox(FVector(-500.0f, -500.0f, -100.0f), FVector(500.0f, 500.0f, 100.0f));
	Plan.AntSpawns.Add(FTransform(FVector::ZeroVector));
	Plan.AntInitialDirections.Add(FVector(1.0f, 0.0f, 0.0f));

	FRandomStream RandomStream(Plan.RandomSeedBase);
	const FVector FirstTurnDirection = ComputeAntTurnDirection(
		FVector(1.0f, 0.0f, 0.0f),
		RandomStream.FRandRange(-1.0f, 1.0f),
		PI / 2.0f);
	const FVector DropLocation = FVector(10.0f, 0.0f, 0.0f) + FirstTurnDirection * 10.0f;
	const FVector SecondTurnDirection = ComputeAntTurnDirection(
		FirstTurnDirection,
		RandomStream.FRandRange(-1.0f, 1.0f),
		PI / 2.0f);
	const FVector RepeatPickupLocation = DropLocation + SecondTurnDirection * 50.0f;

	Plan.FoodSpawns.Add(FTransform(FVector(8.0f, 0.0f, 0.0f)));
	Plan.FoodSpawns.Add(FTransform(DropLocation));
	Plan.FoodSpawns.Add(FTransform(RepeatPickupLocation));

	const FGatherersSpawnResult Result = SpawnGatherersActors(*World, Plan);
	TestEqual(TEXT("spawned ant actor count"), Result.Ants.Num(), 0);
	TestEqual(TEXT("spawned food actor count"), Result.Foods.Num(), 0);
	TestEqual(TEXT("managed ant count"), MassSubsystem->GetManagedAntCount(), 1);
	TestEqual(TEXT("managed food count"), MassSubsystem->GetManagedFoodCount(), 3);

	if (MassSubsystem->GetManagedAntCount() != 1 || MassSubsystem->GetManagedFoodCount() != 3)
	{
		return false;
	}

	for (int32 StepIndex = 0; StepIndex < 8; ++StepIndex)
	{
		MassSubsystem->Tick(0.1f);
	}

	UMassEntitySubsystem* MassEntitySubsystem = World->GetSubsystem<UMassEntitySubsystem>();
	TestNotNull(TEXT("Mass entity subsystem should exist"), MassEntitySubsystem);
	if (MassEntitySubsystem == nullptr)
	{
		return false;
	}

	FMassEntityManager& EntityManager = MassEntitySubsystem->GetMutableEntityManager();
	int32 CarriedFoodCount = 0;
	for (const FMassEntityHandle FoodEntity : MassSubsystem->ManagedFoodEntities)
	{
		FMassEntityView FoodView(EntityManager, FoodEntity);
		const FGatherersMassFoodFragment& FoodFragment = FoodView.GetFragmentData<FGatherersMassFoodFragment>();
		if (!FoodFragment.bIsLoose)
		{
			++CarriedFoodCount;
		}
	}
	TestEqual(TEXT("one food should be carried again after the cooldown window"), CarriedFoodCount, 1);

	MassSubsystem->ResetSimulation();
	return true;
}
