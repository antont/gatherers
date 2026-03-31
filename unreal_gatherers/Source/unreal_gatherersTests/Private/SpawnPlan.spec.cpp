#include "Misc/AutomationTest.h"
#include "Simulation/GatherersSpawnPlan.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGatherersSpawnPlanAutomationTest,
	"default.unreal_gatherers.Spawning.SpawnPlanDefinesOneAntAndTwoFoods",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGatherersSpawnPlanAutomationTest::RunTest(const FString& Parameters)
{
	const FGatherersSpawnPlan Plan = BuildInitialGatherersSpawnPlan();

	TestEqual(TEXT("ant spawn count"), Plan.AntSpawns.Num(), 1);
	TestEqual(TEXT("food spawn count"), Plan.FoodSpawns.Num(), 2);
	TestTrue(
		TEXT("first food spawn is the initial forward target"),
		Plan.FoodSpawns[0].GetLocation().Equals(FVector(200.0f, 0.0f, 50.0f), KINDA_SMALL_NUMBER));
	TestTrue(
		TEXT("second food spawn sits behind the ant for the return path"),
		Plan.FoodSpawns[1].GetLocation().Equals(FVector(-200.0f, 0.0f, 50.0f), KINDA_SMALL_NUMBER));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGatherersDefaultGameFullSimulationSpawnPlanAutomationTest,
	"default.unreal_gatherers.FullSimulation.DefaultGameSpawnPlanUsesRustLikeDefaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGatherersDefaultGameFullSimulationSpawnPlanAutomationTest::RunTest(const FString& Parameters)
{
	const FGatherersSpawnPlan Plan = BuildDefaultGameFullSimulationSpawnPlan(
		FBox(FVector(-640.0f, -360.0f, -100.0f), FVector(640.0f, 360.0f, 100.0f)),
		123);

	TestTrue(TEXT("default game spawn plan uses full simulation mode"), Plan.bUseFullSimulationMode);
	TestEqual(TEXT("default game ant spawn count follows the Rust row spacing"), Plan.AntSpawns.Num(), 26);
	TestEqual(TEXT("default game ant heading count matches the spawned ants"), Plan.AntInitialDirections.Num(), 26);
	TestEqual(TEXT("default game food count follows the Rust default"), Plan.FoodSpawns.Num(), 80);
	TestTrue(
		TEXT("default game spawn plan starts the first ant at the left edge row position"),
		Plan.AntSpawns[0].GetLocation().Equals(FVector(-640.0f, 100.0f, 50.0f), KINDA_SMALL_NUMBER));
	TestTrue(
		TEXT("default game spawn plan uses the Rust ant row spacing"),
		Plan.AntSpawns[1].GetLocation().Equals(FVector(-590.0f, 100.0f, 50.0f), KINDA_SMALL_NUMBER));
	return true;
}
