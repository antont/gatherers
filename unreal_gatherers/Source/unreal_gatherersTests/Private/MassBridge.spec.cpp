#include "Actors/Ant.h"
#include "Actors/Food.h"
#include "Editor.h"
#include "MassEntitySubsystem.h"
#include "MassEntityView.h"
#include "Misc/AutomationTest.h"
#include "Simulation/GatherersMassSubsystem.h"
#include "Simulation/GatherersSpawnPlan.h"
#include "Simulation/GatherersWorldSpawner.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGatherersMassBridgeAutomationTest,
	"default.unreal_gatherers.Mass.WorldSpawnerRegistersMassBackedFullSimState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGatherersMassProxyAuthorityAutomationTest,
	"default.unreal_gatherers.Mass.FullSimProxyDoesNotOwnActorRuntimeState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGatherersMassRepresentationBridgeAutomationTest,
	"default.unreal_gatherers.Mass.EntitiesAdvanceWithoutActorVisualProxies",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGatherersFoodQueryChannelAutomationTest,
	"default.unreal_gatherers.Mass.FoodRepresentationUsesDedicatedFoodQueryChannel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGatherersMassBridgeAutomationTest::RunTest(const FString& Parameters)
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
	Plan.bSpawnActorVisuals = true;
	Plan.PlayAreaBounds = FBox(FVector(-500.0f, -500.0f, -100.0f), FVector(500.0f, 500.0f, 100.0f));
	Plan.AntSpawns.Add(FTransform(FVector::ZeroVector));
	Plan.AntInitialDirections.Add(FVector(1.0f, 0.0f, 0.0f));
	Plan.FoodSpawns.Add(FTransform(FVector(100.0f, 0.0f, 0.0f)));
	Plan.FoodSpawns.Add(FTransform(FVector(-100.0f, 0.0f, 0.0f)));

	const FGatherersSpawnResult Result = SpawnGatherersActors(*World, Plan);
	TestEqual(TEXT("spawned ant proxies"), Result.Ants.Num(), 1);
	TestEqual(TEXT("spawned food proxies"), Result.Foods.Num(), 2);
	TestEqual(TEXT("Mass subsystem tracks one ant entity"), MassSubsystem->GetManagedAntCount(), 1);
	TestEqual(TEXT("Mass subsystem tracks two food entities"), MassSubsystem->GetManagedFoodCount(), 2);

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

	MassSubsystem->ResetSimulation();
	return true;
}

bool FGatherersMassProxyAuthorityAutomationTest::RunTest(const FString& Parameters)
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
	Plan.bSpawnActorVisuals = true;
	Plan.PlayAreaBounds = FBox(FVector(-500.0f, -500.0f, -100.0f), FVector(500.0f, 500.0f, 100.0f));
	Plan.AntSpawns.Add(FTransform(FVector::ZeroVector));
	Plan.AntInitialDirections.Add(FVector(1.0f, 0.0f, 0.0f));
	Plan.FoodSpawns.Add(FTransform(FVector(0.0f, 200.0f, 0.0f)));

	const FGatherersSpawnResult Result = SpawnGatherersActors(*World, Plan);
	TestEqual(TEXT("spawned ant proxies"), Result.Ants.Num(), 1);

	if (Result.Ants.Num() != 1)
	{
		return false;
	}

	TestTrue(
		TEXT("Mass-backed proxy actor should have tick disabled so it does not move independently"),
		!Result.Ants[0]->IsActorTickEnabled());

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

	MassSubsystem->ResetSimulation();
	return true;
}

bool FGatherersMassRepresentationBridgeAutomationTest::RunTest(const FString& Parameters)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	TestNotNull(TEXT("editor world should exist"), World);

	if (World == nullptr)
	{
		return false;
	}

	UGatherersMassSubsystem* MassSubsystem = World->GetSubsystem<UGatherersMassSubsystem>();
	TestNotNull(TEXT("gatherers Mass subsystem should exist"), MassSubsystem);

	UMassEntitySubsystem* MassEntitySubsystem = World->GetSubsystem<UMassEntitySubsystem>();
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
	Plan.FoodSpawns.Add(FTransform(FVector(100.0f, 0.0f, 0.0f)));
	Plan.bSpawnActorVisuals = false;

	const FGatherersSpawnResult Result = SpawnGatherersActors(*World, Plan);
	TestEqual(TEXT("Mass-instanced visual path should not spawn ant actors"), Result.Ants.Num(), 0);
	TestEqual(TEXT("Mass-instanced visual path should not spawn food actors"), Result.Foods.Num(), 0);
	TestEqual(TEXT("Mass subsystem tracks one ant entity"), MassSubsystem->GetManagedAntCount(), 1);
	TestEqual(TEXT("Mass subsystem tracks one food entity"), MassSubsystem->GetManagedFoodCount(), 1);

	if (MassSubsystem->ManagedAntEntities.Num() != 1)
	{
		return false;
	}

	FMassEntityManager& EntityManager = MassEntitySubsystem->GetMutableEntityManager();
	FMassEntityView AntView(EntityManager, MassSubsystem->ManagedAntEntities[0]);
	TestNotNull(
		TEXT("Mass ant path should expose a dedicated ant instanced visual component"),
		const_cast<UInstancedStaticMeshComponent*>(MassSubsystem->GetAntVisualComponent()));
	TestNotNull(
		TEXT("Mass food path should expose a dedicated food instanced visual component"),
		const_cast<UInstancedStaticMeshComponent*>(MassSubsystem->GetFoodRepresentationComponent()));

	MassSubsystem->RunSimulationProcessorsForTesting(0.1f);

	const FGatherersMassAntFragment& AntFragment = AntView.GetFragmentData<FGatherersMassAntFragment>();
	TestTrue(
		TEXT("Mass ant can still advance through the processor-driven path without spawned actor proxies"),
		AntFragment.Position.Equals(FVector(10.0f, 0.0f, 0.0f), 1.0f));

	MassSubsystem->ResetSimulation();
	return true;
}

bool FGatherersFoodQueryChannelAutomationTest::RunTest(const FString& Parameters)
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
	Plan.FoodSpawns.Add(FTransform(FVector(100.0f, 0.0f, 0.0f)));

	SpawnGatherersActors(*World, Plan);

	const UInstancedStaticMeshComponent* FoodRepresentation = MassSubsystem->GetFoodRepresentationComponent();
	TestNotNull(
		TEXT("food representation component should exist"),
		const_cast<UInstancedStaticMeshComponent*>(FoodRepresentation));

	if (FoodRepresentation == nullptr)
	{
		return false;
	}

	TestEqual(
		TEXT("food representation should be query-only"),
		FoodRepresentation->GetCollisionEnabled(),
		ECollisionEnabled::QueryOnly);
	TestEqual(
		TEXT("food representation should block the dedicated FoodQuery trace channel"),
		FoodRepresentation->GetCollisionResponseToChannel(ECC_GameTraceChannel1),
		ECR_Block);
	TestEqual(
		TEXT("food representation should ignore Visibility now that pickup uses FoodQuery"),
		FoodRepresentation->GetCollisionResponseToChannel(ECC_Visibility),
		ECR_Ignore);

	MassSubsystem->ResetSimulation();
	return true;
}
