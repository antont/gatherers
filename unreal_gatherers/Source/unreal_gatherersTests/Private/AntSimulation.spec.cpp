#include "Math/Vector.h"
#include "Misc/AutomationTest.h"
#include "Simulation/GatherersAntSimulation.h"

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGatherersAntRetargetDirectionAutomationTest,
	"default.unreal_gatherers.Simulation.AntRetargetDirectionReversesHeadingOnPickup",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGatherersAntRetargetDirectionAutomationTest::RunTest(const FString& Parameters)
{
	const FVector CurrentDirection(1.0f, 0.0f, 0.0f);
	const FVector RetargetedDirection = ComputeAntRetargetDirection(CurrentDirection, 0.0f);

	TestTrue(
		TEXT("ant reverses heading when retarget jitter is zero"),
		RetargetedDirection.Equals(FVector(-1.0f, 0.0f, 0.0f), KINDA_SMALL_NUMBER));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGatherersPickupCooldownAutomationTest,
	"default.unreal_gatherers.Simulation.AntPickupCooldownCountsDownAndClampsToZero",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGatherersPickupCooldownAutomationTest::RunTest(const FString& Parameters)
{
	const float RemainingCooldown = ComputeRemainingPickupCooldown(0.2f, 0.5f);

	TestEqual(TEXT("pickup cooldown clamps to zero instead of going negative"), RemainingCooldown, 0.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGatherersCarriedFoodOffsetAutomationTest,
	"default.unreal_gatherers.Simulation.CarriedFoodOffsetMatchesConfiguredHeight",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGatherersCarriedFoodOffsetAutomationTest::RunTest(const FString& Parameters)
{
	const FVector CarriedFoodOffset = ComputeCarriedFoodRelativeLocation(20.0f);

	TestTrue(
		TEXT("carried food sits directly above the ant at the configured height"),
		CarriedFoodOffset.Equals(FVector(0.0f, 0.0f, 20.0f), KINDA_SMALL_NUMBER));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGatherersHeadingMovementStepAutomationTest,
	"default.unreal_gatherers.Simulation.FullSimHeadingMovementUsesSafeStepCap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGatherersHeadingMovementStepAutomationTest::RunTest(const FString& Parameters)
{
	const FVector CurrentLocation(0.0f, 0.0f, 0.0f);
	const FVector HeadingDirection(1.0f, 0.0f, 0.0f);

	const FVector NextLocation = ComputeAntHeadingMovementStep(CurrentLocation, HeadingDirection, 1000.0f, 18.0f, 0.1f);

	TestTrue(
		TEXT("full-sim movement clamps to the safe step distance instead of tunneling past it"),
		NextLocation.Equals(FVector(18.0f, 0.0f, 0.0f), KINDA_SMALL_NUMBER));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGatherersFullSimTurnDirectionAutomationTest,
	"default.unreal_gatherers.Simulation.FullSimTurnDirectionUsesAboutFacePlusBoundedJitter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGatherersFullSimTurnDirectionAutomationTest::RunTest(const FString& Parameters)
{
	const FVector CurrentDirection(1.0f, 0.0f, 0.0f);
	const FVector TurnedDirection = ComputeAntTurnDirection(CurrentDirection, 1.0f, PI / 2.0f);

	TestTrue(
		TEXT("full-sim pickup/drop turn can rotate up to the positive jitter bound around an about-face turn"),
		TurnedDirection.Equals(FVector(0.0f, -1.0f, 0.0f), KINDA_SMALL_NUMBER));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGatherersPickupSeparationCooldownAutomationTest,
	"default.unreal_gatherers.Simulation.FullSimPickupCooldownPreservesDropSeparationDistance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGatherersPickupSeparationCooldownAutomationTest::RunTest(const FString& Parameters)
{
	const float CooldownSeconds = ComputePickupCooldownForSeparationDistance(50.0f, 100.0f);

	TestEqual(
		TEXT("full-sim cooldown can be derived from the desired separation distance and movement speed"),
		CooldownSeconds,
		0.5f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGatherersBoundaryTurnBackAutomationTest,
	"default.unreal_gatherers.Simulation.FullSimBoundaryTurnBackReflectsHeadingInward",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGatherersBoundaryTurnBackAutomationTest::RunTest(const FString& Parameters)
{
	const FVector CurrentDirection(1.0f, 0.0f, 0.0f);
	const FVector InwardBoundaryNormal(-1.0f, 0.0f, 0.0f);

	const FVector TurnedDirection = ComputeBoundaryTurnBackDirection(CurrentDirection, InwardBoundaryNormal);

	TestTrue(
		TEXT("boundary turn-back reflects the heading back into the play area"),
		TurnedDirection.Equals(FVector(-1.0f, 0.0f, 0.0f), KINDA_SMALL_NUMBER));
	return true;
}
