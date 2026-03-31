#include "Actors/Ant.h"
#include "Editor.h"
#include "MassEntitySubsystem.h"
#include "MassEntityView.h"
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
	Plan.PlayAreaBounds = FBox(FVector(-100.0f, -100.0f, -100.0f), FVector(100.0f, 100.0f, 100.0f));
	Plan.AntSpawns.Add(FTransform(FVector(95.0f, 0.0f, 0.0f)));
	Plan.AntInitialDirections.Add(FVector(1.0f, 0.0f, 0.0f));

	const FGatherersSpawnResult Result = SpawnGatherersActors(*World, Plan);
	TestEqual(TEXT("spawned ant actor count"), Result.Ants.Num(), 0);
	TestEqual(TEXT("managed ant count"), MassSubsystem->GetManagedAntCount(), 1);

	if (MassSubsystem->GetManagedAntCount() != 1)
	{
		return false;
	}

	TestTrue(
		TEXT("Mass subsystem should store the active shared play area bounds for the simulation"),
		MassSubsystem->GetSimulationBounds().Min.Equals(Plan.PlayAreaBounds.Min, KINDA_SMALL_NUMBER)
			&& MassSubsystem->GetSimulationBounds().Max.Equals(Plan.PlayAreaBounds.Max, KINDA_SMALL_NUMBER));

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
		TEXT("Mass-backed ant should stay within the play area and head back inward after the boundary hit"),
		AntFragment.Position.Equals(FVector(90.0f, 0.0f, 0.0f), 1.0f));

	MassSubsystem->ResetSimulation();
	return true;
}
