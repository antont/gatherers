#include "Editor.h"
#include "FileHelpers.h"
#include "HAL/PlatformTime.h"
#include "LevelEditorViewport.h"
#include "Misc/AutomationTest.h"
#include "Misc/PackageName.h"
#include "Simulation/GatherersMassSubsystem.h"
#include "Simulation/GatherersSpawnPlan.h"
#include "Simulation/GatherersWorldSpawner.h"
#include "TestLogic/GatherersWorldAssertions.h"

namespace
{
constexpr TCHAR SimBlankMapPackage[] = TEXT("/Game/SimBlank/Levels/SimBlank");
constexpr double VisualStepSeconds = 0.1;
constexpr double VisualTimeoutSeconds = 8.0;

bool LoadSimBlankIntoEditorWorld()
{
	const FString MapFilename = FPackageName::LongPackageNameToFilename(
		SimBlankMapPackage,
		FPackageName::GetMapPackageExtension());
	return FEditorFileUtils::LoadMap(MapFilename, false, false);
}

void FrameVisualFullSimulationInViewport(const FGatherersSpawnPlan& Plan)
{
	if (GCurrentLevelEditingViewportClient == nullptr)
	{
		return;
	}

	FBox FocusBounds(EForceInit::ForceInit);
	for (const FTransform& AntSpawn : Plan.AntSpawns)
	{
		FocusBounds += AntSpawn.GetLocation();
	}

	for (const FTransform& FoodSpawn : Plan.FoodSpawns)
	{
		FocusBounds += FoodSpawn.GetLocation();
	}

	FocusBounds = FocusBounds.ExpandBy(FVector(150.0f, 200.0f, 150.0f));
	GCurrentLevelEditingViewportClient->FocusViewportOnBox(FocusBounds, true);
	GCurrentLevelEditingViewportClient->Invalidate();
}

class FGatherersAdvanceVisibleFullSimulationCommand : public IAutomationLatentCommand
{
public:
	explicit FGatherersAdvanceVisibleFullSimulationCommand(FAutomationTestBase* InTest)
		: Test(InTest),
		  StartTimeSeconds(FPlatformTime::Seconds()),
		  LastStepTimeSeconds(StartTimeSeconds)
	{
	}

	virtual bool Update() override
	{
		UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
		Test->TestNotNull(TEXT("visual full-sim editor world should exist"), World);
		if (World == nullptr)
		{
			return true;
		}

		UGatherersMassSubsystem* MassSubsystem = World->GetSubsystem<UGatherersMassSubsystem>();
		Test->TestNotNull(TEXT("visual full-sim editor world should expose the gatherers Mass subsystem"), MassSubsystem);
		if (MassSubsystem == nullptr)
		{
			return true;
		}

		const GatherersWorldAssertions::FObservedMassVisualState VisualState = GatherersWorldAssertions::ObserveMassVisuals(World);
		if (VisualState.HasCarriedFoodVisual(20.0f))
		{
			++ObservedAttachedFoodFrames;
		}

		if (ObservedAttachedFoodFrames >= 2)
		{
			Test->TestTrue(TEXT("full-sim visual path reaches a second visible pickup for inspection"), true);
			return true;
		}

		const double NowSeconds = FPlatformTime::Seconds();
		if (NowSeconds - StartTimeSeconds >= VisualTimeoutSeconds)
		{
			Test->TestTrue(TEXT("full-sim visual path reaches a second visible pickup for inspection"), false);
			return true;
		}

		if (NowSeconds - LastStepTimeSeconds < VisualStepSeconds)
		{
			return false;
		}

		MassSubsystem->Tick(VisualStepSeconds);
		LastStepTimeSeconds = NowSeconds;
		return false;
	}

private:
	FAutomationTestBase* Test;
	double StartTimeSeconds;
	double LastStepTimeSeconds;
	int32 ObservedAttachedFoodFrames = 0;
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGatherersVisualFullSimulationAutomationTest,
	"manual.unreal_gatherers.Visual.FullSimulationSecondPickupStaysVisible",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGatherersVisualFullSimulationAutomationTest::RunTest(const FString& Parameters)
{
	const bool bLoadedMap = LoadSimBlankIntoEditorWorld();
	TestTrue(TEXT("should load SimBlank into the editor world"), bLoadedMap);

	if (!bLoadedMap)
	{
		return false;
	}

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	TestNotNull(TEXT("editor world should exist after loading SimBlank"), World);

	if (World == nullptr)
	{
		return false;
	}

	UGatherersMassSubsystem* MassSubsystem = World->GetSubsystem<UGatherersMassSubsystem>();
	TestNotNull(TEXT("editor world should expose the gatherers Mass subsystem"), MassSubsystem);
	if (MassSubsystem == nullptr)
	{
		return false;
	}

	MassSubsystem->ResetSimulation();

	FGatherersSpawnPlan Plan = BuildFullSimulationVisualSpawnPlan();
	Plan.bSpawnActorVisuals = false;
	const FGatherersSpawnResult Result = SpawnGatherersActors(*World, Plan);
	TestEqual(TEXT("full-sim visual spawned ant actor count"), Result.Ants.Num(), 0);
	TestEqual(TEXT("full-sim visual spawned food actor count"), Result.Foods.Num(), 0);
	TestEqual(TEXT("full-sim visual managed ant count"), MassSubsystem->GetManagedAntCount(), 1);
	TestEqual(TEXT("full-sim visual managed food count"), MassSubsystem->GetManagedFoodCount(), 3);

	if (MassSubsystem->GetManagedAntCount() != 1 || MassSubsystem->GetManagedFoodCount() != 3)
	{
		return false;
	}

	FrameVisualFullSimulationInViewport(Plan);
	ADD_LATENT_AUTOMATION_COMMAND(FGatherersAdvanceVisibleFullSimulationCommand(this));
	return true;
}
