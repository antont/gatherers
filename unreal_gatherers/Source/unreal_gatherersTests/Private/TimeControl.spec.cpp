#include "Editor.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/PlatformTime.h"
#include "MassEntitySubsystem.h"
#include "MassEntityView.h"
#include "Misc/AutomationTest.h"
#include "Misc/Optional.h"
#include "Simulation/GatherersMassSubsystem.h"
#include "Simulation/GatherersSpawnPlan.h"
#include "Simulation/GatherersWorldSpawner.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"
#include "TestLogic/GatherersWorldAssertions.h"
#include "Templates/SharedPointer.h"
#include "UI/GatherersTimeControlWidget.h"
#include "unreal_gatherers/unreal_gatherersGameModeBase.h"

namespace
{
constexpr TCHAR SimBlankMapPath[] = TEXT("/Game/SimBlank/Levels/SimBlank");
constexpr double ObservationWindowSeconds = 0.3;

struct FTimeControlObservationResults
{
	float NormalDistance = 0.0f;
	float FastDistance = 0.0f;
	float NormalSimulatedSeconds = 0.0f;
	float FastSimulatedSeconds = 0.0f;
	float NormalWorldTimeDilation = 0.0f;
	float FastWorldTimeDilation = 0.0f;
};

FGatherersSpawnPlan BuildSingleAntTimeControlFixturePlan()
{
	FGatherersSpawnPlan Plan;
	Plan.bUseFullSimulationMode = true;
	Plan.bSpawnActorVisuals = false;
	Plan.PlayAreaBounds = FBox(FVector(-500.0f, -500.0f, -100.0f), FVector(500.0f, 500.0f, 100.0f));
	Plan.AntSpawns.Add(FTransform(FVector::ZeroVector));
	Plan.AntInitialDirections.Add(FVector(1.0f, 0.0f, 0.0f));
	Plan.FullSimulationTurnJitterRadians = 0.0f;
	return Plan;
}

FGatherersSpawnPlan BuildDenseTimeControlFixturePlan()
{
	FGatherersSpawnPlan Plan;
	Plan.bUseFullSimulationMode = true;
	Plan.bSpawnActorVisuals = false;
	Plan.PlayAreaBounds = FBox(FVector(-500.0f, -500.0f, -100.0f), FVector(500.0f, 500.0f, 100.0f));
	Plan.FullSimulationTurnJitterRadians = 0.0f;
	Plan.FullSimulationMovementSpeed = 100.0f;

	for (int32 AntIndex = 0; AntIndex < 8; ++AntIndex)
	{
		Plan.AntSpawns.Add(FTransform(FVector(0.0f, -175.0f + (AntIndex * 50.0f), 0.0f)));
		Plan.AntInitialDirections.Add(FVector(1.0f, 0.0f, 0.0f));
	}

	for (int32 FoodIndex = 0; FoodIndex < 16; ++FoodIndex)
	{
		Plan.FoodSpawns.Add(FTransform(FVector(-400.0f + (FoodIndex * 40.0f), 300.0f, 0.0f)));
	}

	return Plan;
}

class FGatherersPrepareTimeControlFixtureCommand : public IAutomationLatentCommand
{
public:
	FGatherersPrepareTimeControlFixtureCommand(
		FAutomationTestBase* InTest,
		EGatherersTimeControlMode InMode,
		FGatherersSpawnPlan InPlan)
		: Test(InTest),
		  Mode(InMode),
		  Plan(MoveTemp(InPlan))
	{
	}

