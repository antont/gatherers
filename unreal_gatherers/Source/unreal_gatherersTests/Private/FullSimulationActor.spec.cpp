#include "Editor.h"
#include "MassEntitySubsystem.h"
#include "MassEntityView.h"
#include "Misc/AutomationTest.h"
#include "Simulation/GatherersMassSubsystem.h"
#include "Simulation/GatherersSpawnPlan.h"
#include "Simulation/GatherersWorldSpawner.h"
#include "unreal_gatherers/unreal_gatherersGameModeBase.h"

namespace
{
UGatherersMassSubsystem* RequireMassSubsystem(FAutomationTestBase& Test, UWorld* World)
{
	Test.TestNotNull(TEXT("editor world should exist"), World);
	if (World == nullptr)
	{
		return nullptr;
	}

	UGatherersMassSubsystem* MassSubsystem = World->GetSubsystem<UGatherersMassSubsystem>();
	Test.TestNotNull(TEXT("gatherers Mass subsystem should exist"), MassSubsystem);
	if (MassSubsystem != nullptr)
	{
		MassSubsystem->ResetSimulation();
	}

	return MassSubsystem;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGatherersFullSimulationIgnoresOffPathFoodAutomationTest,
	"default.unreal_gatherers.FullSimulationActorFixture.AntKeepsHeadingWhenFoodIsOffPath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGatherersFullSimulationIgnoresOffPathFoodAutomationTest::RunTest(const FString& Parameters)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	UGatherersMassSubsystem* MassSubsystem = RequireMassSubsystem(*this, World);
	if (MassSubsystem == nullptr)
	{
		return false;
	}

	FGatherersSpawnPlan Plan;
	Plan.bUseFullSimulationMode = true;
	Plan.bSpawnActorVisuals = false;
	Plan.PlayAreaBounds = FBox(FVector(-500.0f, -500.0f, -100.0f), FVector(500.0f, 500.0f, 100.0f));
	Plan.AntSpawns.Add(FTransform(FVector::ZeroVector));
	Plan.AntInitialDirections.Add(FVector(1.0f, 0.0f, 0.0f));
	Plan.FoodSpawns.Add(FTransform(FVector(0.0f, 200.0f, 0.0f)));

	const FGatherersSpawnResult Result = SpawnGatherersActors(*World, Plan);
	TestEqual(TEXT("full-sim spawned ant actor count"), Result.Ants.Num(), 0);
	TestEqual(TEXT("full-sim spawned food actor count"), Result.Foods.Num(), 0);
	TestEqual(TEXT("full-sim managed ant count"), MassSubsystem->GetManagedAntCount(), 1);
	TestEqual(TEXT("full-sim managed food count"), MassSubsystem->GetManagedFoodCount(), 1);

	if (MassSubsystem->GetManagedAntCount() != 1 || MassSubsystem->GetManagedFoodCount() != 1)
	{
		return false;
	}

	for (int32 StepIndex = 0; StepIndex < 5; ++StepIndex)
	{
		MassSubsystem->RunSimulationProcessorsForTesting(0.1f);
	}

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
		TEXT("full-sim ant keeps moving forward through the processor-driven path instead of steering toward off-path food"),
		AntFragment.Position.Equals(FVector(50.0f, 0.0f, 0.0f), 1.0f));

	MassSubsystem->ResetSimulation();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGatherersFullSimulationBorderTurnBackAutomationTest,
	"default.unreal_gatherers.FullSimulationActorFixture.AntTurnsBackAtPlayAreaBorder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGatherersFullSimulationBorderTurnBackAutomationTest::RunTest(const FString& Parameters)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	UGatherersMassSubsystem* MassSubsystem = RequireMassSubsystem(*this, World);
	if (MassSubsystem == nullptr)
	{
		return false;
	}

	FGatherersSpawnPlan Plan;
	Plan.bUseFullSimulationMode = true;
	Plan.bSpawnActorVisuals = false;
	Plan.PlayAreaBounds = FBox(FVector(-10.0f, -100.0f, -100.0f), FVector(10.0f, 100.0f, 100.0f));
	Plan.AntSpawns.Add(FTransform(FVector::ZeroVector));
	Plan.AntInitialDirections.Add(FVector(1.0f, 0.0f, 0.0f));

	const FGatherersSpawnResult Result = SpawnGatherersActors(*World, Plan);
	TestEqual(TEXT("full-sim border spawned ant actor count"), Result.Ants.Num(), 0);
	TestEqual(TEXT("full-sim border managed ant count"), MassSubsystem->GetManagedAntCount(), 1);

