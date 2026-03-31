#include "Editor.h"
#include "MassRepresentationSubsystem.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGatherersMassRepresentationAvailabilityAutomationTest,
	"default.unreal_gatherers.Mass.MassRepresentationSubsystemIsAvailable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGatherersMassRepresentationAvailabilityAutomationTest::RunTest(const FString& Parameters)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	TestNotNull(TEXT("editor world should exist"), World);

	if (World == nullptr)
	{
		return false;
	}

	UMassRepresentationSubsystem* RepresentationSubsystem = World->GetSubsystem<UMassRepresentationSubsystem>();
	TestNotNull(TEXT("Mass representation subsystem should exist"), RepresentationSubsystem);
	return RepresentationSubsystem != nullptr;
}
