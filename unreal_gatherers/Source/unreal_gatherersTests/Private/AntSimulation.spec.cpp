#include "Math/Vector.h"
#include "Misc/AutomationTest.h"
#include "Simulation/GatherersAntSimulation.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGatherersAntMovementStepAutomationTest,
	"default.unreal_gatherers.Simulation.AntMovementStepMovesTowardFood",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGatherersAntMovementStepAutomationTest::RunTest(const FString& Parameters)
{
	const FVector CurrentLocation(-50.0f, 0.0f, 0.0f);
	const FVector FoodLocation(50.0f, 0.0f, 0.0f);

	const FVector NextLocation = ComputeAntNextLocation(CurrentLocation, FoodLocation, 100.0f, 0.1f);

	TestTrue(
		TEXT("ant moves ten units toward food in one step"),
		NextLocation.Equals(FVector(-40.0f, 0.0f, 0.0f), KINDA_SMALL_NUMBER));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGatherersAntPickupRadiusAutomationTest,
	"default.unreal_gatherers.Simulation.AntPickupTriggersWithinRadius",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGatherersAntPickupRadiusAutomationTest::RunTest(const FString& Parameters)
{
	const FVector AntLocation(0.0f, 0.0f, 0.0f);
	const FVector FoodLocation(12.0f, 0.0f, 0.0f);

	const bool bShouldPickup = ShouldAntPickUpFood(AntLocation, FoodLocation, 15.0f);

	TestTrue(TEXT("ant picks up food when it is within pickup radius"), bShouldPickup);
	return true;
}
