#include "Actors/Ant.h"
#include "Actors/Food.h"
#include "Editor.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGatherersFullSimulationIgnoresOffPathFoodAutomationTest,
	"default.unreal_gatherers.FullSimulation.AntKeepsHeadingWhenFoodIsOffPath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGatherersFullSimulationIgnoresOffPathFoodAutomationTest::RunTest(const FString& Parameters)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	TestNotNull(TEXT("editor world should exist"), World);

	if (World == nullptr)
	{
		return false;
	}

	AAnt* Ant = World->SpawnActor<AAnt>(AAnt::StaticClass(), FTransform(FVector::ZeroVector));
	AFood* Food = World->SpawnActor<AFood>(AFood::StaticClass(), FTransform(FVector(0.0f, 200.0f, 0.0f)));
	TestNotNull(TEXT("full-sim ant should spawn"), Ant);
	TestNotNull(TEXT("full-sim food should spawn"), Food);

	if (Ant == nullptr || Food == nullptr)
	{
		return false;
	}

	Ant->ConfigureForFullSimulation(FVector(1.0f, 0.0f, 0.0f), FBox(FVector(-500.0f, -500.0f, -100.0f), FVector(500.0f, 500.0f, 100.0f)), 123);

	for (int32 StepIndex = 0; StepIndex < 5; ++StepIndex)
	{
		Ant->Tick(0.1f);
	}

	TestTrue(
		TEXT("full-sim ant keeps moving forward instead of steering toward off-path food"),
		Ant->GetActorLocation().Equals(FVector(50.0f, 0.0f, 0.0f), 1.0f));

	Ant->Destroy();
	Food->Destroy();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGatherersFullSimulationBorderTurnBackAutomationTest,
	"default.unreal_gatherers.FullSimulation.AntTurnsBackAtPlayAreaBorder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGatherersFullSimulationBorderTurnBackAutomationTest::RunTest(const FString& Parameters)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	TestNotNull(TEXT("editor world should exist"), World);

	if (World == nullptr)
	{
		return false;
	}

	AAnt* Ant = World->SpawnActor<AAnt>(AAnt::StaticClass(), FTransform(FVector::ZeroVector));
	TestNotNull(TEXT("full-sim border ant should spawn"), Ant);

	if (Ant == nullptr)
	{
		return false;
	}

	Ant->ConfigureForFullSimulation(FVector(1.0f, 0.0f, 0.0f), FBox(FVector(-10.0f, -100.0f, -100.0f), FVector(10.0f, 100.0f, 100.0f)), 123);
	Ant->Tick(0.1f);
	Ant->Tick(0.1f);
	Ant->Tick(0.1f);

	TestTrue(
		TEXT("full-sim ant stays inside the play area after touching the border"),
		Ant->GetActorLocation().X <= 10.0f + KINDA_SMALL_NUMBER);

	Ant->Destroy();
	return true;
}