	if (MassSubsystem->GetManagedAntCount() != 1)
	{
		return false;
	}

	MassSubsystem->Tick(0.1f);
	MassSubsystem->Tick(0.1f);
	MassSubsystem->Tick(0.1f);

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
		TEXT("full-sim ant stays inside the play area after touching the border"),
		AntFragment.Position.X <= 10.0f + KINDA_SMALL_NUMBER);

	MassSubsystem->ResetSimulation();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGatherersFullSimulationHighSpeedPickupAutomationTest,
	"default.unreal_gatherers.FullSimulationActorFixture.HighSpeedMovementStillPicksUpWithoutTunneling",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGatherersFullSimulationHighSpeedSweptPickupAutomationTest,
	"default.unreal_gatherers.FullSimulationActorFixture.HighSpeedMovementPicksUpFoodAlongSweptPath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGatherersFullSimulationFastWorldTimeSweptPickupAutomationTest,
	"default.unreal_gatherers.FullSimulationActorFixture.FastWorldTimeStillPicksUpFoodAlongSweptPath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGatherersFullSimulationHighSpeedPickupAutomationTest::RunTest(const FString& Parameters)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	UGatherersMassSubsystem* MassSubsystem = RequireMassSubsystem(*this, World);
	if (MassSubsystem == nullptr)
	{
		return false;
	}

	FGatherersSpawnPlan Plan;
	Plan.bUseFullSimulationMode = true;
	Plan.bSpawnActorVisuals = false;
	Plan.PlayAreaBounds = FBox(FVector(-500.0f, -500.0f, -100.0f), FVector(500.0f, 500.0f, 100.0f));
	Plan.AntSpawns.Add(FTransform(FVector::ZeroVector));
	Plan.AntInitialDirections.Add(FVector(1.0f, 0.0f, 0.0f));
	Plan.FoodSpawns.Add(FTransform(FVector(15.0f, 0.0f, 0.0f)));
	Plan.FullSimulationMovementSpeed = 5000.0f;
	Plan.FullSimulationTurnJitterRadians = 0.0f;

	const FGatherersSpawnResult Result = SpawnGatherersActors(*World, Plan);
	TestEqual(TEXT("high-speed spawned ant actor count"), Result.Ants.Num(), 0);
	TestEqual(TEXT("high-speed spawned food actor count"), Result.Foods.Num(), 0);
	TestEqual(TEXT("high-speed managed ant count"), MassSubsystem->GetManagedAntCount(), 1);
	TestEqual(TEXT("high-speed managed food count"), MassSubsystem->GetManagedFoodCount(), 1);

	if (MassSubsystem->GetManagedAntCount() != 1 || MassSubsystem->GetManagedFoodCount() != 1)
	{
		return false;
	}

	MassSubsystem->Tick(1.0f);

	UMassEntitySubsystem* MassEntitySubsystem = World->GetSubsystem<UMassEntitySubsystem>();
	TestNotNull(TEXT("Mass entity subsystem should exist"), MassEntitySubsystem);
	if (MassEntitySubsystem == nullptr)
	{
		return false;
	}

	FMassEntityManager& EntityManager = MassEntitySubsystem->GetMutableEntityManager();
	FMassEntityView AntView(EntityManager, MassSubsystem->ManagedAntEntities[0]);
	FMassEntityView FoodView(EntityManager, MassSubsystem->ManagedFoodEntities[0]);
	const FGatherersMassAntFragment& AntFragment = AntView.GetFragmentData<FGatherersMassAntFragment>();
	const FGatherersMassFoodFragment& FoodFragment = FoodView.GetFragmentData<FGatherersMassFoodFragment>();
	TestTrue(
		TEXT("safe-step movement still lets a nearby food be picked up during a long high-speed frame"),
		AntFragment.CarriedFoodEntity == MassSubsystem->ManagedFoodEntities[0] && !FoodFragment.bIsLoose);

	MassSubsystem->ResetSimulation();
	return true;
}