	bool Update() override
	{
		UWorld* World = GEditor ? GEditor->PlayWorld : nullptr;
		Test->TestNotNull(TEXT("time-control play world should exist"), World);
		if (World == nullptr)
		{
			return true;
		}

		UGatherersMassSubsystem* MassSubsystem = World->GetSubsystem<UGatherersMassSubsystem>();
		Test->TestNotNull(TEXT("time-control Mass subsystem should exist"), MassSubsystem);
		if (MassSubsystem == nullptr)
		{
			return true;
		}

		Aunreal_gatherersGameModeBase::ApplyTimeControlModeToWorld(*World, Mode);
		MassSubsystem->ResetSimulation();
		SpawnGatherersActors(*World, Plan);
		Test->TestEqual(
			TEXT("time-control fixture managed ant count should match the requested plan"),
			MassSubsystem->GetManagedAntCount(),
			Plan.AntSpawns.Num());
		return true;
	}

private:
	FAutomationTestBase* Test;
	EGatherersTimeControlMode Mode;
	FGatherersSpawnPlan Plan;
};

class FGatherersObserveTimeControlDistanceCommand : public IAutomationLatentCommand
{
public:
	FGatherersObserveTimeControlDistanceCommand(
		FAutomationTestBase* InTest,
		double InObservationWindowSeconds,
		TSharedRef<FTimeControlObservationResults> InResults,
		bool bInObserveFastDistance)
		: Test(InTest),
		  ObservationWindowSeconds(InObservationWindowSeconds),
		  Results(InResults),
		  bObserveFastDistance(bInObserveFastDistance)
	{
	}

	bool Update() override
	{
		if (!StartTimeSeconds.IsSet())
		{
			StartTimeSeconds = FPlatformTime::Seconds();
		}

		if (FPlatformTime::Seconds() - StartTimeSeconds.GetValue() < ObservationWindowSeconds)
		{
			return false;
		}

		UWorld* World = GEditor ? GEditor->PlayWorld : nullptr;
		Test->TestNotNull(TEXT("time-control observed play world should exist"), World);
		if (World == nullptr)
		{
			return true;
		}

		UGatherersMassSubsystem* MassSubsystem = World->GetSubsystem<UGatherersMassSubsystem>();
		UMassEntitySubsystem* MassEntitySubsystem = World->GetSubsystem<UMassEntitySubsystem>();
		Test->TestNotNull(TEXT("time-control Mass subsystem should exist"), MassSubsystem);
		Test->TestNotNull(TEXT("time-control Mass entity subsystem should exist"), MassEntitySubsystem);
		if (MassSubsystem == nullptr || MassEntitySubsystem == nullptr || MassSubsystem->ManagedAntEntities.Num() != 1)
		{
			return true;
		}

		FMassEntityManager& EntityManager = MassEntitySubsystem->GetMutableEntityManager();
		FMassEntityView AntView(EntityManager, MassSubsystem->ManagedAntEntities[0]);
		const FGatherersMassAntFragment& AntFragment = AntView.GetFragmentData<FGatherersMassAntFragment>();
		if (bObserveFastDistance)
		{
			Results->FastDistance = AntFragment.Position.X;
			Results->FastSimulatedSeconds = MassSubsystem->GetAccumulatedSimulationSeconds();
			Results->FastWorldTimeDilation = World->GetWorldSettings() != nullptr
				? World->GetWorldSettings()->GetEffectiveTimeDilation()
				: 0.0f;
		}
		else
		{
			Results->NormalDistance = AntFragment.Position.X;
			Results->NormalSimulatedSeconds = MassSubsystem->GetAccumulatedSimulationSeconds();
			Results->NormalWorldTimeDilation = World->GetWorldSettings() != nullptr
				? World->GetWorldSettings()->GetEffectiveTimeDilation()
				: 0.0f;
		}
		return true;
	}

private:
	FAutomationTestBase* Test;
	double ObservationWindowSeconds;
	TSharedRef<FTimeControlObservationResults> Results;
	bool bObserveFastDistance;
	TOptional<double> StartTimeSeconds;
};

class FGatherersAssertFastModeAdvancesFurtherCommand : public IAutomationLatentCommand
{
public:
	FGatherersAssertFastModeAdvancesFurtherCommand(
		FAutomationTestBase* InTest,
		TSharedRef<FTimeControlObservationResults> InResults)
		: Test(InTest),
		  Results(InResults)
	{
	}

