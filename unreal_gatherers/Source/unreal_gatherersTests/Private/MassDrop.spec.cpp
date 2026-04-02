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
	FGatherersMassDropAutomationTest,
	"default.unreal_gatherers.Mass.CarryingAntDropsFoodAtNextLooseFood",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGatherersMassDropAutomationTest::RunTest(const FString& Parameters)
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
	const FVector PickupEncounterPoint(8.0f, 0.0f, 0.0f);
	const FVector ExpectedDropLocation = PickupEncounterPoint + FirstTurnDirection * 10.0f;

	Plan.FoodSpawns.Add(FTransform(FVector(8.0f, 0.0f, 0.0f)));
	Plan.FoodSpawns.Add(FTransform(ExpectedDropLocation));

	const FGatherersSpawnResult Result = SpawnGatherersActors(*World, Plan);
	TestEqual(TEXT("spawned ant actor count"), Result.Ants.Num(), 0);
	TestEqual(TEXT("spawned food actor count"), Result.Foods.Num(), 0);
	TestEqual(TEXT("managed ant count"), MassSubsystem->GetManagedAntCount(), 1);
	TestEqual(TEXT("managed food count"), MassSubsystem->GetManagedFoodCount(), 2);

	if (MassSubsystem->GetManagedAntCount() != 1 || MassSubsystem->GetManagedFoodCount() != 2)
	{
		return false;
	}

	MassSubsystem->RunSimulationProcessorsForTesting(0.1f);
	MassSubsystem->RunSimulationProcessorsForTesting(0.1f);

	UMassEntitySubsystem* MassEntitySubsystem = World->GetSubsystem<UMassEntitySubsystem>();
	TestNotNull(TEXT("Mass entity subsystem should exist"), MassEntitySubsystem);
	if (MassEntitySubsystem == nullptr)
	{
		return false;
	}

	FMassEntityManager& EntityManager = MassEntitySubsystem->GetMutableEntityManager();
	FMassEntityView FirstFoodView(EntityManager, MassSubsystem->ManagedFoodEntities[0]);
	FMassEntityView SecondFoodView(EntityManager, MassSubsystem->ManagedFoodEntities[1]);
	const FGatherersMassFoodFragment& FirstFoodFragment = FirstFoodView.GetFragmentData<FGatherersMassFoodFragment>();
	const FGatherersMassFoodFragment& SecondFoodFragment = SecondFoodView.GetFragmentData<FGatherersMassFoodFragment>();
	TestTrue(TEXT("first food should be loose again after the drop"), FirstFoodFragment.bIsLoose);
	TestTrue(TEXT("second food should stay loose in the world"), SecondFoodFragment.bIsLoose);
	TestTrue(TEXT("dropped food should appear at the deterministic turn-based drop location"), FirstFoodFragment.Position.Equals(ExpectedDropLocation, 1.0f));

	MassSubsystem->ResetSimulation();
	return true;
}
