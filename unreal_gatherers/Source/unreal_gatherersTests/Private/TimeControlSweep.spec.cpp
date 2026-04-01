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
#include "TestLogic/GatherersWorldAssertions.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"
#include "Templates/SharedPointer.h"

namespace
{
constexpr TCHAR SimBlankMapPath[] = TEXT("/Game/SimBlank/Levels/SimBlank");
constexpr double SweepObservationWindowSeconds = 0.25;
constexpr int32 SweepFixtureAntCount = 8;
constexpr int32 SweepFixtureFoodCount = 8;
constexpr int32 MaxSweepDilation = 1024;

struct FTimeDilationSweepCandidateResult
{
	int32 Dilation = 1;
	bool bPassed = false;
	float AccumulatedSimulationSeconds = 0.0f;
	int32 CarriedFoodCount = 0;
	int32 LooseFoodCount = 0;
};

struct FTimeDilationSweepResults
{
	TArray<FTimeDilationSweepCandidateResult> CandidateResults;
	int32 HighestPassingDilation = 0;
	bool bReachedMaxTestedDilation = false;
};

FGatherersSpawnPlan BuildTimeDilationSweepFixturePlan()
{
	FGatherersSpawnPlan Plan;
	Plan.bUseFullSimulationMode = true;
	Plan.bSpawnActorVisuals = false;
	Plan.PlayAreaBounds = FBox(FVector(-500.0f, -500.0f, -100.0f), FVector(500.0f, 500.0f, 100.0f));
	Plan.FullSimulationMovementSpeed = 100.0f;
	Plan.FullSimulationTurnJitterRadians = 0.0f;

	for (int32 AntIndex = 0; AntIndex < SweepFixtureAntCount; ++AntIndex)
	{
		const float RowY = -175.0f + (AntIndex * 50.0f);
		Plan.AntSpawns.Add(FTransform(FVector::ZeroVector + FVector(0.0f, RowY, 0.0f)));
		Plan.AntInitialDirections.Add(FVector(1.0f, 0.0f, 0.0f));
		Plan.FoodSpawns.Add(FTransform(FVector(0.0f, RowY + 14.0f, 0.0f)));
	}

	return Plan;
}

FTimeDilationSweepCandidateResult EvaluateSweepCandidate(UGatherersMassSubsystem& MassSubsystem, UMassEntitySubsystem& MassEntitySubsystem, int32 Dilation)
{
	FTimeDilationSweepCandidateResult Result;
	Result.Dilation = Dilation;
	Result.AccumulatedSimulationSeconds = MassSubsystem.GetAccumulatedSimulationSeconds();

	if (MassSubsystem.GetManagedAntCount() != SweepFixtureAntCount || MassSubsystem.GetManagedFoodCount() != SweepFixtureFoodCount)
	{
		return Result;
	}

	bool bAllAntPositionsFinite = true;
	FMassEntityManager& EntityManager = MassEntitySubsystem.GetMutableEntityManager();
	for (const FMassEntityHandle AntEntity : MassSubsystem.ManagedAntEntities)
	{
		if (!EntityManager.IsEntityValid(AntEntity))
		{
			bAllAntPositionsFinite = false;
			break;
		}

		FMassEntityView AntView(EntityManager, AntEntity);
		const FGatherersMassAntFragment& AntFragment = AntView.GetFragmentData<FGatherersMassAntFragment>();
		bAllAntPositionsFinite &= FMath::IsFinite(AntFragment.Position.X);
		bAllAntPositionsFinite &= FMath::IsFinite(AntFragment.Position.Y);
		bAllAntPositionsFinite &= FMath::IsFinite(AntFragment.Position.Z);
		if (AntFragment.CarriedFoodEntity.IsValid())
		{
			++Result.CarriedFoodCount;
		}
	}

	for (const FMassEntityHandle FoodEntity : MassSubsystem.ManagedFoodEntities)
	{
		if (!EntityManager.IsEntityValid(FoodEntity))
		{
			bAllAntPositionsFinite = false;
			break;
		}

		FMassEntityView FoodView(EntityManager, FoodEntity);
		const FGatherersMassFoodFragment& FoodFragment = FoodView.GetFragmentData<FGatherersMassFoodFragment>();
		if (FoodFragment.bIsLoose)
		{
			++Result.LooseFoodCount;
		}
	}

	const float ExpectedSimulatedSecondsLowerBound = static_cast<float>(SweepObservationWindowSeconds * Dilation * 0.75);
	Result.bPassed = bAllAntPositionsFinite
		&& Result.CarriedFoodCount == SweepFixtureFoodCount
		&& Result.LooseFoodCount == 0
		&& Result.AccumulatedSimulationSeconds >= ExpectedSimulatedSecondsLowerBound;
	return Result;
}

class FGatherersRunTimeDilationSweepCommand : public IAutomationLatentCommand
{
public:
	FGatherersRunTimeDilationSweepCommand(
		FAutomationTestBase* InTest,
		double InObservationWindowSeconds,
		TSharedRef<FTimeDilationSweepResults> InResults)
		: Test(InTest),
		  ObservationWindowSeconds(InObservationWindowSeconds),
		  Results(InResults)
	{
	}

