#include "Actors/Food.h"

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
const FLinearColor FoodColor(192.0f / 255.0f, 2.0f / 255.0f, 2.0f / 255.0f, 1.0f);
const FVector FoodVisualScale(0.1f, 0.1f, 0.1f);
}

AFood::AFood()
{
	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	UStaticMeshComponent* Visual = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Visual"));
	Visual->SetupAttachment(Root);
	Visual->SetRelativeScale3D(FoodVisualScale);
	Visual->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (SphereMesh.Succeeded())
	{
		Visual->SetStaticMesh(SphereMesh.Object);
	}

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> BasicShapeMaterial(
		TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (BasicShapeMaterial.Succeeded())
	{
		UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(BasicShapeMaterial.Object, this);
		if (Material != nullptr)
		{
			Material->SetVectorParameterValue(TEXT("Color"), FoodColor);
			Visual->SetMaterial(0, Material);
		}
	}
}
