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
