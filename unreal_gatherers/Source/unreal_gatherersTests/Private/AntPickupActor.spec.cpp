#include "Actors/Ant.h"
#include "Actors/Food.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"
#include "Simulation/GatherersSpawnPlan.h"
#include "Simulation/GatherersWorldSpawner.h"
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

DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(
	FGatherersQueueSecondSimulationRunCommand,
	FAutomationTestBase*,
	Test);

DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(
	FGatherersQueueSecondDropSimulationRunCommand,
	FAutomationTestBase*,
	Test);

class FGatherersPrepareDeterministicSimulationFixtureCommand : public IAutomationLatentCommand
{
public:
	explicit FGatherersPrepareDeterministicSimulationFixtureCommand(FAutomationTestBase* InTest)
		: Test(InTest)
	{
	}

	virtual bool Update() override
	{
		UWorld* World = GEditor ? GEditor->PlayWorld : nullptr;
		Test->TestNotNull(TEXT("simulation world should exist for deterministic fixture setup"), World);
		if (World == nullptr)
		{
			return true;
		}

		const GatherersWorldAssertions::FObservedWorldState ExistingWorldState = GatherersWorldAssertions::Observe(World);
		for (AAnt* Ant : ExistingWorldState.Ants)
		{
			if (Ant != nullptr)
			{
				Ant->Destroy();
			}
		}

		for (AFood* Food : ExistingWorldState.Foods)
		{
			if (Food != nullptr)
			{
				Food->Destroy();
			}
		}

		const FGatherersSpawnResult SpawnResult = SpawnGatherersActors(*World, BuildInitialGatherersSpawnPlan());
		Test->TestEqual(TEXT("deterministic simulation fixture ant count"), SpawnResult.Ants.Num(), 1);
		Test->TestEqual(TEXT("deterministic simulation fixture food count"), SpawnResult.Foods.Num(), 2);
		return true;
	}

private:
	FAutomationTestBase* Test;
};

class FGatherersWaitForSimulationPIECleanupCommand : public IAutomationLatentCommand
{
public:
	FGatherersWaitForSimulationPIECleanupCommand(FAutomationTestBase* InTest, double InTimeoutSeconds)
		: Test(InTest),
		  TimeoutSeconds(InTimeoutSeconds)
	{
	}

	virtual bool Update() override
	{
		if (!StartTimeSeconds.IsSet())
		{
			StartTimeSeconds = FPlatformTime::Seconds();
		}

		return GatherersWorldAssertions::PollForPIEToEnd(
			*Test,
			TEXT("/Game/SimBlank/Levels/SimBlank"),
			StartTimeSeconds.GetValue(),
			TimeoutSeconds,
			TEXT("simulation"));
	}

private:
	FAutomationTestBase* Test;
	double TimeoutSeconds;
	TOptional<double> StartTimeSeconds;
};

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
		AFood* AttachedFood = WorldState.GetFirstAttachedFood();
		const bool bPickedUpFood = Ant != nullptr && AttachedFood != nullptr && AttachedFood->GetAttachParentActor() == Ant
			&& WorldState.CountAttachedFoods() == 1;

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

class FGatherersWaitForDropStateCommand : public IAutomationLatentCommand
{
public:
	FGatherersWaitForDropStateCommand(FAutomationTestBase* InTest, double InStartTimeSeconds, double InTimeoutSeconds)
		: Test(InTest),
		  StartTimeSeconds(InStartTimeSeconds),
		  TimeoutSeconds(InTimeoutSeconds)
	{
	}

