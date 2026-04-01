#include "Editor.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/AutomationTest.h"
#include "Simulation/GatherersMassSubsystem.h"
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
	"default.unreal_gatherers.Visual.MassInstancedVisualsExposeRustColorVisuals",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGatherersActorVisualsAutomationTest::RunTest(const FString& Parameters)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	TestNotNull(TEXT("editor world should exist"), World);

	if (World == nullptr)
	{
		return false;
	}

	UGatherersMassSubsystem* MassSubsystem = World->GetSubsystem<UGatherersMassSubsystem>();
	TestNotNull(TEXT("gatherers Mass subsystem should exist"), MassSubsystem);

	if (MassSubsystem == nullptr)
	{
		return false;
	}

	MassSubsystem->ResetSimulation();

	FGatherersSpawnPlan Plan = BuildInitialGatherersSpawnPlan();
	Plan.bSpawnActorVisuals = false;
	const FGatherersSpawnResult Result = SpawnGatherersActors(*World, Plan);
	TestEqual(TEXT("spawned ant actor count"), Result.Ants.Num(), 0);
	TestEqual(TEXT("spawned food actor count"), Result.Foods.Num(), 0);
	TestEqual(TEXT("Mass subsystem tracks one ant entity"), MassSubsystem->GetManagedAntCount(), 1);
	TestEqual(TEXT("Mass subsystem tracks two food entities"), MassSubsystem->GetManagedFoodCount(), 2);
	MassSubsystem->Tick(0.1f);

	if (MassSubsystem->ManagedAntEntities.Num() != 1 || MassSubsystem->ManagedFoodEntities.Num() != 2)
	{
		return false;
	}

	const UInstancedStaticMeshComponent* AntVisual = MassSubsystem->GetAntVisualComponent();
	const UInstancedStaticMeshComponent* FoodVisual = MassSubsystem->GetFoodRepresentationComponent();
	TestNotNull(TEXT("ant should have a visible instanced mesh component"), const_cast<UInstancedStaticMeshComponent*>(AntVisual));
	TestNotNull(TEXT("food should have a visible instanced mesh component"), const_cast<UInstancedStaticMeshComponent*>(FoodVisual));

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
	MassSubsystem->ResetSimulation();
	return true;
}
