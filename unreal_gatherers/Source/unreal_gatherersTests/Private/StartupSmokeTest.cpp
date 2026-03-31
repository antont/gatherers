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
	return GatherersWorldAssertions::PollForPickupState(
		*Test,
		GEditor ? GEditor->PlayWorld : nullptr,
		BuildInitialGatherersSpawnPlan(),
		StartTimeSeconds,
		TimeoutSeconds,
		TEXT("startup"));
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
	"supplemental.unreal_gatherers.Spawning.StartupSmokeSpawnsOneAntAndOneFood",
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
