#include "Actors/Ant.h"
#include "Actors/Food.h"
#include "Editor.h"
#include "Misc/AutomationTest.h"
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
	Plan.bUseMassSimulation = true;
	Plan.PlayAreaBounds = FBox(FVector(-500.0f, -500.0f, -100.0f), FVector(500.0f, 500.0f, 100.0f));
	Plan.AntSpawns.Add(FTransform(FVector::ZeroVector));
	Plan.AntInitialDirections.Add(FVector(1.0f, 0.0f, 0.0f));
	Plan.FoodSpawns.Add(FTransform(FVector(8.0f, 0.0f, 0.0f)));
	Plan.FoodSpawns.Add(FTransform(FVector(65.0f, 0.0f, 0.0f)));
	Plan.FoodSpawns.Add(FTransform(FVector(108.0f, 0.0f, 0.0f)));

	const FGatherersSpawnResult Result = SpawnGatherersActors(*World, Plan);
	TestEqual(TEXT("spawned ant proxies"), Result.Ants.Num(), 1);
	TestEqual(TEXT("spawned food proxies"), Result.Foods.Num(), 3);

	if (Result.Ants.Num() != 1 || Result.Foods.Num() != 3)
	{
		return false;
	}

	for (int32 StepIndex = 0; StepIndex < 11; ++StepIndex)
	{
		MassSubsystem->Tick(0.1f);
	}

	int32 AttachedFoodCount = 0;
	for (AFood* Food : Result.Foods)
	{
		if (Food != nullptr && Food->GetAttachParentActor() == Result.Ants[0])
		{
			++AttachedFoodCount;
		}
	}

	TestEqual(TEXT("one food should be carried again after the cooldown window"), AttachedFoodCount, 1);

	Result.Ants[0]->Destroy();
	for (AFood* Food : Result.Foods)
	{
		if (Food != nullptr)
		{
			Food->Destroy();
		}
	}

	MassSubsystem->ResetSimulation();
	return true;
}
