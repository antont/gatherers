#include "Editor.h"
#include "HAL/PlatformTime.h"
#include "MassEntitySubsystem.h"
#include "MassEntityView.h"
#include "Misc/AutomationTest.h"
#include "Simulation/GatherersMassSubsystem.h"
#include "Simulation/GatherersSpawnPlan.h"
#include "Simulation/GatherersWorldSpawner.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"
#include "TestLogic/GatherersWorldAssertions.h"
#include "unreal_gatherers/unreal_gatherersGameModeBase.h"

namespace
{
constexpr TCHAR SimBlankMapPath[] = TEXT("/Game/SimBlank/Levels/SimBlank");
constexpr double ObservationWindowSeconds = 0.3;

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

		Aunreal_gatherersGameModeBase* GameMode = World->GetAuthGameMode<Aunreal_gatherersGameModeBase>();
		Test->TestNotNull(TEXT("time-control game mode should exist"), GameMode);
		if (GameMode == nullptr)
		{
			return true;
		}

		UGatherersMassSubsystem* MassSubsystem = World->GetSubsystem<UGatherersMassSubsystem>();
		Test->TestNotNull(TEXT("time-control Mass subsystem should exist"), MassSubsystem);
		if (MassSubsystem == nullptr)
		{
			return true;
		}

		GameMode->ApplyTimeControlMode(Mode);
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
	FGatherersObserveTimeControlDistanceCommand(FAutomationTestBase* InTest, double InObservationWindowSeconds, float* InOutDistance)
		: Test(InTest),
		  ObservationWindowSeconds(InObservationWindowSeconds),
		  OutDistance(InOutDistance),
		  StartTimeSeconds(FPlatformTime::Seconds())
	{
	}

	bool Update() override
	{
		if (FPlatformTime::Seconds() - StartTimeSeconds < ObservationWindowSeconds)
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
		*OutDistance = AntFragment.Position.X;
		return true;
	}

private:
	FAutomationTestBase* Test;
	double ObservationWindowSeconds;
	float* OutDistance;
	double StartTimeSeconds;
};

class FGatherersAssertFastModeAdvancesFurtherCommand : public IAutomationLatentCommand
{
public:
	FGatherersAssertFastModeAdvancesFurtherCommand(FAutomationTestBase* InTest, const float* InNormalDistance, const float* InFastDistance)
		: Test(InTest),
		  NormalDistance(InNormalDistance),
		  FastDistance(InFastDistance)
	{
	}

	bool Update() override
	{
		Test->TestTrue(TEXT("normal mode should advance the ant at least a little"), *NormalDistance > 1.0f);
		Test->TestTrue(
			TEXT("fast world time should advance the ant farther than normal over the same wall-clock window"),
			*FastDistance > (*NormalDistance + 10.0f));
		return true;
	}

private:
	FAutomationTestBase* Test;
	const float* NormalDistance;
	const float* FastDistance;
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

	float NormalDistance = 0.0f;
	float FastDistance = 0.0f;

	ADD_LATENT_AUTOMATION_COMMAND(FGatherersPrepareTimeControlFixtureCommand(this, EGatherersTimeControlMode::Normal));
	ADD_LATENT_AUTOMATION_COMMAND(FGatherersObserveTimeControlDistanceCommand(this, ObservationWindowSeconds, &NormalDistance));
	ADD_LATENT_AUTOMATION_COMMAND(FGatherersPrepareTimeControlFixtureCommand(this, EGatherersTimeControlMode::Fast));
	ADD_LATENT_AUTOMATION_COMMAND(FGatherersObserveTimeControlDistanceCommand(this, ObservationWindowSeconds, &FastDistance));
	ADD_LATENT_AUTOMATION_COMMAND(FGatherersAssertFastModeAdvancesFurtherCommand(this, &NormalDistance, &FastDistance));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	ADD_LATENT_AUTOMATION_COMMAND(FGatherersWaitForTimeControlPIECleanupCommand(this, FPlatformTime::Seconds(), 5.0));
	return true;
}
