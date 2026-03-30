#include "Misc/AutomationTest.h"
#include "Simulation/GatherersSpawnPlan.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGatherersSpawnPlanAutomationTest,
	"unreal_gatherers.Spawning.SpawnPlanDefinesOneAntAndOneFood",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGatherersSpawnPlanAutomationTest::RunTest(const FString& Parameters)
{
	const FGatherersSpawnPlan Plan = BuildInitialGatherersSpawnPlan();

	TestEqual(TEXT("ant spawn count"), Plan.AntSpawns.Num(), 1);
	TestEqual(TEXT("food spawn count"), Plan.FoodSpawns.Num(), 1);
	return true;
}
