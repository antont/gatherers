#include "Actors/Ant.h"
#include "Actors/Food.h"
#include "Editor.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"
#include "Simulation/GatherersMassSubsystem.h"
#include "Simulation/GatherersSpawnPlan.h"
#include "TestLogic/GatherersWorldAssertions.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"
#include "unreal_gatherers/unreal_gatherersGameModeBase.h"

DEFINE_LATENT_AUTOMATION_COMMAND_THREE_PARAMETER(
	FGatherersWaitForStartupActorsCommand,
	FAutomationTestBase*,
	Test,
	double,
	StartTimeSeconds,
	double,
	TimeoutSeconds);

DEFINE_LATENT_AUTOMATION_COMMAND_THREE_PARAMETER(
	FGatherersWaitForStartupPIECleanupCommand,
	FAutomationTestBase*,
	Test,
	double,
	StartTimeSeconds,
	double,
	TimeoutSeconds);

bool FGatherersWaitForStartupActorsCommand::Update()
{
	UWorld* World = GEditor ? GEditor->PlayWorld : nullptr;
	Test->TestNotNull(TEXT("startup world should exist"), World);
	if (World == nullptr)
	{
		return true;
	}

	const GatherersWorldAssertions::FObservedMassVisualState VisualState = GatherersWorldAssertions::ObserveMassVisuals(World);
	UGatherersMassSubsystem* MassSubsystem = World->GetSubsystem<UGatherersMassSubsystem>();
	Aunreal_gatherersGameModeBase* GameMode = World->GetAuthGameMode<Aunreal_gatherersGameModeBase>();
	Test->TestNotNull(TEXT("startup world should expose the gatherers Mass subsystem"), MassSubsystem);
	Test->TestNotNull(TEXT("startup world should use the gatherers game mode"), GameMode);
	const GatherersWorldAssertions::FObservedWorldState ActorState = GatherersWorldAssertions::Observe(World);
	if (VisualState.AntPositions.Num() != 26 || VisualState.FoodPositions.Num() != 80)
	{
		if (FPlatformTime::Seconds() - StartTimeSeconds < TimeoutSeconds)
		{
			return false;
		}
	}

	Test->TestEqual(TEXT("startup ant actor count"), ActorState.Ants.Num(), 0);
	Test->TestEqual(TEXT("startup food actor count"), ActorState.Foods.Num(), 0);
	Test->TestEqual(TEXT("startup ant entity count"), VisualState.AntPositions.Num(), 26);
	Test->TestEqual(TEXT("startup food entity count"), VisualState.FoodPositions.Num(), 80);
	Test->TestEqual(TEXT("startup ant visual count"), VisualState.AntVisualPositions.Num(), 26);
	Test->TestEqual(TEXT("startup food visual count"), VisualState.FoodVisualPositions.Num(), 80);
	if (MassSubsystem != nullptr)
	{
		Test->TestEqual(TEXT("startup managed ant count"), MassSubsystem->GetManagedAntCount(), 26);
		Test->TestEqual(TEXT("startup managed food count"), MassSubsystem->GetManagedFoodCount(), 80);
	}
	if (GameMode != nullptr)
	{
		Test->TestEqual(
			TEXT("startup active time mode should match the configured startup mode"),
			GameMode->GetTimeControlMode(),
			GameMode->GetStartupTimeControlMode());
		Test->TestEqual(
			TEXT("startup world time dilation should stay at 1x because startup speed uses the simulation clock"),
			World->GetWorldSettings()->GetEffectiveTimeDilation(),
			1.0f);
		Test->TestEqual(
			TEXT("startup simulation rate should match the startup mode"),
			MassSubsystem->GetSimulationRateMultiplier(),
			Aunreal_gatherersGameModeBase::GetSimulationRateForMode(GameMode->GetStartupTimeControlMode()));
	}
	return true;
}

bool FGatherersWaitForStartupPIECleanupCommand::Update()
{
	return GatherersWorldAssertions::PollForPIEToEnd(
		*Test,
		TEXT("/Game/SimBlank/Levels/SimBlank"),
		StartTimeSeconds,
		TimeoutSeconds,
		TEXT("startup"));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGatherersStartupSmokeAutomationTest,
	"supplemental.unreal_gatherers.Spawning.StartupSmokeSpawnsRustLikeFullSimulationCounts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGatherersStartupSmokeAutomationTest::RunTest(const FString& Parameters)
{
	const bool bOpenedMap = AutomationOpenMap(TEXT("/Game/SimBlank/Levels/SimBlank"));
	TestTrue(TEXT("should open SimBlank map"), bOpenedMap);

	if (!bOpenedMap)
	{
		return false;
	}

	ADD_LATENT_AUTOMATION_COMMAND(FGatherersWaitForStartupActorsCommand(this, FPlatformTime::Seconds(), 5.0));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	ADD_LATENT_AUTOMATION_COMMAND(FGatherersWaitForStartupPIECleanupCommand(this, FPlatformTime::Seconds(), 5.0));
	return true;
}
