#include "Actors/Ant.h"
#include "Actors/Food.h"
#include "Editor.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"
#include "Simulation/GatherersSpawnPlan.h"
#include "TestLogic/GatherersWorldAssertions.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"

DEFINE_LATENT_AUTOMATION_COMMAND_THREE_PARAMETER(
	FGatherersWaitForSimulationPickupCommand,
	FAutomationTestBase*,
	Test,
	double,
	StartTimeSeconds,
	double,
	TimeoutSeconds);

DEFINE_LATENT_AUTOMATION_COMMAND_THREE_PARAMETER(
	FGatherersWaitForSimulationPIECleanupCommand,
	FAutomationTestBase*,
	Test,
	double,
	StartTimeSeconds,
	double,
	TimeoutSeconds);

DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(
	FGatherersQueueSecondSimulationRunCommand,
	FAutomationTestBase*,
	Test);

class FGatherersWaitForCarryMovementCommand : public IAutomationLatentCommand
{
public:
	FGatherersWaitForCarryMovementCommand(FAutomationTestBase* InTest, double InStartTimeSeconds, double InTimeoutSeconds)
		: Test(InTest),
		  StartTimeSeconds(InStartTimeSeconds),
		  TimeoutSeconds(InTimeoutSeconds)
	{
	}

	virtual bool Update() override
	{
		UWorld* World = GEditor ? GEditor->PlayWorld : nullptr;
		Test->TestNotNull(TEXT("simulation world should exist during carry movement"), World);
		if (World == nullptr)
		{
			return true;
		}

		const GatherersWorldAssertions::FObservedWorldState WorldState = GatherersWorldAssertions::Observe(World);
		AAnt* Ant = WorldState.GetSingleAnt();
		AFood* Food = WorldState.GetSingleFood();
		const bool bPickedUpFood = Ant != nullptr && Food != nullptr && Food->GetAttachParentActor() == Ant;

		if (bPickedUpFood && !PickupLocation.IsSet())
		{
			PickupLocation = Ant->GetActorLocation();
			return false;
		}

		if (bPickedUpFood && PickupLocation.IsSet())
		{
			const bool bAntMovedWhileCarrying =
				!Ant->GetActorLocation().Equals(PickupLocation.GetValue(), KINDA_SMALL_NUMBER);
			if (bAntMovedWhileCarrying)
			{
				Test->TestTrue(TEXT("ant keeps moving after pickup while carrying food"), true);
				return true;
			}
		}

		if (FPlatformTime::Seconds() - StartTimeSeconds < TimeoutSeconds)
		{
			return false;
		}

		Test->TestTrue(TEXT("ant keeps moving after pickup while carrying food"), false);
		return true;
	}

private:
	FAutomationTestBase* Test;
	double StartTimeSeconds;
	double TimeoutSeconds;
	TOptional<FVector> PickupLocation;
};

bool FGatherersWaitForSimulationPickupCommand::Update()
{
	return GatherersWorldAssertions::PollForPickupState(
		*Test,
		GEditor ? GEditor->PlayWorld : nullptr,
		BuildInitialGatherersSpawnPlan(),
		StartTimeSeconds,
		TimeoutSeconds,
		TEXT("simulation"));
}

bool FGatherersWaitForSimulationPIECleanupCommand::Update()
{
	return GatherersWorldAssertions::PollForPIEToEnd(
		*Test,
		TEXT("/Game/SimBlank/Levels/SimBlank"),
		StartTimeSeconds,
		TimeoutSeconds,
		TEXT("simulation"));
}

bool FGatherersQueueSecondSimulationRunCommand::Update()
{
	const bool bOpenedMap = AutomationOpenMap(TEXT("/Game/SimBlank/Levels/SimBlank"));
	Test->TestTrue(TEXT("should reopen SimBlank map for second simulation run"), bOpenedMap);

	if (!bOpenedMap)
	{
		return true;
	}

	ADD_LATENT_AUTOMATION_COMMAND(FGatherersWaitForSimulationPickupCommand(Test, FPlatformTime::Seconds(), 5.0));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	ADD_LATENT_AUTOMATION_COMMAND(FGatherersWaitForSimulationPIECleanupCommand(Test, FPlatformTime::Seconds(), 5.0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGatherersAntPickupActorAutomationTest,
	"default.unreal_gatherers.Simulation.AntMovesAndPicksUpFoodInWorld",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGatherersAntPickupActorAutomationTest::RunTest(const FString& Parameters)
{
	const bool bOpenedMap = AutomationOpenMap(TEXT("/Game/SimBlank/Levels/SimBlank"));
	TestTrue(TEXT("should open SimBlank map"), bOpenedMap);

	if (!bOpenedMap)
	{
		return false;
	}

	ADD_LATENT_AUTOMATION_COMMAND(FGatherersWaitForSimulationPickupCommand(this, FPlatformTime::Seconds(), 5.0));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	ADD_LATENT_AUTOMATION_COMMAND(FGatherersWaitForSimulationPIECleanupCommand(this, FPlatformTime::Seconds(), 5.0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGatherersAntPickupActorRerunAutomationTest,
	"default.unreal_gatherers.Simulation.AntMovesAndPicksUpFoodTwiceInSameEditorSession",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGatherersAntPickupActorRerunAutomationTest::RunTest(const FString& Parameters)
{
	const bool bOpenedMap = AutomationOpenMap(TEXT("/Game/SimBlank/Levels/SimBlank"));
	TestTrue(TEXT("should open SimBlank map"), bOpenedMap);

	if (!bOpenedMap)
	{
		return false;
	}

	ADD_LATENT_AUTOMATION_COMMAND(FGatherersWaitForSimulationPickupCommand(this, FPlatformTime::Seconds(), 5.0));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	ADD_LATENT_AUTOMATION_COMMAND(FGatherersWaitForSimulationPIECleanupCommand(this, FPlatformTime::Seconds(), 5.0));
	ADD_LATENT_AUTOMATION_COMMAND(FGatherersQueueSecondSimulationRunCommand(this));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGatherersAntCarryMovementAutomationTest,
	"default.unreal_gatherers.Simulation.AntKeepsMovingAfterPickup",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGatherersAntCarryMovementAutomationTest::RunTest(const FString& Parameters)
{
	const bool bOpenedMap = AutomationOpenMap(TEXT("/Game/SimBlank/Levels/SimBlank"));
	TestTrue(TEXT("should open SimBlank map"), bOpenedMap);

	if (!bOpenedMap)
	{
		return false;
	}

	ADD_LATENT_AUTOMATION_COMMAND(FGatherersWaitForCarryMovementCommand(this, FPlatformTime::Seconds(), 5.0));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	ADD_LATENT_AUTOMATION_COMMAND(FGatherersWaitForSimulationPIECleanupCommand(this, FPlatformTime::Seconds(), 5.0));
	return true;
}
