#include "Actors/Ant.h"
#include "Actors/Food.h"
#include "HAL/PlatformTime.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"
#include "Simulation/GatherersSpawnPlan.h"
#include "Tests/AutomationCommon.h"

DEFINE_LATENT_AUTOMATION_COMMAND_THREE_PARAMETER(
	FGatherersWaitForStartupActorsCommand,
	FAutomationTestBase*,
	Test,
	double,
	StartTimeSeconds,
	double,
	TimeoutSeconds);

bool FGatherersWaitForStartupActorsCommand::Update()
{
	constexpr float PositionTolerance = KINDA_SMALL_NUMBER;
	UWorld* World = AutomationCommon::GetAnyGameWorld();
	Test->TestNotNull(TEXT("game world should exist after AutomationOpenMap"), World);

	if (World == nullptr)
	{
		return true;
	}

	const FGatherersSpawnPlan Plan = BuildInitialGatherersSpawnPlan();
	TArray<AAnt*> AntsInWorld;
	for (TActorIterator<AAnt> It(World); It; ++It)
	{
		AntsInWorld.Add(*It);
	}

	TArray<AFood*> FoodsInWorld;
	for (TActorIterator<AFood> It(World); It; ++It)
	{
		FoodsInWorld.Add(*It);
	}

	const bool bHasExpectedCounts = AntsInWorld.Num() == 1 && FoodsInWorld.Num() == 1;
	const bool bAntMoved = bHasExpectedCounts
		&& !AntsInWorld[0]->GetActorLocation().Equals(Plan.AntSpawns[0].GetLocation(), PositionTolerance);
	const bool bFoodAttachedToAnt = bHasExpectedCounts && FoodsInWorld[0]->GetAttachParentActor() == AntsInWorld[0];

	if (!bHasExpectedCounts || !bAntMoved || !bFoodAttachedToAnt)
	{
		if (FPlatformTime::Seconds() - StartTimeSeconds < TimeoutSeconds)
		{
			return false;
		}

		Test->TestEqual(TEXT("startup ant count"), AntsInWorld.Num(), 1);
		Test->TestEqual(TEXT("startup food count"), FoodsInWorld.Num(), 1);
		Test->TestTrue(TEXT("startup ant moves from its spawn point"), bAntMoved);
		Test->TestTrue(TEXT("startup food attaches to the ant"), bFoodAttachedToAnt);
		return true;
	}

	Test->TestTrue(TEXT("startup ant world matches PIE world"), AntsInWorld[0]->GetWorld() == World);
	Test->TestTrue(TEXT("startup food world matches PIE world"), FoodsInWorld[0]->GetWorld() == World);
	Test->TestTrue(
		TEXT("startup ant moves away from its spawn plan position"),
		!AntsInWorld[0]->GetActorLocation().Equals(Plan.AntSpawns[0].GetLocation(), PositionTolerance));
	Test->TestTrue(TEXT("startup food is attached to the ant"), FoodsInWorld[0]->GetAttachParentActor() == AntsInWorld[0]);
	Test->TestTrue(
		TEXT("startup food no longer remains at its spawn plan position"),
		!FoodsInWorld[0]->GetActorLocation().Equals(Plan.FoodSpawns[0].GetLocation(), PositionTolerance));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGatherersStartupSmokeAutomationTest,
	"unreal_gatherers.Spawning.StartupSmokeSpawnsOneAntAndOneFood",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGatherersStartupSmokeAutomationTest::RunTest(const FString& Parameters)
{
	const bool bOpenedMap = AutomationOpenMap(TEXT("/Game/SimBlank/Levels/SimBlank"));
	TestTrue(TEXT("should open SimBlank map"), bOpenedMap);

	if (!bOpenedMap)
	{
		return false;
	}

	ADD_LATENT_AUTOMATION_COMMAND(FGatherersWaitForStartupActorsCommand(this, FPlatformTime::Seconds(), 5.0));
	return true;
}
