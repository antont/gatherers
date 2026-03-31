#include "Editor.h"
#include "MassEntitySubsystem.h"
#include "MassEntityView.h"
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

bool AnyFoodAtLocation(const TArray<FMassEntityHandle>& FoodEntities, FMassEntityManager& EntityManager, const FVector& ExpectedLocation)
{
	for (const FMassEntityHandle FoodEntity : FoodEntities)
	{
		if (!EntityManager.IsEntityValid(FoodEntity))
		{
			continue;
		}

		FMassEntityView FoodView(EntityManager, FoodEntity);
		const FGatherersMassFoodFragment& FoodFragment = FoodView.GetFragmentData<FGatherersMassFoodFragment>();
		if (FoodFragment.Position.Equals(ExpectedLocation, 1.0f))
		{
			return true;
		}
	}

	return false;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGatherersFullSimulationFirstDropAutomationTest,
	"default.unreal_gatherers.FullSimulationActorFixture.FirstDropLeavesFoodLooseAndPreservesFoodCount",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGatherersFullSimulationFirstDropAutomationTest::RunTest(const FString& Parameters)
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
	Plan.PlayAreaBounds = FBox(FVector(-100.0f, -100.0f, -100.0f), FVector(100.0f, 100.0f, 100.0f));
	Plan.RandomSeedBase = 123;
	Plan.FullSimulationTurnJitterRadians = 0.0f;
	Plan.AntSpawns.Add(FTransform(FVector::ZeroVector));
	Plan.AntInitialDirections.Add(FVector(1.0f, 0.0f, 0.0f));
	Plan.FoodSpawns.Add(FTransform(FVector(8.0f, 0.0f, 0.0f)));
	Plan.FoodSpawns.Add(FTransform(FVector(-10.0f, 0.0f, 0.0f)));
	Plan.FoodSpawns.Add(FTransform(FVector(50.0f, 0.0f, 0.0f)));

	const FGatherersSpawnResult Result = SpawnGatherersActors(*World, Plan);
	TestEqual(TEXT("full-sim observation ant actor count"), Result.Ants.Num(), 0);
	TestEqual(TEXT("full-sim observation food actor count"), Result.Foods.Num(), 0);
	TestEqual(TEXT("full-sim observation managed ant count"), MassSubsystem->GetManagedAntCount(), 1);
	TestEqual(TEXT("full-sim observation managed food count"), MassSubsystem->GetManagedFoodCount(), 3);

	if (MassSubsystem->GetManagedAntCount() != 1 || MassSubsystem->GetManagedFoodCount() != 3)
	{
		return false;
	}

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
	int32 LooseFoodCount = 0;
	for (const FMassEntityHandle FoodEntity : MassSubsystem->ManagedFoodEntities)
	{
		FMassEntityView FoodView(EntityManager, FoodEntity);
		const FGatherersMassFoodFragment& FoodFragment = FoodView.GetFragmentData<FGatherersMassFoodFragment>();
		if (FoodFragment.bIsLoose)
		{
			++LooseFoodCount;
		}
	}

	TestEqual(TEXT("food count stays constant after pickup then first drop"), MassSubsystem->GetManagedFoodCount(), 3);
	TestEqual(TEXT("first drop leaves all food loose again"), LooseFoodCount, 3);
	TestTrue(
		TEXT("one loose food stays near the ant after the first drop"),
		AnyFoodAtLocation(MassSubsystem->ManagedFoodEntities, EntityManager, AntFragment.Position));

	MassSubsystem->ResetSimulation();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGatherersFullSimulationCooldownAutomationTest,
	"default.unreal_gatherers.FullSimulationActorFixture.CooldownBlocksImmediateRepickupAndAllowsLaterPickup",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGatherersFullSimulationCooldownAutomationTest::RunTest(const FString& Parameters)
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
	Plan.PlayAreaBounds = FBox(FVector(-100.0f, -100.0f, -100.0f), FVector(100.0f, 100.0f, 100.0f));
	Plan.RandomSeedBase = 123;
	Plan.FullSimulationTurnJitterRadians = 0.0f;
	Plan.AntSpawns.Add(FTransform(FVector::ZeroVector));
	Plan.AntInitialDirections.Add(FVector(1.0f, 0.0f, 0.0f));
	Plan.FoodSpawns.Add(FTransform(FVector(8.0f, 0.0f, 0.0f)));
	Plan.FoodSpawns.Add(FTransform(FVector(-10.0f, 0.0f, 0.0f)));
	Plan.FoodSpawns.Add(FTransform(FVector(50.0f, 0.0f, 0.0f)));

	const FGatherersSpawnResult Result = SpawnGatherersActors(*World, Plan);
	TestEqual(TEXT("full-sim cooldown ant actor count"), Result.Ants.Num(), 0);
	TestEqual(TEXT("full-sim cooldown food actor count"), Result.Foods.Num(), 0);
	TestEqual(TEXT("full-sim cooldown managed ant count"), MassSubsystem->GetManagedAntCount(), 1);
	TestEqual(TEXT("full-sim cooldown managed food count"), MassSubsystem->GetManagedFoodCount(), 3);

	if (MassSubsystem->GetManagedAntCount() != 1 || MassSubsystem->GetManagedFoodCount() != 3)
	{
		return false;
	}

	for (int32 StepIndex = 0; StepIndex < 3; ++StepIndex)
	{
		MassSubsystem->Tick(0.1f);
	}

	UMassEntitySubsystem* MassEntitySubsystem = World->GetSubsystem<UMassEntitySubsystem>();
	TestNotNull(TEXT("Mass entity subsystem should exist"), MassEntitySubsystem);
	if (MassEntitySubsystem == nullptr)
	{
		return false;
	}

	FMassEntityManager& EntityManager = MassEntitySubsystem->GetMutableEntityManager();
	int32 LooseFoodCount = 0;
	for (const FMassEntityHandle FoodEntity : MassSubsystem->ManagedFoodEntities)
	{
		FMassEntityView FoodView(EntityManager, FoodEntity);
		const FGatherersMassFoodFragment& FoodFragment = FoodView.GetFragmentData<FGatherersMassFoodFragment>();
		if (FoodFragment.bIsLoose)
		{
			++LooseFoodCount;
		}
	}
	TestEqual(TEXT("cooldown blocks immediate re-pickup right after the first drop"), LooseFoodCount, 3);

	for (int32 StepIndex = 0; StepIndex < 5; ++StepIndex)
	{
		MassSubsystem->Tick(0.1f);
	}

	LooseFoodCount = 0;
	for (const FMassEntityHandle FoodEntity : MassSubsystem->ManagedFoodEntities)
	{
		FMassEntityView FoodView(EntityManager, FoodEntity);
		const FGatherersMassFoodFragment& FoodFragment = FoodView.GetFragmentData<FGatherersMassFoodFragment>();
		if (FoodFragment.bIsLoose)
		{
			++LooseFoodCount;
		}
	}
	TestEqual(TEXT("after cooldown the ant can gather again"), LooseFoodCount, 2);

	MassSubsystem->ResetSimulation();
	return true;
}
