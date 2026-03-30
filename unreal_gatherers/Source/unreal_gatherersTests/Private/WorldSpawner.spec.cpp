#include "Actors/Ant.h"
#include "Actors/Food.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"
#include "Simulation/GatherersSpawnPlan.h"
#include "Simulation/GatherersWorldSpawner.h"

namespace
{
template <typename ActorType>
TArray<ActorType*> CollectActors(UWorld* World)
{
	TArray<ActorType*> Actors;
	for (TActorIterator<ActorType> It(World); It; ++It)
	{
		Actors.Add(*It);
	}

	return Actors;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGatherersWorldSpawnerAutomationTest,
	"default.unreal_gatherers.Spawning.WorldSpawnerCreatesAntAndTwoFoodActors",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGatherersWorldSpawnerAutomationTest::RunTest(const FString& Parameters)
{
	constexpr float PositionTolerance = KINDA_SMALL_NUMBER;
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	TestNotNull(TEXT("editor world should exist"), World);

	if (World == nullptr)
	{
		return false;
	}

	const FGatherersSpawnPlan Plan = BuildInitialGatherersSpawnPlan();
	AAnt* ExistingAnt = World->SpawnActor<AAnt>(AAnt::StaticClass(), FTransform(FVector(-100.0f, 0.0f, 50.0f)));
	AFood* ExistingFood = World->SpawnActor<AFood>(AFood::StaticClass(), FTransform(FVector(-200.0f, 0.0f, 50.0f)));
	TestNotNull(TEXT("preexisting ant should spawn"), ExistingAnt);
	TestNotNull(TEXT("preexisting food should spawn"), ExistingFood);

	const TArray<AAnt*> AntsBeforeSpawn = CollectActors<AAnt>(World);
	const TArray<AFood*> FoodsBeforeSpawn = CollectActors<AFood>(World);
	const FGatherersSpawnResult Result = SpawnGatherersActors(*World, Plan);
	const TArray<AAnt*> AntsInWorld = CollectActors<AAnt>(World);
	const TArray<AFood*> FoodsInWorld = CollectActors<AFood>(World);

	TestEqual(TEXT("spawned ant count"), Result.Ants.Num(), 1);
	TestEqual(TEXT("spawned food count"), Result.Foods.Num(), 2);
	TestEqual(TEXT("editor world ant count increases by one"), AntsInWorld.Num(), AntsBeforeSpawn.Num() + 1);
	TestEqual(TEXT("editor world food count increases by two"), FoodsInWorld.Num(), FoodsBeforeSpawn.Num() + 2);

	if (Result.Ants.Num() == 1)
	{
		TestTrue(TEXT("spawned ant type"), Result.Ants[0]->IsA<AAnt>());
		TestFalse(TEXT("returned ant did not exist before spawn"), AntsBeforeSpawn.Contains(Result.Ants[0]));
		TestTrue(TEXT("returned ant exists in editor world"), AntsInWorld.Contains(Result.Ants[0]));
		TestTrue(TEXT("spawned ant world matches editor world"), Result.Ants[0]->GetWorld() == World);
		TestTrue(
			TEXT("spawned ant location matches spawn plan"),
			Result.Ants[0]->GetActorLocation().Equals(Plan.AntSpawns[0].GetLocation(), PositionTolerance));
	}

	if (Result.Foods.Num() == 2)
	{
		TestTrue(TEXT("spawned first food type"), Result.Foods[0]->IsA<AFood>());
		TestTrue(TEXT("spawned second food type"), Result.Foods[1]->IsA<AFood>());
		TestFalse(TEXT("returned first food did not exist before spawn"), FoodsBeforeSpawn.Contains(Result.Foods[0]));
		TestFalse(TEXT("returned second food did not exist before spawn"), FoodsBeforeSpawn.Contains(Result.Foods[1]));
		TestTrue(TEXT("returned first food exists in editor world"), FoodsInWorld.Contains(Result.Foods[0]));
		TestTrue(TEXT("returned second food exists in editor world"), FoodsInWorld.Contains(Result.Foods[1]));
		TestTrue(TEXT("first spawned food world matches editor world"), Result.Foods[0]->GetWorld() == World);
		TestTrue(TEXT("second spawned food world matches editor world"), Result.Foods[1]->GetWorld() == World);
		TestTrue(
			TEXT("first spawned food location matches spawn plan"),
			Result.Foods[0]->GetActorLocation().Equals(Plan.FoodSpawns[0].GetLocation(), PositionTolerance));
		TestTrue(
			TEXT("second spawned food location matches spawn plan"),
			Result.Foods[1]->GetActorLocation().Equals(Plan.FoodSpawns[1].GetLocation(), PositionTolerance));
	}

	for (AAnt* Ant : Result.Ants)
	{
		if (Ant)
		{
			Ant->Destroy();
		}
	}

	for (AFood* Food : Result.Foods)
	{
		if (Food)
		{
			Food->Destroy();
		}
	}

	return true;
}
