#include "Actors/Ant.h"
#include "Actors/Food.h"
#include "Editor.h"
#include "Misc/AutomationTest.h"

namespace
{
TArray<AFood*> SpawnFoods(UWorld& World, std::initializer_list<FVector> Locations)
{
	TArray<AFood*> Foods;
	for (const FVector& Location : Locations)
	{
		if (AFood* Food = World.SpawnActor<AFood>(AFood::StaticClass(), FTransform(Location)))
		{
			Foods.Add(Food);
		}
	}

	return Foods;
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
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGatherersFullSimulationFirstDropAutomationTest,
	"default.unreal_gatherers.FullSimulation.FirstDropLeavesFoodLooseAndPreservesFoodCount",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGatherersFullSimulationFirstDropAutomationTest::RunTest(const FString& Parameters)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	TestNotNull(TEXT("editor world should exist"), World);

	if (World == nullptr)
	{
		return false;
	}

	AAnt* Ant = World->SpawnActor<AAnt>(AAnt::StaticClass(), FTransform(FVector::ZeroVector));
	TArray<AFood*> Foods = SpawnFoods(*World, {FVector(8.0f, 0.0f, 0.0f), FVector(-10.0f, 0.0f, 0.0f), FVector(-50.0f, 0.0f, 0.0f)});
	TestNotNull(TEXT("full-sim observation ant should spawn"), Ant);
	TestEqual(TEXT("full-sim observation food count"), Foods.Num(), 3);

	if (Ant == nullptr || Foods.Num() != 3)
	{
		return false;
	}

	Ant->ConfigureForFullSimulation(FVector(1.0f, 0.0f, 0.0f), FBox(FVector(-100.0f, -100.0f, -100.0f), FVector(100.0f, 100.0f, 100.0f)), 123);
	Ant->SetFullSimulationTurnJitterRadians(0.0f);

	Ant->Tick(0.1f);
	Ant->Tick(0.1f);

	TestEqual(TEXT("food count stays constant after pickup then first drop"), Foods.Num(), 3);
	TestEqual(TEXT("first drop leaves all food loose again"), CountAttachedFoods(Foods), 0);
	TestTrue(
		TEXT("one loose food stays near the ant after the first drop"),
		Foods[0]->GetActorLocation().Equals(Ant->GetActorLocation(), 1.0f));

	Ant->Destroy();
	for (AFood* Food : Foods)
	{
		if (Food != nullptr)
		{
			Food->Destroy();
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGatherersFullSimulationCooldownAutomationTest,
	"default.unreal_gatherers.FullSimulation.CooldownBlocksImmediateRepickupAndAllowsLaterPickup",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGatherersFullSimulationCooldownAutomationTest::RunTest(const FString& Parameters)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	TestNotNull(TEXT("editor world should exist"), World);

	if (World == nullptr)
	{
		return false;
	}

	AAnt* Ant = World->SpawnActor<AAnt>(AAnt::StaticClass(), FTransform(FVector::ZeroVector));
	TArray<AFood*> Foods = SpawnFoods(*World, {FVector(8.0f, 0.0f, 0.0f), FVector(-10.0f, 0.0f, 0.0f), FVector(-50.0f, 0.0f, 0.0f)});
	TestNotNull(TEXT("full-sim cooldown ant should spawn"), Ant);
	TestEqual(TEXT("full-sim cooldown food count"), Foods.Num(), 3);

	if (Ant == nullptr || Foods.Num() != 3)
	{
		return false;
	}

	Ant->ConfigureForFullSimulation(FVector(1.0f, 0.0f, 0.0f), FBox(FVector(-100.0f, -100.0f, -100.0f), FVector(100.0f, 100.0f, 100.0f)), 123);
	Ant->SetFullSimulationTurnJitterRadians(0.0f);

	for (int32 StepIndex = 0; StepIndex < 3; ++StepIndex)
	{
		Ant->Tick(0.1f);
	}

	TestEqual(TEXT("cooldown blocks immediate re-pickup right after the first drop"), CountAttachedFoods(Foods), 0);

	for (int32 StepIndex = 0; StepIndex < 4; ++StepIndex)
	{
		Ant->Tick(0.1f);
	}

	TestEqual(TEXT("after cooldown the ant can gather again"), CountAttachedFoods(Foods), 1);

	Ant->Destroy();
	for (AFood* Food : Foods)
	{
		if (Food != nullptr)
		{
			Food->Destroy();
		}
	}

	return true;
}
