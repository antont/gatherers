#include "Actors/Ant.h"
#include "Actors/Food.h"
#include "Editor.h"
#include "Misc/AutomationTest.h"
#include "Simulation/GatherersMassSubsystem.h"
#include "Simulation/GatherersSpawnPlan.h"
#include "Simulation/GatherersWorldSpawner.h"

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

void DestroySpawnedActors(const FGatherersSpawnResult& Result)
{
	for (AAnt* Ant : Result.Ants)
	{
		if (Ant != nullptr)
		{
			Ant->Destroy();
		}
	}

	for (AFood* Food : Result.Foods)
	{
		if (Food != nullptr)
		{
			Food->Destroy();
		}
	}
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
	Plan.PlayAreaBounds = FBox(FVector(-500.0f, -500.0f, -100.0f), FVector(500.0f, 500.0f, 100.0f));
	Plan.AntSpawns.Add(FTransform(FVector::ZeroVector));
	Plan.AntInitialDirections.Add(FVector(1.0f, 0.0f, 0.0f));
	Plan.FoodSpawns.Add(FTransform(FVector(0.0f, 200.0f, 0.0f)));

	const FGatherersSpawnResult Result = SpawnGatherersActors(*World, Plan);
	TestEqual(TEXT("full-sim spawned ant count"), Result.Ants.Num(), 1);
	TestEqual(TEXT("full-sim spawned food count"), Result.Foods.Num(), 1);

	if (Result.Ants.Num() != 1 || Result.Foods.Num() != 1)
	{
		return false;
	}

	for (int32 StepIndex = 0; StepIndex < 5; ++StepIndex)
	{
		MassSubsystem->Tick(0.1f);
	}

	TestTrue(
		TEXT("full-sim ant keeps moving forward instead of steering toward off-path food"),
		Result.Ants[0]->GetActorLocation().Equals(FVector(50.0f, 0.0f, 0.0f), 1.0f));

	DestroySpawnedActors(Result);
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
	Plan.PlayAreaBounds = FBox(FVector(-10.0f, -100.0f, -100.0f), FVector(10.0f, 100.0f, 100.0f));
	Plan.AntSpawns.Add(FTransform(FVector::ZeroVector));
	Plan.AntInitialDirections.Add(FVector(1.0f, 0.0f, 0.0f));

	const FGatherersSpawnResult Result = SpawnGatherersActors(*World, Plan);
	TestEqual(TEXT("full-sim border spawned ant count"), Result.Ants.Num(), 1);

	if (Result.Ants.Num() != 1)
	{
		return false;
	}

	MassSubsystem->Tick(0.1f);
	MassSubsystem->Tick(0.1f);
	MassSubsystem->Tick(0.1f);

	TestTrue(
		TEXT("full-sim ant stays inside the play area after touching the border"),
		Result.Ants[0]->GetActorLocation().X <= 10.0f + KINDA_SMALL_NUMBER);

	DestroySpawnedActors(Result);
	MassSubsystem->ResetSimulation();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGatherersFullSimulationHighSpeedPickupAutomationTest,
	"default.unreal_gatherers.FullSimulationActorFixture.HighSpeedMovementStillPicksUpWithoutTunneling",
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
	Plan.PlayAreaBounds = FBox(FVector(-500.0f, -500.0f, -100.0f), FVector(500.0f, 500.0f, 100.0f));
	Plan.AntSpawns.Add(FTransform(FVector::ZeroVector));
	Plan.AntInitialDirections.Add(FVector(1.0f, 0.0f, 0.0f));
	Plan.FoodSpawns.Add(FTransform(FVector(15.0f, 0.0f, 0.0f)));
	Plan.FullSimulationMovementSpeed = 5000.0f;
	Plan.FullSimulationTurnJitterRadians = 0.0f;

	const FGatherersSpawnResult Result = SpawnGatherersActors(*World, Plan);
	TestEqual(TEXT("high-speed spawned ant count"), Result.Ants.Num(), 1);
	TestEqual(TEXT("high-speed spawned food count"), Result.Foods.Num(), 1);

	if (Result.Ants.Num() != 1 || Result.Foods.Num() != 1)
	{
		return false;
	}

	MassSubsystem->Tick(1.0f);

	TestTrue(
		TEXT("safe-step movement still lets a nearby food be picked up during a long high-speed frame"),
		Result.Foods[0]->GetAttachParentActor() == Result.Ants[0]);

	DestroySpawnedActors(Result);
	MassSubsystem->ResetSimulation();
	return true;
}
