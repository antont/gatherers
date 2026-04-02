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
	FGatherersMassTurnAutomationTest,
	"default.unreal_gatherers.Mass.PickupUsesStoredTurnState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGatherersMassTurnAutomationTest::RunTest(const FString& Parameters)
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
	Plan.FoodSpawns.Add(FTransform(FVector(8.0f, 0.0f, 0.0f)));

	const FGatherersSpawnResult Result = SpawnGatherersActors(*World, Plan);
	TestEqual(TEXT("spawned ant actor count"), Result.Ants.Num(), 0);
	TestEqual(TEXT("spawned food actor count"), Result.Foods.Num(), 0);
	TestEqual(TEXT("managed ant count"), MassSubsystem->GetManagedAntCount(), 1);
	TestEqual(TEXT("managed food count"), MassSubsystem->GetManagedFoodCount(), 1);

	if (MassSubsystem->GetManagedAntCount() != 1 || MassSubsystem->GetManagedFoodCount() != 1)
	{
		return false;
	}

	FRandomStream RandomStream(Plan.RandomSeedBase);
	const FVector ExpectedTurnDirection = ComputeAntTurnDirection(
		FVector(1.0f, 0.0f, 0.0f),
		RandomStream.FRandRange(-1.0f, 1.0f),
		PI / 2.0f);
	const FVector PickupEncounterPoint(8.0f, 0.0f, 0.0f);
	const FVector ExpectedLocationAfterSecondTick = PickupEncounterPoint + ExpectedTurnDirection * 10.0f;

	MassSubsystem->RunSimulationProcessorsForTesting(0.1f);
	MassSubsystem->RunSimulationProcessorsForTesting(0.1f);

	UMassEntitySubsystem* MassEntitySubsystem = World->GetSubsystem<UMassEntitySubsystem>();
	TestNotNull(TEXT("Mass entity subsystem should exist"), MassEntitySubsystem);
	if (MassEntitySubsystem == nullptr)
	{
		return false;
	}

	FMassEntityManager& EntityManager = MassEntitySubsystem->GetMutableEntityManager();
	FMassEntityView AntView(EntityManager, MassSubsystem->ManagedAntEntities[0]);
	const FGatherersMassAntFragment& AntFragment = AntView.GetFragmentData<FGatherersMassAntFragment>();
	TestTrue(
		TEXT("pickup should consume the stored Mass turn state and move on the turned heading next frame"),
		AntFragment.Position.Equals(ExpectedLocationAfterSecondTick, 1.0f));

	MassSubsystem->ResetSimulation();
	return true;
}