	bool Update() override
	{
		UWorld* World = GEditor ? GEditor->PlayWorld : nullptr;
		Test->TestNotNull(TEXT("time-dilation sweep play world should exist"), World);
		if (World == nullptr)
		{
			return true;
		}

		UGatherersMassSubsystem* MassSubsystem = World->GetSubsystem<UGatherersMassSubsystem>();
		UMassEntitySubsystem* MassEntitySubsystem = World->GetSubsystem<UMassEntitySubsystem>();
		Test->TestNotNull(TEXT("time-rate sweep Mass subsystem should exist"), MassSubsystem);
		Test->TestNotNull(TEXT("time-rate sweep Mass entity subsystem should exist"), MassEntitySubsystem);
		if (MassSubsystem == nullptr || MassEntitySubsystem == nullptr || World->GetWorldSettings() == nullptr)
		{
			return true;
		}

		if (!bCandidatePrepared)
		{
			World->GetWorldSettings()->SetTimeDilation(1.0f);
			MassSubsystem->SetSimulationRateMultiplier(static_cast<float>(CurrentCandidateDilation));
			MassSubsystem->ResetSimulation();
			SpawnGatherersActors(*World, BuildTimeDilationSweepFixturePlan());
			CandidateStartTimeSeconds = FPlatformTime::Seconds();
			bCandidatePrepared = true;
			return false;
		}

		if (FPlatformTime::Seconds() - CandidateStartTimeSeconds.GetValue() < ObservationWindowSeconds)
		{
			return false;
		}

		const FTimeDilationSweepCandidateResult CandidateResult =
			EvaluateSweepCandidate(*MassSubsystem, *MassEntitySubsystem, CurrentCandidateDilation);
		Results->CandidateResults.Add(CandidateResult);
		Test->AddInfo(FString::Printf(
			TEXT("time-rate sweep candidate %dx: %s (sim_seconds=%.3f carried=%d loose=%d)"),
			CandidateResult.Dilation,
			CandidateResult.bPassed ? TEXT("PASS") : TEXT("FAIL"),
			CandidateResult.AccumulatedSimulationSeconds,
			CandidateResult.CarriedFoodCount,
			CandidateResult.LooseFoodCount));

		if (CandidateResult.bPassed)
		{
			Results->HighestPassingDilation = FMath::Max(Results->HighestPassingDilation, CurrentCandidateDilation);
		}

		MassSubsystem->ResetSimulation();
		MassSubsystem->SetSimulationRateMultiplier(1.0f);
		World->GetWorldSettings()->SetTimeDilation(1.0f);
		bCandidatePrepared = false;
		CandidateStartTimeSeconds.Reset();

		if (!bBinarySearch)
		{
			if (CandidateResult.bPassed)
			{
				if (CurrentCandidateDilation >= MaxSweepDilation)
				{
					Results->bReachedMaxTestedDilation = true;
					return true;
				}

				const int32 NextCandidate = CurrentCandidateDilation * 2;
				if (NextCandidate > MaxSweepDilation)
				{
					Results->bReachedMaxTestedDilation = true;
					return true;
				}

				PreviousPassingDilation = CurrentCandidateDilation;
				CurrentCandidateDilation = NextCandidate;
				return false;
			}

			if (PreviousPassingDilation == 0 || PreviousPassingDilation + 1 > CurrentCandidateDilation - 1)
			{
				return true;
			}

			bBinarySearch = true;
			BinarySearchLow = PreviousPassingDilation + 1;
			BinarySearchHigh = CurrentCandidateDilation - 1;
			CurrentCandidateDilation = (BinarySearchLow + BinarySearchHigh) / 2;
			return false;
		}

		if (CandidateResult.bPassed)
		{
			BinarySearchLow = CurrentCandidateDilation + 1;
		}
		else
		{
			BinarySearchHigh = CurrentCandidateDilation - 1;
		}

		if (BinarySearchLow > BinarySearchHigh)
		{
			return true;
		}

		CurrentCandidateDilation = (BinarySearchLow + BinarySearchHigh) / 2;
		return false;
	}

private:
	FAutomationTestBase* Test;
	double ObservationWindowSeconds;
	TSharedRef<FTimeDilationSweepResults> Results;
	int32 CurrentCandidateDilation = 1;
	int32 PreviousPassingDilation = 0;
	int32 BinarySearchLow = 0;
	int32 BinarySearchHigh = 0;
	bool bBinarySearch = false;
	bool bCandidatePrepared = false;
	TOptional<double> CandidateStartTimeSeconds;
};

class FGatherersAssertTimeDilationSweepResultsCommand : public IAutomationLatentCommand
{
public:
	FGatherersAssertTimeDilationSweepResultsCommand(
		FAutomationTestBase* InTest,
		TSharedRef<FTimeDilationSweepResults> InResults)
		: Test(InTest),
		  Results(InResults)
	{
	}

