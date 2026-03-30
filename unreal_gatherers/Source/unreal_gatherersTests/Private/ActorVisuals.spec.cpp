#include "Actors/Ant.h"
#include "Actors/Food.h"
#include "Components/StaticMeshComponent.h"
#include "Editor.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/AutomationTest.h"
#include "Simulation/GatherersSpawnPlan.h"
#include "Simulation/GatherersWorldSpawner.h"

namespace
{
constexpr TCHAR ColorParameterName[] = TEXT("Color");
const FLinearColor ExpectedAntColor(0.8f, 0.8f, 0.8f, 1.0f);
const FLinearColor ExpectedFoodColor(192.0f / 255.0f, 2.0f / 255.0f, 2.0f / 255.0f, 1.0f);

bool ColorsMatch(const FLinearColor& Left, const FLinearColor& Right)
{
	return Left.Equals(Right, KINDA_SMALL_NUMBER);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGatherersActorVisualsAutomationTest,
	"default.unreal_gatherers.Visual.ActorsExposeRustColorVisuals",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGatherersActorVisualsAutomationTest::RunTest(const FString& Parameters)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	TestNotNull(TEXT("editor world should exist"), World);

	if (World == nullptr)
	{
		return false;
	}

	const FGatherersSpawnResult Result = SpawnGatherersActors(*World, BuildInitialGatherersSpawnPlan());
	TestEqual(TEXT("spawned ant count"), Result.Ants.Num(), 1);
	TestEqual(TEXT("spawned food count"), Result.Foods.Num(), 1);

	if (Result.Ants.Num() != 1 || Result.Foods.Num() != 1)
	{
		return false;
	}

	UStaticMeshComponent* AntVisual = Result.Ants[0]->FindComponentByClass<UStaticMeshComponent>();
	UStaticMeshComponent* FoodVisual = Result.Foods[0]->FindComponentByClass<UStaticMeshComponent>();

	TestNotNull(TEXT("ant should have a visible mesh component"), AntVisual);
	TestNotNull(TEXT("food should have a visible mesh component"), FoodVisual);

	if (AntVisual != nullptr)
	{
		TestTrue(TEXT("ant visual should have a mesh"), AntVisual->GetStaticMesh() != nullptr);
		UMaterialInstanceDynamic* AntMaterial = Cast<UMaterialInstanceDynamic>(AntVisual->GetMaterial(0));
		TestNotNull(TEXT("ant visual should use a dynamic material"), AntMaterial);

		if (AntMaterial != nullptr)
		{
			TestTrue(
				TEXT("ant visual uses the Rust ant color"),
				ColorsMatch(AntMaterial->K2_GetVectorParameterValue(ColorParameterName), ExpectedAntColor));
		}
	}

	if (FoodVisual != nullptr)
	{
		TestTrue(TEXT("food visual should have a mesh"), FoodVisual->GetStaticMesh() != nullptr);
		UMaterialInstanceDynamic* FoodMaterial = Cast<UMaterialInstanceDynamic>(FoodVisual->GetMaterial(0));
		TestNotNull(TEXT("food visual should use a dynamic material"), FoodMaterial);

		if (FoodMaterial != nullptr)
		{
			TestTrue(
				TEXT("food visual uses the Rust food color"),
				ColorsMatch(FoodMaterial->K2_GetVectorParameterValue(ColorParameterName), ExpectedFoodColor));
		}
	}

	for (AAnt* Ant : Result.Ants)
	{
		if (Ant != nullptr)
		{
			Ant->Destroy();
		}
	}

	for (AFood* Food : Result.Foods)
	{
		if (Food != nullptr)
		{
			Food->Destroy();
		}
	}

	return true;
}
