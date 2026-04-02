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
	FGatherersMassEncounterPickupPositionAutomationTest,
	"default.unreal_gatherers.Mass.AntStopsAtFoodEncounterPointOnPickup",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGatherersMassEncounterDropPositionAutomationTest,
	"default.unreal_gatherers.Mass.AntStopsAtFoodEncounterPointOnDrop",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGatherersMassHighSpeedDropProximityAutomationTest,
	"default.unreal_gatherers.Mass.HighSpeedDropLandsNearTriggerFood",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGatherersMassEncounterPickupPositionAutomationTest::RunTest(const FString& Parameters)
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
	Plan.bSpawnActorVisuals = false;
	Plan.PlayAreaBounds = FBox(FVector(-500.0f, -500.0f, -100.0f), FVector(500.0f, 500.0f, 100.0f));
	Plan.AntSpawns.Add(FTransform(FVector::ZeroVector));
	Plan.AntInitialDirections.Add(FVector(1.0f, 0.0f, 0.0f));
	Plan.FoodSpawns.Add(FTransform(FVector(50.0f, 0.0f, 0.0f)));

	SpawnGatherersActors(*World, Plan);
	TestEqual(TEXT("managed ant count"), MassSubsystem->GetManagedAntCount(), 1);
	TestEqual(TEXT("managed food count"), MassSubsystem->GetManagedFoodCount(), 1);
	if (MassSubsystem->GetManagedAntCount() != 1 || MassSubsystem->GetManagedFoodCount() != 1)
	{
		return false;
	}

	// Large step: speed 100 * 2.0s = 200 units, but food is at 50.
	// Ant should stop at ~50, not continue to 200.
	MassSubsystem->RunSimulationProcessorsForTesting(2.0f);

	UMassEntitySubsystem* MassEntitySubsystem = World->GetSubsystem<UMassEntitySubsystem>();
	TestNotNull(TEXT("Mass entity subsystem should exist"), MassEntitySubsystem);
	if (MassEntitySubsystem == nullptr)
	{
		return false;
	}

	FMassEntityManager& EntityManager = MassEntitySubsystem->GetMutableEntityManager();
	FMassEntityView AntView(EntityManager, MassSubsystem->ManagedAntEntities[0]);
	const FGatherersMassAntFragment& AntFragment = AntView.GetFragmentData<FGatherersMassAntFragment>();

	TestTrue(TEXT("ant should have picked up the food"),
		AntFragment.CarriedFoodEntity == MassSubsystem->ManagedFoodEntities[0]);
	TestTrue(TEXT("ant should stop near the food encounter point (X ~ 50), not at end of full step"),
		FMath::Abs(AntFragment.Position.X - 50.0f) < 20.0f);
	TestTrue(TEXT("ant should NOT be at the full step distance"),
		AntFragment.Position.X < 100.0f);

	MassSubsystem->ResetSimulation();
	return true;
}

bool FGatherersMassEncounterDropPositionAutomationTest::RunTest(const FString& Parameters)
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

	// Ant at origin heading +X, food at (8, 0, 0).
	// After pickup in step 1, ant should stop at ~(8, 0, 0) and turn.
	// Place second food along the deterministic turn direction from ~(8, 0, 0).
	constexpr int32 Seed = 123;
	FRandomStream RandomStream(Seed);
	const FVector FirstTurnDirection = ComputeAntTurnDirection(
		FVector(1.0f, 0.0f, 0.0f),
		RandomStream.FRandRange(-1.0f, 1.0f),
		PI / 2.0f);

	// Second food at 10 units along the turn direction from encounter point (8, 0, 0)
	const FVector PickupEncounterPoint(8.0f, 0.0f, 0.0f);
	const FVector SecondFoodLocation = PickupEncounterPoint + FirstTurnDirection * 10.0f;

	FGatherersSpawnPlan Plan;
	Plan.bUseFullSimulationMode = true;
	Plan.bSpawnActorVisuals = false;
	Plan.RandomSeedBase = Seed;
	Plan.PlayAreaBounds = FBox(FVector(-500.0f, -500.0f, -100.0f), FVector(500.0f, 500.0f, 100.0f));
	Plan.AntSpawns.Add(FTransform(FVector::ZeroVector));
	Plan.AntInitialDirections.Add(FVector(1.0f, 0.0f, 0.0f));
	Plan.FoodSpawns.Add(FTransform(FVector(8.0f, 0.0f, 0.0f)));
	Plan.FoodSpawns.Add(FTransform(SecondFoodLocation));

	SpawnGatherersActors(*World, Plan);
	TestEqual(TEXT("managed ant count"), MassSubsystem->GetManagedAntCount(), 1);
	TestEqual(TEXT("managed food count"), MassSubsystem->GetManagedFoodCount(), 2);
	if (MassSubsystem->GetManagedAntCount() != 1 || MassSubsystem->GetManagedFoodCount() != 2)
	{
		return false;
	}

	// Step 1: pickup food A, ant should stop at encounter point
	MassSubsystem->RunSimulationProcessorsForTesting(0.1f);
	// Step 2: encounter food B, drop food A
	MassSubsystem->RunSimulationProcessorsForTesting(0.1f);

	UMassEntitySubsystem* MassEntitySubsystem = World->GetSubsystem<UMassEntitySubsystem>();
	TestNotNull(TEXT("Mass entity subsystem should exist"), MassEntitySubsystem);
	if (MassEntitySubsystem == nullptr)
	{
		return false;
	}

	FMassEntityManager& EntityManager = MassEntitySubsystem->GetMutableEntityManager();
	FMassEntityView FirstFoodView(EntityManager, MassSubsystem->ManagedFoodEntities[0]);
	const FGatherersMassFoodFragment& FirstFoodFragment = FirstFoodView.GetFragmentData<FGatherersMassFoodFragment>();

	TestTrue(TEXT("first food should be loose again after the drop"), FirstFoodFragment.bIsLoose);
	TestTrue(TEXT("dropped food should be near the second food (encounter point)"),
		FVector::Distance(FirstFoodFragment.Position, SecondFoodLocation) < 5.0f);

	FMassEntityView AntView(EntityManager, MassSubsystem->ManagedAntEntities[0]);
	const FGatherersMassAntFragment& AntFragment = AntView.GetFragmentData<FGatherersMassAntFragment>();
	TestTrue(TEXT("ant position should be near the second food encounter point"),
		FVector::Distance(AntFragment.Position, SecondFoodLocation) < 5.0f);

	MassSubsystem->ResetSimulation();
	return true;
}

