#include "unreal_gatherersTests.h"

#include "Misc/AutomationTest.h"
#include "Modules/ModuleManager.h"

IMPLEMENT_MODULE(Funreal_gatherersTestsModule, unreal_gatherersTests)

void Funreal_gatherersTestsModule::StartupModule()
{
}

void Funreal_gatherersTestsModule::ShutdownModule()
{
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGatherersPlaceholderAutomationTest,
	"default.unreal_gatherers.Placeholder.LoadsTestModule",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGatherersPlaceholderAutomationTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("placeholder test runs"), true);
	return true;
}
