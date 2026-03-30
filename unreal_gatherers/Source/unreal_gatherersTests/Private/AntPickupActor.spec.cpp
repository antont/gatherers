#include "Actors/Ant.h"
#include "Actors/Food.h"
#include "Editor.h"
#include "Misc/AutomationTest.h"
#include "Simulation/GatherersSpawnPlan.h"
#include "Simulation/GatherersWorldSpawner.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGatherersAntPickupActorAutomationTest,
	"unreal_gatherers.Simulation.AntMovesAndPicksUpFoodInWorld",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGatherersAntPickupActorAutomationTest::RunTest(const FString& Parameters)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	TestNotNull(TEXT("editor world should exist"), World);

	if (World == nullptr)
	{
		return false;
	}

	const FGatherersSpawnResult Result = SpawnGatherersActors(*World, BuildInitialGatherersSpawnPlan());
	TestEqual(TEXT("spawned ant count"), Result.Ants.Num(), 1);
	TestEqual(TEXT("spawned food count"), Result.Foods.Num(), 1);

	if (Result.Ants.Num() != 1 || Result.Foods.Num() != 1)
	{
		return false;
	}

	AAnt* Ant = Result.Ants[0];
	AFood* Food = Result.Foods[0];
	const FVector InitialAntLocation = Ant->GetActorLocation();
	const FVector InitialFoodLocation = Food->GetActorLocation();

	for (int32 Step = 0; Step < 30; ++Step)
	{
		Ant->Tick(0.1f);
	}

	TestFalse(
		TEXT("ant moves from its spawn location"),
		Ant->GetActorLocation().Equals(InitialAntLocation, KINDA_SMALL_NUMBER));
	TestTrue(TEXT("food attaches to the ant after pickup"), Food->GetAttachParentActor() == Ant);
	TestFalse(
		TEXT("food no longer remains at its initial world location"),
		Food->GetActorLocation().Equals(InitialFoodLocation, KINDA_SMALL_NUMBER));

	if (Food)
	{
		Food->Destroy();
	}

	if (Ant)
	{
		Ant->Destroy();
	}

	return true;
}