	bool Update() override
	{
		Test->TestEqual(
			TEXT("normal mode should leave Unreal world time dilation at 1x because speed control belongs to the sim clock"),
			Results->NormalWorldTimeDilation,
			1.0f);
		Test->TestEqual(
			TEXT("fast mode should also leave Unreal world time dilation at 1x because speed control belongs to the sim clock"),
			Results->FastWorldTimeDilation,
			1.0f);
		Test->TestTrue(TEXT("normal mode should advance the ant at least a little"), Results->NormalDistance > 1.0f);
		Test->TestTrue(
			TEXT("normal mode should accumulate simulated seconds while the fixture runs"),
			Results->NormalSimulatedSeconds >= 0.20f);
		Test->TestTrue(
			TEXT("fast sim rate should advance the ant farther than normal over the same wall-clock window"),
			Results->FastDistance > (Results->NormalDistance + 10.0f));
		Test->TestTrue(
			TEXT("fast sim rate should accumulate about four times the simulated seconds seen in normal mode"),
			Results->FastSimulatedSeconds >= (Results->NormalSimulatedSeconds * 3.5f));
		return true;
	}

private:
	FAutomationTestBase* Test;
	TSharedRef<FTimeControlObservationResults> Results;
};

class FGatherersAssertDenseFastTimeFixtureCommand : public IAutomationLatentCommand
{
public:
	FGatherersAssertDenseFastTimeFixtureCommand(
		FAutomationTestBase* InTest,
		double InObservationWindowSeconds)
		: Test(InTest),
		  ObservationWindowSeconds(InObservationWindowSeconds)
	{
	}

	bool Update() override
	{
		if (!StartTimeSeconds.IsSet())
		{
			StartTimeSeconds = FPlatformTime::Seconds();
		}

		if (FPlatformTime::Seconds() - StartTimeSeconds.GetValue() < ObservationWindowSeconds)
		{
			return false;
		}

		UWorld* World = GEditor ? GEditor->PlayWorld : nullptr;
		Test->TestNotNull(TEXT("dense time-control play world should exist"), World);
		if (World == nullptr)
		{
			return true;
		}

		UGatherersMassSubsystem* MassSubsystem = World->GetSubsystem<UGatherersMassSubsystem>();
		UMassEntitySubsystem* MassEntitySubsystem = World->GetSubsystem<UMassEntitySubsystem>();
		Test->TestNotNull(TEXT("dense time-control Mass subsystem should exist"), MassSubsystem);
		Test->TestNotNull(TEXT("dense time-control Mass entity subsystem should exist"), MassEntitySubsystem);
		if (MassSubsystem == nullptr || MassEntitySubsystem == nullptr)
		{
			return true;
		}

		Test->TestEqual(TEXT("dense fast fixture should preserve ant count"), MassSubsystem->GetManagedAntCount(), 8);
		Test->TestEqual(TEXT("dense fast fixture should preserve food count"), MassSubsystem->GetManagedFoodCount(), 16);
		Test->TestTrue(
			TEXT("dense fast fixture should accumulate roughly the expected simulated seconds"),
			MassSubsystem->GetAccumulatedSimulationSeconds() >= 0.70f);

		FMassEntityManager& EntityManager = MassEntitySubsystem->GetMutableEntityManager();
		for (const FMassEntityHandle AntEntity : MassSubsystem->ManagedAntEntities)
		{
			if (!EntityManager.IsEntityValid(AntEntity))
			{
				continue;
			}

			FMassEntityView AntView(EntityManager, AntEntity);
			const FGatherersMassAntFragment& AntFragment = AntView.GetFragmentData<FGatherersMassAntFragment>();
			Test->TestTrue(TEXT("dense fast fixture ant X position should remain finite"), FMath::IsFinite(AntFragment.Position.X));
			Test->TestTrue(TEXT("dense fast fixture ant Y position should remain finite"), FMath::IsFinite(AntFragment.Position.Y));
		}

		return true;
	}

private:
	FAutomationTestBase* Test;
	double ObservationWindowSeconds;
	TOptional<double> StartTimeSeconds;
};

class FGatherersAssertTimeControlWidgetToggleCommand : public IAutomationLatentCommand
{
public:
	explicit FGatherersAssertTimeControlWidgetToggleCommand(FAutomationTestBase* InTest)
		: Test(InTest)
	{
	}

