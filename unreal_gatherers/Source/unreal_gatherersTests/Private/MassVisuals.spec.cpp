#include "Actors/Ant.h"
#include "Actors/Food.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Editor.h"
#include "HAL/PlatformTime.h"
#include "MassEntitySubsystem.h"
#include "MassEntityView.h"
#include "Misc/AutomationTest.h"
#include "Simulation/GatherersMassSubsystem.h"
#include "Simulation/GatherersSpawnPlan.h"
#include "Simulation/GatherersWorldSpawner.h"
#include "TestLogic/GatherersWorldAssertions.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGatherersMassVisualsAutomationTest,
	"default.unreal_gatherers.Mass.MassInstancedVisualsCreateVisibleAntAndFoodInstances",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(
	FGatherersPrepareMassVisualStabilityFixtureCommand,
	FAutomationTestBase*,
	Test);

class FGatherersWaitForStableMassVisualFramesCommand : public IAutomationLatentCommand
{
public:
	FGatherersWaitForStableMassVisualFramesCommand(FAutomationTestBase* InTest, double InTimeoutSeconds)
		: Test(InTest),
		  TimeoutSeconds(InTimeoutSeconds),
		  StartTimeSeconds(FPlatformTime::Seconds())
	{
	}

	virtual bool Update() override
	{
		UWorld* World = GEditor ? GEditor->PlayWorld : nullptr;
		Test->TestNotNull(TEXT("Mass visual stability world should exist"), World);
		if (World == nullptr)
		{
			return true;
		}

		const GatherersWorldAssertions::FObservedMassVisualState VisualState = GatherersWorldAssertions::ObserveMassVisuals(World);
		if (VisualState.HasSingleAntAndOneFood())
		{
			const bool bVisualsMatchSimulation = VisualState.SingleAntAndFoodVisualsMatchSimulation(20.0f, 0.001f);
			Test->TestTrue(TEXT("Mass visuals should match simulation transforms every live frame"), bVisualsMatchSimulation);
			if (!bVisualsMatchSimulation)
			{
				return true;
			}

			if (PreviousFoodVisualPosition.IsSet())
			{
				const bool bLooseFoodVisualStable = VisualState.FoodVisualPositions[0].Equals(
					PreviousFoodVisualPosition.GetValue(),
					0.001f);
				Test->TestTrue(TEXT("loose food visual should remain stable across live frames"), bLooseFoodVisualStable);
				if (!bLooseFoodVisualStable)
				{
					return true;
				}
			}

			PreviousFoodVisualPosition = VisualState.FoodVisualPositions[0];
			++ObservedFrames;
			if (ObservedFrames >= 60)
			{
				return true;
			}
		}

		if (FPlatformTime::Seconds() - StartTimeSeconds < TimeoutSeconds)
		{
			return false;
		}

		Test->AddError(TEXT("Mass visual stability fixture did not produce enough observed frames"));
		return true;
	}

private:
	FAutomationTestBase* Test;
	double TimeoutSeconds;
	double StartTimeSeconds;
	int32 ObservedFrames = 0;
	TOptional<FVector> PreviousFoodVisualPosition;
};

DEFINE_LATENT_AUTOMATION_COMMAND_THREE_PARAMETER(
	FGatherersWaitForMassVisualStabilityPIECleanupCommand,
	FAutomationTestBase*,
	Test,
	double,
	StartTimeSeconds,
	double,
	TimeoutSeconds);