	virtual bool Update() override
	{
		UWorld* World = GEditor ? GEditor->PlayWorld : nullptr;
		Test->TestNotNull(TEXT("simulation world should exist during drop verification"), World);
		if (World == nullptr)
		{
			return true;
		}

		const FGatherersSpawnPlan Plan = BuildInitialGatherersSpawnPlan();
		const GatherersWorldAssertions::FObservedWorldState WorldState = GatherersWorldAssertions::Observe(World);
		const bool bHasOneAntAndTwoFoods = WorldState.Ants.Num() == 1 && WorldState.Foods.Num() == 2;
		bool bAnyFoodAttached = false;
		bool bDroppedFoodLeftInitialForwardSpawn = false;

		for (AFood* Food : WorldState.Foods)
		{
			if (Food == nullptr)
			{
				continue;
			}

			bAnyFoodAttached |= Food->GetAttachParentActor() != nullptr;
			bDroppedFoodLeftInitialForwardSpawn |=
				!Food->GetActorLocation().Equals(Plan.FoodSpawns[0].GetLocation(), KINDA_SMALL_NUMBER);
		}

		if (bAnyFoodAttached)
		{
			bSawAttachedFood = true;
		}

		if (bHasOneAntAndTwoFoods && bSawAttachedFood && !bAnyFoodAttached && bDroppedFoodLeftInitialForwardSpawn)
		{
			Test->TestTrue(TEXT("carried food becomes loose again after the return path"), true);
			return true;
		}

		if (FPlatformTime::Seconds() - StartTimeSeconds < TimeoutSeconds)
		{
			return false;
		}

		Test->TestTrue(TEXT("carried food becomes loose again after the return path"), false);
		return true;
	}

private:
	FAutomationTestBase* Test;
	double StartTimeSeconds;
	double TimeoutSeconds;
	bool bSawAttachedFood = false;
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

bool FGatherersQueueSecondSimulationRunCommand::Update()
{
	const bool bOpenedMap = AutomationOpenMap(TEXT("/Game/SimBlank/Levels/SimBlank"));
	Test->TestTrue(TEXT("should reopen SimBlank map for second simulation run"), bOpenedMap);

	if (!bOpenedMap)
	{
		return true;
	}

	ADD_LATENT_AUTOMATION_COMMAND(FGatherersPrepareDeterministicSimulationFixtureCommand(Test));
	ADD_LATENT_AUTOMATION_COMMAND(FGatherersWaitForSimulationPickupCommand(Test, FPlatformTime::Seconds(), 5.0));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	ADD_LATENT_AUTOMATION_COMMAND(FGatherersWaitForSimulationPIECleanupCommand(Test, 5.0));
	return true;
}

bool FGatherersQueueSecondDropSimulationRunCommand::Update()
{
	const bool bOpenedMap = AutomationOpenMap(TEXT("/Game/SimBlank/Levels/SimBlank"));
	Test->TestTrue(TEXT("should reopen SimBlank map for second drop simulation run"), bOpenedMap);

	if (!bOpenedMap)
	{
		return true;
	}

	ADD_LATENT_AUTOMATION_COMMAND(FGatherersPrepareDeterministicSimulationFixtureCommand(Test));
	ADD_LATENT_AUTOMATION_COMMAND(FGatherersWaitForDropStateCommand(Test, FPlatformTime::Seconds(), 8.0));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	ADD_LATENT_AUTOMATION_COMMAND(FGatherersWaitForSimulationPIECleanupCommand(Test, 5.0));
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

	ADD_LATENT_AUTOMATION_COMMAND(FGatherersPrepareDeterministicSimulationFixtureCommand(this));
	ADD_LATENT_AUTOMATION_COMMAND(FGatherersWaitForSimulationPickupCommand(this, FPlatformTime::Seconds(), 5.0));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	ADD_LATENT_AUTOMATION_COMMAND(FGatherersWaitForSimulationPIECleanupCommand(this, 5.0));
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

	ADD_LATENT_AUTOMATION_COMMAND(FGatherersPrepareDeterministicSimulationFixtureCommand(this));
	ADD_LATENT_AUTOMATION_COMMAND(FGatherersWaitForSimulationPickupCommand(this, FPlatformTime::Seconds(), 5.0));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	ADD_LATENT_AUTOMATION_COMMAND(FGatherersWaitForSimulationPIECleanupCommand(this, 5.0));
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

	ADD_LATENT_AUTOMATION_COMMAND(FGatherersPrepareDeterministicSimulationFixtureCommand(this));
	ADD_LATENT_AUTOMATION_COMMAND(FGatherersWaitForCarryMovementCommand(this, FPlatformTime::Seconds(), 5.0));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	ADD_LATENT_AUTOMATION_COMMAND(FGatherersWaitForSimulationPIECleanupCommand(this, 5.0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGatherersAntDropFoodAutomationTest,
	"default.unreal_gatherers.Simulation.AntDropsFoodBackIntoWorld",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGatherersAntDropFoodAutomationTest::RunTest(const FString& Parameters)
{
	const bool bOpenedMap = AutomationOpenMap(TEXT("/Game/SimBlank/Levels/SimBlank"));
	TestTrue(TEXT("should open SimBlank map"), bOpenedMap);

	if (!bOpenedMap)
	{
		return false;
	}

	ADD_LATENT_AUTOMATION_COMMAND(FGatherersPrepareDeterministicSimulationFixtureCommand(this));
	ADD_LATENT_AUTOMATION_COMMAND(FGatherersWaitForDropStateCommand(this, FPlatformTime::Seconds(), 8.0));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	ADD_LATENT_AUTOMATION_COMMAND(FGatherersWaitForSimulationPIECleanupCommand(this, 20.0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGatherersAntDropFoodRerunAutomationTest,
	"default.unreal_gatherers.Simulation.AntDropsFoodTwiceInSameEditorSession",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGatherersAntDropFoodRerunAutomationTest::RunTest(const FString& Parameters)
{
	const bool bOpenedMap = AutomationOpenMap(TEXT("/Game/SimBlank/Levels/SimBlank"));
	TestTrue(TEXT("should open SimBlank map"), bOpenedMap);

	if (!bOpenedMap)
	{
		return false;
	}

	ADD_LATENT_AUTOMATION_COMMAND(FGatherersPrepareDeterministicSimulationFixtureCommand(this));
	ADD_LATENT_AUTOMATION_COMMAND(FGatherersWaitForDropStateCommand(this, FPlatformTime::Seconds(), 8.0));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	ADD_LATENT_AUTOMATION_COMMAND(FGatherersWaitForSimulationPIECleanupCommand(this, 5.0));
	ADD_LATENT_AUTOMATION_COMMAND(FGatherersQueueSecondDropSimulationRunCommand(this));
	return true;
}
