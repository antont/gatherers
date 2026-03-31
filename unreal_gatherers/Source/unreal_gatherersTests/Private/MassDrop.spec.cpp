#include "Actors/Ant.h"
#include "Actors/Food.h"
#include "Editor.h"
#include "Misc/AutomationTest.h"
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
	Plan.bUseMassSimulation = true;
	Plan.PlayAreaBounds = FBox(FVector(-500.0f, -500.0f, -100.0f), FVector(500.0f, 500.0f, 100.0f));
	Plan.AntSpawns.Add(FTransform(FVector::ZeroVector));
	Plan.AntInitialDirections.Add(FVector(1.0f, 0.0f, 0.0f));
	Plan.FoodSpawns.Add(FTransform(FVector(8.0f, 0.0f, 0.0f)));
	Plan.FoodSpawns.Add(FTransform(FVector(26.0f, 0.0f, 0.0f)));

	const FGatherersSpawnResult Result = SpawnGatherersActors(*World, Plan);
	TestEqual(TEXT("spawned ant proxies"), Result.Ants.Num(), 1);
	TestEqual(TEXT("spawned food proxies"), Result.Foods.Num(), 2);

	if (Result.Ants.Num() != 1 || Result.Foods.Num() != 2)
	{
		return false;
	}

	MassSubsystem->Tick(0.1f);
	MassSubsystem->Tick(0.1f);

	TestTrue(TEXT("first food should be loose again after the drop"), Result.Foods[0]->GetAttachParentActor() == nullptr);
	TestTrue(TEXT("second food should stay loose in the world"), Result.Foods[1]->GetAttachParentActor() == nullptr);
	TestTrue(TEXT("dropped food should appear at the ant drop location"), Result.Foods[0]->GetActorLocation().Equals(FVector(20.0f, 0.0f, 0.0f), 1.0f));

	Result.Ants[0]->Destroy();
	Result.Foods[0]->Destroy();
	Result.Foods[1]->Destroy();
	MassSubsystem->ResetSimulation();
	return true;
}
