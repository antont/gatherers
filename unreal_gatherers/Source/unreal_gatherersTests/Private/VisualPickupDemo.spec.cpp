#include "Actors/Ant.h"
#include "Actors/Food.h"
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

void FrameVisualPickupInViewport(const FGatherersSpawnPlan& Plan)
{
	if (GCurrentLevelEditingViewportClient == nullptr)
	{
		return;
	}

	FBox FocusBounds(EForceInit::ForceInit);
	FocusBounds += Plan.AntSpawns[0].GetLocation();
	for (const FTransform& FoodSpawn : Plan.FoodSpawns)
	{
		FocusBounds += FoodSpawn.GetLocation();
	}
	FocusBounds = FocusBounds.ExpandBy(FVector(150.0f, 300.0f, 150.0f));

	GCurrentLevelEditingViewportClient->FocusViewportOnBox(FocusBounds, true);
	GCurrentLevelEditingViewportClient->Invalidate();
}

class FGatherersAdvanceVisiblePickupCommand : public IAutomationLatentCommand
{
public:
	explicit FGatherersAdvanceVisiblePickupCommand(FAutomationTestBase* InTest)
		: Test(InTest),
		  StartTimeSeconds(FPlatformTime::Seconds()),
		  LastStepTimeSeconds(StartTimeSeconds)
	{
	}

	virtual bool Update() override
	{
		UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
		Test->TestNotNull(TEXT("visual editor world should exist"), World);
		if (World == nullptr)
		{
			return true;
		}

		UGatherersMassSubsystem* MassSubsystem = World->GetSubsystem<UGatherersMassSubsystem>();
		Test->TestNotNull(TEXT("visual editor world should expose the gatherers Mass subsystem"), MassSubsystem);
		if (MassSubsystem == nullptr)
		{
			return true;
		}

		const FGatherersSpawnPlan Plan = BuildInitialGatherersSpawnPlan();
		const GatherersWorldAssertions::FObservedWorldState WorldState = GatherersWorldAssertions::Observe(World);
		AFood* AttachedFood = WorldState.GetFirstAttachedFood();
		if (AttachedFood != nullptr)
		{
			bSawAttachedFood = true;
		}

		if (WorldState.HasSingleAntAndTwoFoods() && bSawAttachedFood && AttachedFood == nullptr)
		{
			GatherersWorldAssertions::AssertFirstDropState(*Test, WorldState, Plan, TEXT("visual"));
			return true;
		}

		const double NowSeconds = FPlatformTime::Seconds();
		if (NowSeconds - StartTimeSeconds >= VisualTimeoutSeconds)
		{
			GatherersWorldAssertions::AssertFirstDropState(*Test, WorldState, Plan, TEXT("visual"));
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
	bool bSawAttachedFood = false;
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGatherersVisualPickupAutomationTest,
	"manual.unreal_gatherers.Visual.AntFirstDropLeavesWorldForInspection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGatherersVisualPickupAutomationTest::RunTest(const FString& Parameters)
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

	const FGatherersSpawnPlan Plan = BuildInitialGatherersSpawnPlan();
	const FGatherersSpawnResult Result = SpawnGatherersActors(*World, Plan);
	TestEqual(TEXT("visual spawned ant count"), Result.Ants.Num(), 1);
	TestEqual(TEXT("visual spawned food count"), Result.Foods.Num(), 2);

	if (Result.Ants.Num() != 1 || Result.Foods.Num() != 2)
	{
		return false;
	}

	FrameVisualPickupInViewport(Plan);
	ADD_LATENT_AUTOMATION_COMMAND(FGatherersAdvanceVisiblePickupCommand(this));
	return true;
}
