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

	const GatherersWorldAssertions::FObservedWorldState WorldState = GatherersWorldAssertions::Observe(World);
	if (WorldState.Ants.Num() != 26 || WorldState.Foods.Num() != 80)
	{
		if (FPlatformTime::Seconds() - StartTimeSeconds < TimeoutSeconds)
		{
			return false;
		}
	}

	Test->TestEqual(TEXT("startup ant count"), WorldState.Ants.Num(), 26);
	Test->TestEqual(TEXT("startup food count"), WorldState.Foods.Num(), 80);
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
