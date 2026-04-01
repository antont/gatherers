#include "Editor.h"
#include "MassEntitySubsystem.h"
#include "Misc/AutomationTest.h"
#include "Simulation/GatherersMassSubsystem.h"
#include "Simulation/GatherersSpawnPlan.h"
#include "Simulation/GatherersWorldSpawner.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGatherersMassLooseFoodQueryMapsInstancesAutomationTest,
	"default.unreal_gatherers.Mass.SharedFoodQueryMapsInstanceIndicesToLooseFoodEntities",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGatherersMassLooseFoodQuerySkipsCarriedFoodAutomationTest,
	"default.unreal_gatherers.Mass.SharedFoodQuerySkipsCarriedFoodInstances",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGatherersMassLooseFoodQueryMapsInstancesAutomationTest::RunTest(const FString& Parameters)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	TestNotNull(TEXT("editor world should exist"), World);

	if (World == nullptr)
	{
		return false;
	}

	UGatherersMassSubsystem* MassSubsystem = World->GetSubsystem<UGatherersMassSubsystem>();
	UMassEntitySubsystem* MassEntitySubsystem = World->GetSubsystem<UMassEntitySubsystem>();
	TestNotNull(TEXT("gatherers Mass subsystem should exist"), MassSubsystem);
	TestNotNull(TEXT("Mass entity subsystem should exist"), MassEntitySubsystem);

	if (MassSubsystem == nullptr || MassEntitySubsystem == nullptr)
	{
		return false;
	}

	MassSubsystem->ResetSimulation();

	FGatherersSpawnPlan Plan;
	Plan.bUseFullSimulationMode = true;
	Plan.PlayAreaBounds = FBox(FVector(-500.0f, -500.0f, -100.0f), FVector(500.0f, 500.0f, 100.0f));
	Plan.AntSpawns.Add(FTransform(FVector::ZeroVector));
	Plan.AntInitialDirections.Add(FVector(1.0f, 0.0f, 0.0f));
	Plan.FoodSpawns.Add(FTransform(FVector(0.0f, 200.0f, 0.0f)));
	Plan.FoodSpawns.Add(FTransform(FVector(0.0f, -200.0f, 0.0f)));

	SpawnGatherersActors(*World, Plan);

	const TArray<FMassEntityHandle> FirstFoodHits = MassSubsystem->QueryLooseFoodEntitiesOverlappingSphere(
		FVector(0.0f, 200.0f, 0.0f),
		25.0f);
	const TArray<FMassEntityHandle> SecondFoodHits = MassSubsystem->QueryLooseFoodEntitiesOverlappingSphere(
		FVector(0.0f, -200.0f, 0.0f),
		25.0f);
	const TArray<FMassEntityHandle> EmptyHits = MassSubsystem->QueryLooseFoodEntitiesOverlappingSphere(
		FVector(300.0f, 300.0f, 0.0f),
		25.0f);

	TestEqual(TEXT("first food query should return exactly one loose food entity"), FirstFoodHits.Num(), 1);
	TestEqual(TEXT("second food query should return exactly one loose food entity"), SecondFoodHits.Num(), 1);
	TestEqual(TEXT("empty query should return no loose food entities"), EmptyHits.Num(), 0);

	if (FirstFoodHits.Num() == 1)
	{
		TestTrue(
			TEXT("first food query should map back to the first managed food entity"),
			FirstFoodHits[0] == MassSubsystem->ManagedFoodEntities[0]);
	}

	if (SecondFoodHits.Num() == 1)
	{
		TestTrue(
			TEXT("second food query should map back to the second managed food entity"),
			SecondFoodHits[0] == MassSubsystem->ManagedFoodEntities[1]);
	}

	MassSubsystem->ResetSimulation();
	return true;
}

bool FGatherersMassLooseFoodQuerySkipsCarriedFoodAutomationTest::RunTest(const FString& Parameters)
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
	Plan.PlayAreaBounds = FBox(FVector(-500.0f, -500.0f, -100.0f), FVector(500.0f, 500.0f, 100.0f));
	Plan.AntSpawns.Add(FTransform(FVector::ZeroVector));
	Plan.AntInitialDirections.Add(FVector(1.0f, 0.0f, 0.0f));
	Plan.FoodSpawns.Add(FTransform(FVector(8.0f, 0.0f, 0.0f)));
	Plan.FoodSpawns.Add(FTransform(FVector(100.0f, 0.0f, 0.0f)));

	SpawnGatherersActors(*World, Plan);
	MassSubsystem->Tick(0.1f);

	const TArray<FMassEntityHandle> NearAntHits = MassSubsystem->QueryLooseFoodEntitiesOverlappingSphere(
		FVector::ZeroVector,
		25.0f);
	const TArray<FMassEntityHandle> FarFoodHits = MassSubsystem->QueryLooseFoodEntitiesOverlappingSphere(
		FVector(100.0f, 0.0f, 0.0f),
		25.0f);

	TestEqual(TEXT("query near the ant should not report the now-carried food as loose"), NearAntHits.Num(), 0);
	TestEqual(TEXT("query at the far loose food should still report one entity"), FarFoodHits.Num(), 1);

	if (FarFoodHits.Num() == 1)
	{
		TestTrue(
			TEXT("far loose food query should map back to the second managed food entity"),
			FarFoodHits[0] == MassSubsystem->ManagedFoodEntities[1]);
	}

	MassSubsystem->ResetSimulation();
	return true;
}