	bool Update() override
	{
		UWorld* World = GEditor ? GEditor->PlayWorld : nullptr;
		Test->TestNotNull(TEXT("time-control widget play world should exist"), World);
		if (World == nullptr)
		{
			return true;
		}

		Aunreal_gatherersGameModeBase* GameMode = World->GetAuthGameMode<Aunreal_gatherersGameModeBase>();
		Test->TestNotNull(TEXT("time-control widget game mode should exist"), GameMode);
		if (GameMode == nullptr)
		{
			return true;
		}

		UGatherersTimeControlWidget* Widget = GameMode->GetTimeControlWidget();
		Test->TestNotNull(TEXT("time-control widget should be created for the playable world"), Widget);
		if (Widget == nullptr)
		{
			return true;
		}

		Test->TestEqual(
			TEXT("time-control widget should show the current startup mode"),
			Widget->GetCurrentModeLabel(),
			FString(TEXT("Normal")));
		Test->TestEqual(
			TEXT("startup mode should begin at normal"),
			GameMode->GetTimeControlMode(),
			EGatherersTimeControlMode::Normal);

		Widget->TriggerToggleFromUI();

		Test->TestEqual(
			TEXT("UI toggle should switch the world time mode to fast"),
			GameMode->GetTimeControlMode(),
			EGatherersTimeControlMode::Fast);
		Test->TestEqual(
			TEXT("fast mode should leave world dilation at 1x because the simulation clock owns speed"),
			World->GetWorldSettings()->GetEffectiveTimeDilation(),
			1.0f);
		Test->TestEqual(
			TEXT("time-control widget should update its label after toggling"),
			Widget->GetCurrentModeLabel(),
			FString(TEXT("Fast")));

		Test->TestTrue(
			TEXT("very-fast mode should use a higher simulation rate than the existing fast mode"),
			Aunreal_gatherersGameModeBase::GetSimulationRateForMode(EGatherersTimeControlMode::VeryFast)
				> Aunreal_gatherersGameModeBase::GetSimulationRateForMode(EGatherersTimeControlMode::Fast));

		Widget->TriggerToggleFromUI();

		Test->TestEqual(
			TEXT("a second UI toggle should switch the world time mode to very-fast"),
			GameMode->GetTimeControlMode(),
			EGatherersTimeControlMode::VeryFast);
		Test->TestEqual(
			TEXT("very-fast mode should also leave world dilation at 1x because the simulation clock owns speed"),
			World->GetWorldSettings()->GetEffectiveTimeDilation(),
			1.0f);
		Test->TestEqual(
			TEXT("time-control widget should show the very-fast dilation"),
			Widget->GetCurrentModeLabel(),
			FString(TEXT("Very Fast (27x)")));

		Widget->TriggerToggleFromUI();

		Test->TestEqual(
			TEXT("a third UI toggle should switch the world time mode to fastest"),
			GameMode->GetTimeControlMode(),
			EGatherersTimeControlMode::Fastest);
		Test->TestEqual(
			TEXT("fastest mode should also leave world dilation at 1x because the simulation clock owns speed"),
			World->GetWorldSettings()->GetEffectiveTimeDilation(),
			1.0f);
		Test->TestEqual(
			TEXT("time-control widget should show the fastest dilation"),
			Widget->GetCurrentModeLabel(),
			FString(TEXT("Fastest (100x)")));

		Widget->TriggerToggleFromUI();

		Test->TestEqual(
			TEXT("a fourth UI toggle should cycle the world time mode back to normal"),
			GameMode->GetTimeControlMode(),
			EGatherersTimeControlMode::Normal);
		Test->TestEqual(
			TEXT("time-control widget should return to the normal label after the full cycle"),
			Widget->GetCurrentModeLabel(),
			FString(TEXT("Normal")));
		return true;
	}

private:
	FAutomationTestBase* Test;
};

DEFINE_LATENT_AUTOMATION_COMMAND_THREE_PARAMETER(
	FGatherersWaitForTimeControlPIECleanupCommand,
	FAutomationTestBase*,
	Test,
	double,
	StartTimeSeconds,
	double,
	TimeoutSeconds);

bool FGatherersWaitForTimeControlPIECleanupCommand::Update()
{
	return GatherersWorldAssertions::PollForPIEToEnd(
		*Test,
		SimBlankMapPath,
		StartTimeSeconds,
		TimeoutSeconds,
		TEXT("time-control"));
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGatherersWorldTimeControlAutomationTest,
	"default.unreal_gatherers.TimeControl.FastWorldTimeAdvancesSimulationFurtherThanNormal",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGatherersTimeControlWidgetAutomationTest,
	"default.unreal_gatherers.TimeControl.UiButtonTogglesWorldTimeMode",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGatherersDenseFixtureFastWorldTimeAutomationTest,
	"default.unreal_gatherers.TimeControl.FastWorldTimeKeepsDenseFixtureStable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGatherersWorldTimeControlAutomationTest::RunTest(const FString& Parameters)
{
	const bool bOpenedMap = AutomationOpenMap(SimBlankMapPath);
	TestTrue(TEXT("should open SimBlank map"), bOpenedMap);
	if (!bOpenedMap)
	{
		return false;
	}

	const TSharedRef<FTimeControlObservationResults> ObservationResults = MakeShared<FTimeControlObservationResults>();

	ADD_LATENT_AUTOMATION_COMMAND(FGatherersPrepareTimeControlFixtureCommand(
		this,
		EGatherersTimeControlMode::Normal,
		BuildSingleAntTimeControlFixturePlan()));
	ADD_LATENT_AUTOMATION_COMMAND(
		FGatherersObserveTimeControlDistanceCommand(this, ObservationWindowSeconds, ObservationResults, false));
	ADD_LATENT_AUTOMATION_COMMAND(FGatherersPrepareTimeControlFixtureCommand(
		this,
		EGatherersTimeControlMode::Fast,
		BuildSingleAntTimeControlFixturePlan()));
	ADD_LATENT_AUTOMATION_COMMAND(
		FGatherersObserveTimeControlDistanceCommand(this, ObservationWindowSeconds, ObservationResults, true));
	ADD_LATENT_AUTOMATION_COMMAND(FGatherersAssertFastModeAdvancesFurtherCommand(this, ObservationResults));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	ADD_LATENT_AUTOMATION_COMMAND(FGatherersWaitForTimeControlPIECleanupCommand(this, FPlatformTime::Seconds(), 5.0));
	return true;
}

bool FGatherersTimeControlWidgetAutomationTest::RunTest(const FString& Parameters)
{
	const bool bOpenedMap = AutomationOpenMap(SimBlankMapPath);
	TestTrue(TEXT("should open SimBlank map"), bOpenedMap);
	if (!bOpenedMap)
	{
		return false;
	}

	ADD_LATENT_AUTOMATION_COMMAND(FGatherersAssertTimeControlWidgetToggleCommand(this));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	ADD_LATENT_AUTOMATION_COMMAND(FGatherersWaitForTimeControlPIECleanupCommand(this, FPlatformTime::Seconds(), 5.0));
	return true;
}

bool FGatherersDenseFixtureFastWorldTimeAutomationTest::RunTest(const FString& Parameters)
{
	const bool bOpenedMap = AutomationOpenMap(SimBlankMapPath);
	TestTrue(TEXT("should open SimBlank map"), bOpenedMap);
	if (!bOpenedMap)
	{
		return false;
	}

	ADD_LATENT_AUTOMATION_COMMAND(FGatherersPrepareTimeControlFixtureCommand(
		this,
		EGatherersTimeControlMode::Fast,
		BuildDenseTimeControlFixturePlan()));
	ADD_LATENT_AUTOMATION_COMMAND(FGatherersAssertDenseFastTimeFixtureCommand(this, ObservationWindowSeconds));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	ADD_LATENT_AUTOMATION_COMMAND(FGatherersWaitForTimeControlPIECleanupCommand(this, FPlatformTime::Seconds(), 5.0));
	return true;
}
