#include "Actors/Ant.h"
#include "Editor.h"
#include "Misc/AutomationTest.h"
#include "Simulation/GatherersMassSubsystem.h"
#include "Simulation/GatherersSpawnPlan.h"
#include "Simulation/GatherersWorldSpawner.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGatherersMassBoundaryAutomationTest,
	"default.unreal_gatherers.Mass.AntTurnsBackAtBoundaryInMassSimulation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGatherersMassBoundaryAutomationTest::RunTest(const FString& Parameters)
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
	Plan.PlayAreaBounds = FBox(FVector(-100.0f, -100.0f, -100.0f), FVector(100.0f, 100.0f, 100.0f));
	Plan.AntSpawns.Add(FTransform(FVector(95.0f, 0.0f, 0.0f)));
	Plan.AntInitialDirections.Add(FVector(1.0f, 0.0f, 0.0f));

	const FGatherersSpawnResult Result = SpawnGatherersActors(*World, Plan);
	TestEqual(TEXT("spawned ant proxies"), Result.Ants.Num(), 1);

	if (Result.Ants.Num() != 1)
	{
		return false;
	}

	MassSubsystem->Tick(0.1f);
	MassSubsystem->Tick(0.1f);

	TestTrue(
		TEXT("Mass-backed ant should stay within the play area and head back inward after the boundary hit"),
		Result.Ants[0]->GetActorLocation().Equals(FVector(90.0f, 0.0f, 0.0f), 1.0f));

	Result.Ants[0]->Destroy();
	MassSubsystem->ResetSimulation();
	return true;
}
