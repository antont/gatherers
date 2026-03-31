#include "Actors/Ant.h"
#include "Actors/Food.h"
#include "Editor.h"
#include "FileHelpers.h"
#include "HAL/PlatformTime.h"
#include "LevelEditorViewport.h"
#include "Misc/AutomationTest.h"
#include "Misc/PackageName.h"
#include "Simulation/GatherersSpawnPlan.h"
#include "Simulation/GatherersWorldSpawner.h"

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
	FGatherersAdvanceVisibleFullSimulationCommand(FAutomationTestBase* InTest, AAnt* InAnt, const TArray<AFood*>& InFoods)
		: Test(InTest),
		  Ant(InAnt),
		  Foods(InFoods),
		  StartTimeSeconds(FPlatformTime::Seconds()),
		  LastStepTimeSeconds(StartTimeSeconds)
	{
	}

	virtual bool Update() override
	{
		Test->TestNotNull(TEXT("visual full-sim ant should remain valid"), Ant);
		if (Ant == nullptr)
		{
			return true;
		}

		int32 AttachedFoodCount = 0;
		for (AFood* Food : Foods)
		{
			if (Food != nullptr && Food->GetAttachParentActor() == Ant)
			{
				++AttachedFoodCount;
			}
		}

		if (AttachedFoodCount == 1)
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

		Ant->Tick(VisualStepSeconds);
		LastStepTimeSeconds = NowSeconds;
		return false;
	}

private:
	FAutomationTestBase* Test;
	AAnt* Ant;
	TArray<AFood*> Foods;
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

	const FGatherersSpawnPlan Plan = BuildFullSimulationVisualSpawnPlan();
	const FGatherersSpawnResult Result = SpawnGatherersActors(*World, Plan);
	TestEqual(TEXT("full-sim visual spawned ant count"), Result.Ants.Num(), 1);
	TestEqual(TEXT("full-sim visual spawned food count"), Result.Foods.Num(), 3);

	if (Result.Ants.Num() != 1 || Result.Foods.Num() != 3)
	{
		return false;
	}

	Result.Ants[0]->SetFullSimulationTurnJitterRadians(0.0f);
	FrameVisualFullSimulationInViewport(Plan);
	ADD_LATENT_AUTOMATION_COMMAND(FGatherersAdvanceVisibleFullSimulationCommand(this, Result.Ants[0], Result.Foods));
	return true;
}
