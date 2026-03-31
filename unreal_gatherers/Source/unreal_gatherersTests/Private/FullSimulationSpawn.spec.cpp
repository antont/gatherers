#include "Math/Box.h"
#include "Actors/Ant.h"
#include "Actors/Food.h"
#include "Editor.h"
#include "Misc/AutomationTest.h"
#include "Simulation/GatherersMassSubsystem.h"
#include "Simulation/GatherersSpawnPlan.h"
#include "Simulation/GatherersWorldSpawner.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGatherersFullSimulationSpawnPlanAutomationTest,
	"default.unreal_gatherers.FullSimulation.SpawnPlanBuildsConfiguredAntAndFoodCounts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGatherersFullSimulationSpawnPlanAutomationTest::RunTest(const FString& Parameters)
{
	const FGatherersSpawnPlan Plan = BuildFullSimulationSpawnPlan(
		4,
		12,
		1234,
		FBox(FVector(-400.0f, -300.0f, 0.0f), FVector(400.0f, 300.0f, 100.0f)));

	TestEqual(TEXT("full-sim ant spawn count"), Plan.AntSpawns.Num(), 4);
	TestEqual(TEXT("full-sim ant heading count"), Plan.AntInitialDirections.Num(), 4);
	TestEqual(TEXT("full-sim food spawn count"), Plan.FoodSpawns.Num(), 12);
	TestTrue(TEXT("full-sim spawn plan is flagged for full-simulation configuration"), Plan.bUseFullSimulationMode);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGatherersFullSimulationSpawnerAutomationTest,
	"default.unreal_gatherers.FullSimulation.WorldSpawnerConfiguresAntsForFullSimMode",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGatherersFullSimulationSpawnerAutomationTest::RunTest(const FString& Parameters)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	TestNotNull(TEXT("editor world should exist"), World);

	if (World == nullptr)
	{
		return false;
	}

	FGatherersSpawnPlan Plan;
	Plan.bUseFullSimulationMode = true;
	Plan.PlayAreaBounds = FBox(FVector(-500.0f, -500.0f, -100.0f), FVector(500.0f, 500.0f, 100.0f));
	Plan.AntSpawns.Add(FTransform(FVector::ZeroVector));
	Plan.AntInitialDirections.Add(FVector(1.0f, 0.0f, 0.0f));
	Plan.FoodSpawns.Add(FTransform(FVector(0.0f, 200.0f, 0.0f)));

	UGatherersMassSubsystem* MassSubsystem = World->GetSubsystem<UGatherersMassSubsystem>();
	TestNotNull(TEXT("gatherers Mass subsystem should exist"), MassSubsystem);

	if (MassSubsystem == nullptr)
	{
		return false;
	}

	MassSubsystem->ResetSimulation();

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
		TEXT("world spawner routes spawned full-sim ant through Mass-backed heading movement instead of actor food seeking"),
		Result.Ants[0]->GetActorLocation().Equals(FVector(50.0f, 0.0f, 0.0f), 1.0f));

	Result.Ants[0]->Destroy();
	Result.Foods[0]->Destroy();
	MassSubsystem->ResetSimulation();
	return true;
}
