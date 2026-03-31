#include "Math/Box.h"
#include "Misc/AutomationTest.h"
#include "Simulation/GatherersSpawnPlan.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGatherersFullSimulationSpawnPlanAutomationTest,
	"default.unreal_gatherers.FullSimulation.SpawnPlanBuildsConfiguredAntAndFoodCounts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGatherersFullSimulationSpawnPlanAutomationTest::RunTest(const FString& Parameters)
{
	const FGatherersSpawnPlan Plan = BuildFullSimulationSpawnPlan(
		4,
		12,
		1234,
		FBox(FVector(-400.0f, -300.0f, 0.0f), FVector(400.0f, 300.0f, 100.0f)));

	TestEqual(TEXT("full-sim ant spawn count"), Plan.AntSpawns.Num(), 4);
	TestEqual(TEXT("full-sim ant heading count"), Plan.AntInitialDirections.Num(), 4);
	TestEqual(TEXT("full-sim food spawn count"), Plan.FoodSpawns.Num(), 12);
	TestTrue(TEXT("full-sim spawn plan is flagged for full-simulation configuration"), Plan.bUseFullSimulationMode);
	return true;
}
