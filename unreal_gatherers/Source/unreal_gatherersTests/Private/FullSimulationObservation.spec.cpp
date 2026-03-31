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

int32 CountAttachedFoods(const TArray<AFood*>& Foods)
{
	int32 AttachedFoodCount = 0;
	for (AFood* Food : Foods)
	{
		if (Food != nullptr && Food->GetAttachParentActor() != nullptr)
		{
			++AttachedFoodCount;
		}
	}

	return AttachedFoodCount;
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
	Plan.PlayAreaBounds = FBox(FVector(-100.0f, -100.0f, -100.0f), FVector(100.0f, 100.0f, 100.0f));
	Plan.RandomSeedBase = 123;
	Plan.FullSimulationTurnJitterRadians = 0.0f;
	Plan.AntSpawns.Add(FTransform(FVector::ZeroVector));
	Plan.AntInitialDirections.Add(FVector(1.0f, 0.0f, 0.0f));
	Plan.FoodSpawns.Add(FTransform(FVector(8.0f, 0.0f, 0.0f)));
	Plan.FoodSpawns.Add(FTransform(FVector(-10.0f, 0.0f, 0.0f)));
	Plan.FoodSpawns.Add(FTransform(FVector(50.0f, 0.0f, 0.0f)));

	const FGatherersSpawnResult Result = SpawnGatherersActors(*World, Plan);
	TestEqual(TEXT("full-sim observation ant count"), Result.Ants.Num(), 1);
	TestEqual(TEXT("full-sim observation food count"), Result.Foods.Num(), 3);

	if (Result.Ants.Num() != 1 || Result.Foods.Num() != 3)
	{
		return false;
	}

	MassSubsystem->Tick(0.1f);
	MassSubsystem->Tick(0.1f);

	TestEqual(TEXT("food count stays constant after pickup then first drop"), Result.Foods.Num(), 3);
	TestEqual(TEXT("first drop leaves all food loose again"), CountAttachedFoods(Result.Foods), 0);
	TestTrue(
		TEXT("one loose food stays near the ant after the first drop"),
		Result.Foods[0]->GetActorLocation().Equals(Result.Ants[0]->GetActorLocation(), 1.0f));

	DestroySpawnedActors(Result);
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
	Plan.PlayAreaBounds = FBox(FVector(-100.0f, -100.0f, -100.0f), FVector(100.0f, 100.0f, 100.0f));
	Plan.RandomSeedBase = 123;
	Plan.FullSimulationTurnJitterRadians = 0.0f;
	Plan.AntSpawns.Add(FTransform(FVector::ZeroVector));
	Plan.AntInitialDirections.Add(FVector(1.0f, 0.0f, 0.0f));
	Plan.FoodSpawns.Add(FTransform(FVector(8.0f, 0.0f, 0.0f)));
	Plan.FoodSpawns.Add(FTransform(FVector(-10.0f, 0.0f, 0.0f)));
	Plan.FoodSpawns.Add(FTransform(FVector(50.0f, 0.0f, 0.0f)));

	const FGatherersSpawnResult Result = SpawnGatherersActors(*World, Plan);
	TestEqual(TEXT("full-sim cooldown ant count"), Result.Ants.Num(), 1);
	TestEqual(TEXT("full-sim cooldown food count"), Result.Foods.Num(), 3);

	if (Result.Ants.Num() != 1 || Result.Foods.Num() != 3)
	{
		return false;
	}

	for (int32 StepIndex = 0; StepIndex < 3; ++StepIndex)
	{
		MassSubsystem->Tick(0.1f);
	}

	TestEqual(TEXT("cooldown blocks immediate re-pickup right after the first drop"), CountAttachedFoods(Result.Foods), 0);

	for (int32 StepIndex = 0; StepIndex < 5; ++StepIndex)
	{
		MassSubsystem->Tick(0.1f);
	}

	TestEqual(TEXT("after cooldown the ant can gather again"), CountAttachedFoods(Result.Foods), 1);

	DestroySpawnedActors(Result);
	MassSubsystem->ResetSimulation();
	return true;
}
