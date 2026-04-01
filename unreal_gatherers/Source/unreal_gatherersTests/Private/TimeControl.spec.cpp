#include "Editor.h"
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
#include "unreal_gatherers/unreal_gatherersGameModeBase.h"

namespace
{
constexpr TCHAR SimBlankMapPath[] = TEXT("/Game/SimBlank/Levels/SimBlank");
constexpr double ObservationWindowSeconds = 0.3;

struct FTimeControlObservationResults
{
	float NormalDistance = 0.0f;
	float FastDistance = 0.0f;
};

FGatherersSpawnPlan BuildTimeControlFixturePlan()
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

class FGatherersPrepareTimeControlFixtureCommand : public IAutomationLatentCommand
{
public:
	FGatherersPrepareTimeControlFixtureCommand(FAutomationTestBase* InTest, EGatherersTimeControlMode InMode)
		: Test(InTest),
		  Mode(InMode)
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
		const FGatherersSpawnPlan Plan = BuildTimeControlFixturePlan();
		SpawnGatherersActors(*World, Plan);
		Test->TestEqual(TEXT("time-control fixture managed ant count"), MassSubsystem->GetManagedAntCount(), 1);
		return true;
	}

private:
	FAutomationTestBase* Test;
	EGatherersTimeControlMode Mode;
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
		}
		else
		{
			Results->NormalDistance = AntFragment.Position.X;
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
		Test->TestTrue(TEXT("normal mode should advance the ant at least a little"), Results->NormalDistance > 1.0f);
		Test->TestTrue(
			TEXT("fast world time should advance the ant farther than normal over the same wall-clock window"),
			Results->FastDistance > (Results->NormalDistance + 10.0f));
		return true;
	}

private:
	FAutomationTestBase* Test;
	TSharedRef<FTimeControlObservationResults> Results;
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

bool FGatherersWorldTimeControlAutomationTest::RunTest(const FString& Parameters)
{
	const bool bOpenedMap = AutomationOpenMap(SimBlankMapPath);
	TestTrue(TEXT("should open SimBlank map"), bOpenedMap);
	if (!bOpenedMap)
	{
		return false;
	}

	const TSharedRef<FTimeControlObservationResults> ObservationResults = MakeShared<FTimeControlObservationResults>();

	ADD_LATENT_AUTOMATION_COMMAND(FGatherersPrepareTimeControlFixtureCommand(this, EGatherersTimeControlMode::Normal));
	ADD_LATENT_AUTOMATION_COMMAND(
		FGatherersObserveTimeControlDistanceCommand(this, ObservationWindowSeconds, ObservationResults, false));
	ADD_LATENT_AUTOMATION_COMMAND(FGatherersPrepareTimeControlFixtureCommand(this, EGatherersTimeControlMode::Fast));
	ADD_LATENT_AUTOMATION_COMMAND(
		FGatherersObserveTimeControlDistanceCommand(this, ObservationWindowSeconds, ObservationResults, true));
	ADD_LATENT_AUTOMATION_COMMAND(FGatherersAssertFastModeAdvancesFurtherCommand(this, ObservationResults));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	ADD_LATENT_AUTOMATION_COMMAND(FGatherersWaitForTimeControlPIECleanupCommand(this, FPlatformTime::Seconds(), 5.0));
	return true;
}