bool FGatherersWaitForMassVisualStabilityPIECleanupCommand::Update()
{
	return GatherersWorldAssertions::PollForPIEToEnd(
		*Test,
		TEXT("/Game/SimBlank/Levels/SimBlank"),
		StartTimeSeconds,
		TimeoutSeconds,
		TEXT("mass-visual-stability"));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGatherersMassVisualStabilityAutomationTest,
	"supplemental.unreal_gatherers.Mass.MassVisualsStayStableAcrossLiveFrames",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGatherersMassVisualsAutomationTest::RunTest(const FString& Parameters)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	TestNotNull(TEXT("editor world should exist"), World);

	if (World == nullptr)
	{
		return false;
	}

	UGatherersMassSubsystem* MassSubsystem = World->GetSubsystem<UGatherersMassSubsystem>();
	UMassEntitySubsystem* MassEntitySubsystem = World->GetSubsystem<UMassEntitySubsystem>();
	TestNotNull(TEXT("gatherers Mass subsystem should exist"), MassSubsystem);
	TestNotNull(TEXT("Mass entity subsystem should exist"), MassEntitySubsystem);

	if (MassSubsystem == nullptr || MassEntitySubsystem == nullptr)
	{
		return false;
	}

	MassSubsystem->ResetSimulation();

	FGatherersSpawnPlan Plan;
	Plan.bUseFullSimulationMode = true;
	Plan.bSpawnActorVisuals = false;
	Plan.PlayAreaBounds = FBox(FVector(-500.0f, -500.0f, -100.0f), FVector(500.0f, 500.0f, 100.0f));
	Plan.AntSpawns.Add(FTransform(FVector::ZeroVector));
	Plan.AntInitialDirections.Add(FVector(1.0f, 0.0f, 0.0f));
	Plan.FoodSpawns.Add(FTransform(FVector(100.0f, 0.0f, 0.0f)));

	SpawnGatherersActors(*World, Plan);
	MassSubsystem->Tick(0.1f);

	const UInstancedStaticMeshComponent* AntVisual = MassSubsystem->GetAntVisualComponent();
	const UInstancedStaticMeshComponent* FoodVisual = MassSubsystem->GetFoodRepresentationComponent();
	TestNotNull(TEXT("ant instanced visual should exist on the gatherers Mass subsystem"), const_cast<UInstancedStaticMeshComponent*>(AntVisual));
	TestNotNull(TEXT("food instanced visual should exist on the gatherers Mass subsystem"), const_cast<UInstancedStaticMeshComponent*>(FoodVisual));

	if (AntVisual != nullptr)
	{
		TestEqual(TEXT("ant instanced visual should contain one rendered ant instance"), AntVisual->GetInstanceCount(), 1);
	}

	if (FoodVisual != nullptr)
	{
		TestEqual(TEXT("food instanced visual should contain one rendered food instance"), FoodVisual->GetInstanceCount(), 1);
	}

	MassSubsystem->ResetSimulation();
	return true;
}

bool FGatherersPrepareMassVisualStabilityFixtureCommand::Update()
{
	UWorld* World = GEditor ? GEditor->PlayWorld : nullptr;
	Test->TestNotNull(TEXT("Mass visual stability play world should exist"), World);
	if (World == nullptr)
	{
		return true;
	}

	UGatherersMassSubsystem* MassSubsystem = World->GetSubsystem<UGatherersMassSubsystem>();
	Test->TestNotNull(TEXT("Mass visual stability world should expose the gatherers Mass subsystem"), MassSubsystem);
	if (MassSubsystem == nullptr)
	{
		return true;
	}

	MassSubsystem->ResetSimulation();
	const GatherersWorldAssertions::FObservedWorldState ExistingActorState = GatherersWorldAssertions::Observe(World);
	for (AAnt* Ant : ExistingActorState.Ants)
	{
		if (Ant != nullptr)
		{
			Ant->Destroy();
		}
	}

	for (AFood* Food : ExistingActorState.Foods)
	{
		if (Food != nullptr)
		{
			Food->Destroy();
		}
	}

	FGatherersSpawnPlan Plan;
	Plan.bUseFullSimulationMode = true;
	Plan.bSpawnActorVisuals = false;
	Plan.PlayAreaBounds = FBox(FVector(-500.0f, -500.0f, -100.0f), FVector(500.0f, 500.0f, 100.0f));
	Plan.AntSpawns.Add(FTransform(FVector::ZeroVector));
	Plan.AntInitialDirections.Add(FVector(1.0f, 0.0f, 0.0f));
	Plan.FoodSpawns.Add(FTransform(FVector(0.0f, 200.0f, 0.0f)));

	const FGatherersSpawnResult Result = SpawnGatherersActors(*World, Plan);
	Test->TestEqual(TEXT("Mass visual stability fixture should not spawn ant actors"), Result.Ants.Num(), 0);
	Test->TestEqual(TEXT("Mass visual stability fixture should not spawn food actors"), Result.Foods.Num(), 0);
	Test->TestEqual(TEXT("Mass visual stability fixture managed ant count"), MassSubsystem->GetManagedAntCount(), 1);
	Test->TestEqual(TEXT("Mass visual stability fixture managed food count"), MassSubsystem->GetManagedFoodCount(), 1);
	return true;
}

bool FGatherersMassVisualStabilityAutomationTest::RunTest(const FString& Parameters)
{
	const bool bOpenedMap = AutomationOpenMap(TEXT("/Game/SimBlank/Levels/SimBlank"));
	TestTrue(TEXT("should open SimBlank map"), bOpenedMap);

	if (!bOpenedMap)
	{
		return false;
	}

	ADD_LATENT_AUTOMATION_COMMAND(FGatherersPrepareMassVisualStabilityFixtureCommand(this));
	ADD_LATENT_AUTOMATION_COMMAND(FGatherersWaitForStableMassVisualFramesCommand(this, 5.0));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	ADD_LATENT_AUTOMATION_COMMAND(FGatherersWaitForMassVisualStabilityPIECleanupCommand(this, FPlatformTime::Seconds(), 5.0));
	return true;
}
