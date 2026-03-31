#include "MassEntitySubsystem.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGatherersMassEntityAvailabilityAutomationTest,
	"default.unreal_gatherers.Mass.MassEntitySubsystemIsAvailable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGatherersMassEntityAvailabilityAutomationTest::RunTest(const FString& Parameters)
{
	UWorld* World = GWorld;
	TestNotNull(TEXT("test world should exist"), World);

	if (World == nullptr)
	{
		return false;
	}

	UMassEntitySubsystem* MassEntitySubsystem = World->GetSubsystem<UMassEntitySubsystem>();
	TestNotNull(TEXT("MassEntity subsystem should be available once the project enables Mass"), MassEntitySubsystem);
	return MassEntitySubsystem != nullptr;
}