bool FGatherersMassHighSpeedDropProximityAutomationTest::RunTest(const FString& Parameters)
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

	constexpr int32 Seed = 123;
	FRandomStream RandomStream(Seed);
	const FVector FirstTurnDirection = ComputeAntTurnDirection(
		FVector(1.0f, 0.0f, 0.0f),
		RandomStream.FRandRange(-1.0f, 1.0f),
		PI / 2.0f);

	// Food A at 50 units ahead, food B along turn path from encounter at (50, 0, 0)
	const FVector FoodALocation(50.0f, 0.0f, 0.0f);
	const FVector FoodBLocation = FoodALocation + FirstTurnDirection * 80.0f;

	FGatherersSpawnPlan Plan;
	Plan.bUseFullSimulationMode = true;
	Plan.bSpawnActorVisuals = false;
	Plan.RandomSeedBase = Seed;
	Plan.PlayAreaBounds = FBox(FVector(-500.0f, -500.0f, -100.0f), FVector(500.0f, 500.0f, 100.0f));
	Plan.AntSpawns.Add(FTransform(FVector::ZeroVector));
	Plan.AntInitialDirections.Add(FVector(1.0f, 0.0f, 0.0f));
	Plan.FoodSpawns.Add(FTransform(FoodALocation));
	Plan.FoodSpawns.Add(FTransform(FoodBLocation));

	SpawnGatherersActors(*World, Plan);
	TestEqual(TEXT("managed ant count"), MassSubsystem->GetManagedAntCount(), 1);
	TestEqual(TEXT("managed food count"), MassSubsystem->GetManagedFoodCount(), 2);
	if (MassSubsystem->GetManagedAntCount() != 1 || MassSubsystem->GetManagedFoodCount() != 2)
	{
		return false;
	}

	// Large steps simulating high-speed mode
	MassSubsystem->RunSimulationProcessorsForTesting(2.0f);
	MassSubsystem->RunSimulationProcessorsForTesting(2.0f);

	UMassEntitySubsystem* MassEntitySubsystem = World->GetSubsystem<UMassEntitySubsystem>();
	TestNotNull(TEXT("Mass entity subsystem should exist"), MassEntitySubsystem);
	if (MassEntitySubsystem == nullptr)
	{
		return false;
	}

	FMassEntityManager& EntityManager = MassEntitySubsystem->GetMutableEntityManager();
	FMassEntityView FoodAView(EntityManager, MassSubsystem->ManagedFoodEntities[0]);
	FMassEntityView FoodBView(EntityManager, MassSubsystem->ManagedFoodEntities[1]);
	const FGatherersMassFoodFragment& FoodAFragment = FoodAView.GetFragmentData<FGatherersMassFoodFragment>();
	const FGatherersMassFoodFragment& FoodBFragment = FoodBView.GetFragmentData<FGatherersMassFoodFragment>();

	TestTrue(TEXT("food A should be loose again after the drop"), FoodAFragment.bIsLoose);
	TestTrue(TEXT("food B should remain loose"), FoodBFragment.bIsLoose);

	const float DropDistance = FVector::Distance(FoodAFragment.Position, FoodBFragment.Position);
	TestTrue(
		FString::Printf(TEXT("dropped food A should land near trigger food B (distance: %.1f, max: 25.0)"), DropDistance),
		DropDistance < 25.0f);

	MassSubsystem->ResetSimulation();
	return true;
}