bool FGatherersFullSimulationHighSpeedSweptPickupAutomationTest::RunTest(const FString& Parameters)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	UGatherersMassSubsystem* MassSubsystem = RequireMassSubsystem(*this, World);
	if (MassSubsystem == nullptr)
	{
		return false;
	}

	FGatherersSpawnPlan Plan;
	Plan.bUseFullSimulationMode = true;
	Plan.bSpawnActorVisuals = false;
	Plan.PlayAreaBounds = FBox(FVector(-500.0f, -500.0f, -100.0f), FVector(500.0f, 500.0f, 100.0f));
	Plan.AntSpawns.Add(FTransform(FVector::ZeroVector));
	Plan.AntInitialDirections.Add(FVector(1.0f, 0.0f, 0.0f));
	Plan.FoodSpawns.Add(FTransform(FVector(0.0f, 14.0f, 0.0f)));
	Plan.FullSimulationMovementSpeed = 5000.0f;
	Plan.FullSimulationTurnJitterRadians = 0.0f;

	SpawnGatherersActors(*World, Plan);
	if (MassSubsystem->GetManagedAntCount() != 1 || MassSubsystem->GetManagedFoodCount() != 1)
	{
		return false;
	}

	MassSubsystem->Tick(1.0f);

	UMassEntitySubsystem* MassEntitySubsystem = World->GetSubsystem<UMassEntitySubsystem>();
	TestNotNull(TEXT("Mass entity subsystem should exist"), MassEntitySubsystem);
	if (MassEntitySubsystem == nullptr)
	{
		return false;
	}

	FMassEntityManager& EntityManager = MassEntitySubsystem->GetMutableEntityManager();
	FMassEntityView AntView(EntityManager, MassSubsystem->ManagedAntEntities[0]);
	FMassEntityView FoodView(EntityManager, MassSubsystem->ManagedFoodEntities[0]);
	const FGatherersMassAntFragment& AntFragment = AntView.GetFragmentData<FGatherersMassAntFragment>();
	const FGatherersMassFoodFragment& FoodFragment = FoodView.GetFragmentData<FGatherersMassFoodFragment>();
	TestTrue(
		TEXT("high-speed pickup should detect loose food intersected by the ant's traveled path even when the endpoint sphere no longer overlaps"),
		AntFragment.CarriedFoodEntity == MassSubsystem->ManagedFoodEntities[0] && !FoodFragment.bIsLoose);

	MassSubsystem->ResetSimulation();
	return true;
}

bool FGatherersFullSimulationFastWorldTimeSweptPickupAutomationTest::RunTest(const FString& Parameters)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	UGatherersMassSubsystem* MassSubsystem = RequireMassSubsystem(*this, World);
	if (MassSubsystem == nullptr)
	{
		return false;
	}

	Aunreal_gatherersGameModeBase::ApplyTimeControlModeToWorld(*World, EGatherersTimeControlMode::Fast);

	FGatherersSpawnPlan Plan;
	Plan.bUseFullSimulationMode = true;
	Plan.bSpawnActorVisuals = false;
	Plan.PlayAreaBounds = FBox(FVector(-500.0f, -500.0f, -100.0f), FVector(500.0f, 500.0f, 100.0f));
	Plan.AntSpawns.Add(FTransform(FVector::ZeroVector));
	Plan.AntInitialDirections.Add(FVector(1.0f, 0.0f, 0.0f));
	Plan.FoodSpawns.Add(FTransform(FVector(0.0f, 14.0f, 0.0f)));
	Plan.FullSimulationMovementSpeed = 5000.0f;
	Plan.FullSimulationTurnJitterRadians = 0.0f;

	SpawnGatherersActors(*World, Plan);
	if (MassSubsystem->GetManagedAntCount() != 1 || MassSubsystem->GetManagedFoodCount() != 1)
	{
		return false;
	}

	const float RealSeconds = 0.25f;
	MassSubsystem->Tick(RealSeconds);

	UMassEntitySubsystem* MassEntitySubsystem = World->GetSubsystem<UMassEntitySubsystem>();
	TestNotNull(TEXT("Mass entity subsystem should exist"), MassEntitySubsystem);
	if (MassEntitySubsystem == nullptr)
	{
		return false;
	}

	FMassEntityManager& EntityManager = MassEntitySubsystem->GetMutableEntityManager();
	FMassEntityView AntView(EntityManager, MassSubsystem->ManagedAntEntities[0]);
	FMassEntityView FoodView(EntityManager, MassSubsystem->ManagedFoodEntities[0]);
	const FGatherersMassAntFragment& AntFragment = AntView.GetFragmentData<FGatherersMassAntFragment>();
	const FGatherersMassFoodFragment& FoodFragment = FoodView.GetFragmentData<FGatherersMassFoodFragment>();
	TestTrue(
		TEXT("fast sim rate should keep swept-path pickup behavior correct for long simulated movement"),
		AntFragment.CarriedFoodEntity == MassSubsystem->ManagedFoodEntities[0] && !FoodFragment.bIsLoose);

	MassSubsystem->ResetSimulation();
	Aunreal_gatherersGameModeBase::ApplyTimeControlModeToWorld(*World, EGatherersTimeControlMode::Normal);
	return true;
}