	bool Update() override
	{
		Test->TestTrue(TEXT("time-rate sweep should evaluate at least one candidate"), Results->CandidateResults.Num() > 0);
		Test->TestTrue(TEXT("time-rate sweep should find at least a modest passing rate"), Results->HighestPassingDilation >= 4);
		Test->AddInfo(FString::Printf(
			TEXT("time-rate sweep highest passing sim rate: %dx%s"),
			Results->HighestPassingDilation,
			Results->bReachedMaxTestedDilation ? TEXT(" (reached max tested candidate)") : TEXT("")));
		return true;
	}

private:
	FAutomationTestBase* Test;
	TSharedRef<FTimeDilationSweepResults> Results;
};

DEFINE_LATENT_AUTOMATION_COMMAND_THREE_PARAMETER(
	FGatherersWaitForSweepPIECleanupCommand,
	FAutomationTestBase*,
	Test,
	double,
	StartTimeSeconds,
	double,
	TimeoutSeconds);

bool FGatherersWaitForSweepPIECleanupCommand::Update()
{
	return GatherersWorldAssertions::PollForPIEToEnd(
		*Test,
		SimBlankMapPath,
		StartTimeSeconds,
		TimeoutSeconds,
		TEXT("time-dilation sweep"));
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGatherersTimeControlSweepAutomationTest,
	"supplemental.unreal_gatherers.TimeControl.MaxCorrectSpeedSweepReportsHighestPassingDilation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGatherersTimeControlSweepAutomationTest::RunTest(const FString& Parameters)
{
	const bool bOpenedMap = AutomationOpenMap(SimBlankMapPath);
	TestTrue(TEXT("should open SimBlank map"), bOpenedMap);
	if (!bOpenedMap)
	{
		return false;
	}

	const TSharedRef<FTimeDilationSweepResults> SweepResults = MakeShared<FTimeDilationSweepResults>();
	ADD_LATENT_AUTOMATION_COMMAND(
		FGatherersRunTimeDilationSweepCommand(this, SweepObservationWindowSeconds, SweepResults));
	ADD_LATENT_AUTOMATION_COMMAND(FGatherersAssertTimeDilationSweepResultsCommand(this, SweepResults));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	ADD_LATENT_AUTOMATION_COMMAND(FGatherersWaitForSweepPIECleanupCommand(this, FPlatformTime::Seconds(), 5.0));
	return true;
}
