#include "Actors/Ant.h"

#include "Actors/Food.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "EngineUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Simulation/GatherersAntSimulation.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
constexpr float AntMovementSpeed = 100.0f;
constexpr float AntPickupRadius = 15.0f;
constexpr float CarriedFoodHeight = 20.0f;
constexpr float AntPickupCooldownSeconds = 0.5f;
constexpr float AntSafeStepDistance = 18.0f;
constexpr float AntPickupSeparationDistance = 50.0f;
constexpr float AntTurnJitterRadians = PI / 2.0f;
const FLinearColor AntColor(0.8f, 0.8f, 0.8f, 1.0f);
const FVector AntVisualScale(0.2f, 0.2f, 0.2f);
}

AAnt::AAnt()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	UStaticMeshComponent* Visual = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Visual"));
	Visual->SetupAttachment(Root);
	Visual->SetRelativeScale3D(AntVisualScale);
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
			Material->SetVectorParameterValue(TEXT("Color"), AntColor);
			Visual->SetMaterial(0, Material);
		}
	}
}

void AAnt::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	const FVector CurrentLocation = GetActorLocation();
	PickupCooldownRemainingSeconds = ComputeRemainingPickupCooldown(PickupCooldownRemainingSeconds, DeltaSeconds);

	if (bUseFullSimulationMode)
	{
		FVector NextLocation = ComputeAntHeadingMovementStep(
			CurrentLocation,
			MovementDirection,
			AntMovementSpeed,
			AntSafeStepDistance,
			DeltaSeconds);

		if (FullSimulationBounds.IsValid)
		{
			FVector InwardBoundaryNormal = FVector::ZeroVector;
			if (NextLocation.X < FullSimulationBounds.Min.X)
			{
				NextLocation.X = FullSimulationBounds.Min.X;
				InwardBoundaryNormal += FVector(1.0f, 0.0f, 0.0f);
			}
			else if (NextLocation.X > FullSimulationBounds.Max.X)
			{
				NextLocation.X = FullSimulationBounds.Max.X;
				InwardBoundaryNormal += FVector(-1.0f, 0.0f, 0.0f);
			}

			if (NextLocation.Y < FullSimulationBounds.Min.Y)
			{
				NextLocation.Y = FullSimulationBounds.Min.Y;
				InwardBoundaryNormal += FVector(0.0f, 1.0f, 0.0f);
			}
			else if (NextLocation.Y > FullSimulationBounds.Max.Y)
			{
				NextLocation.Y = FullSimulationBounds.Max.Y;
				InwardBoundaryNormal += FVector(0.0f, -1.0f, 0.0f);
			}

			if (!InwardBoundaryNormal.IsNearlyZero())
			{
				MovementDirection = ComputeBoundaryTurnBackDirection(MovementDirection, InwardBoundaryNormal);
			}
		}

		SetActorLocation(NextLocation);

		if (PickupCooldownRemainingSeconds > 0.0f)
		{
			return;
		}

		if (AFood* NearbyLooseFood = FindLooseFoodInPickupRadius())
		{
			MovementDirection = ComputeAntTurnDirection(
				MovementDirection,
				FullSimulationRandomStream.FRandRange(-1.0f, 1.0f),
				AntTurnJitterRadians);

			if (IsCarryingFood())
			{
				DropFood();
			}
			else
			{
				PickUpFood(*NearbyLooseFood);
			}
		}

		return;
	}

	if (IsCarryingFood())
	{
		SetActorLocation(CurrentLocation + MovementDirection.GetSafeNormal() * AntMovementSpeed * FMath::Max(0.0f, DeltaSeconds));

		AFood* DropTargetFood = FindClosestLooseFood();
		if (DropTargetFood != nullptr
			&& ShouldAntPickUpFood(GetActorLocation(), DropTargetFood->GetActorLocation(), AntPickupRadius))
		{
			DropFood();
		}

		return;
	}

	if (PickupCooldownRemainingSeconds > 0.0f)
	{
		SetActorLocation(CurrentLocation + MovementDirection.GetSafeNormal() * AntMovementSpeed * FMath::Max(0.0f, DeltaSeconds));
		return;
	}

	AFood* TargetFood = FindClosestLooseFood();
	if (TargetFood == nullptr)
	{
		return;
	}

	const FVector FoodLocation = TargetFood->GetActorLocation();
	const FVector ToFood = FoodLocation - CurrentLocation;
	if (!ToFood.IsNearlyZero())
	{
		MovementDirection = ToFood.GetSafeNormal();
	}

	if (ShouldAntPickUpFood(CurrentLocation, FoodLocation, AntPickupRadius))
	{
		MovementDirection = ComputeAntRetargetDirection(MovementDirection, 0.0f);
		PickUpFood(*TargetFood);
		return;
	}

	SetActorLocation(ComputeAntNextLocation(CurrentLocation, FoodLocation, AntMovementSpeed, DeltaSeconds));
}

void AAnt::ConfigureForFullSimulation(const FVector& InitialDirection, const FBox& PlayAreaBounds, int32 RandomSeed)
{
	bUseFullSimulationMode = true;
	MovementDirection = InitialDirection.GetSafeNormal();
	if (MovementDirection.IsNearlyZero())
	{
		MovementDirection = FVector(1.0f, 0.0f, 0.0f);
	}

	FullSimulationBounds = PlayAreaBounds;
	FullSimulationRandomStream.Initialize(RandomSeed);
}

AFood* AAnt::FindClosestLooseFood() const
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return nullptr;
	}

	AFood* ClosestFood = nullptr;
	float ClosestDistanceSquared = TNumericLimits<float>::Max();

	for (TActorIterator<AFood> It(World); It; ++It)
	{
		AFood* Food = *It;
		if (Food == nullptr || Food->GetAttachParentActor() != nullptr)
		{
			continue;
		}

		const float DistanceSquared = FVector::DistSquared(GetActorLocation(), Food->GetActorLocation());
		if (DistanceSquared < ClosestDistanceSquared)
		{
			ClosestDistanceSquared = DistanceSquared;
			ClosestFood = Food;
		}
	}

	return ClosestFood;
}

AFood* AAnt::FindLooseFoodInPickupRadius() const
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return nullptr;
	}

	for (TActorIterator<AFood> It(World); It; ++It)
	{
		AFood* Food = *It;
		if (Food == nullptr || Food->GetAttachParentActor() != nullptr)
		{
			continue;
		}

		if (ShouldAntPickUpFood(GetActorLocation(), Food->GetActorLocation(), AntPickupRadius))
		{
			return Food;
		}
	}

	return nullptr;
}

bool AAnt::IsCarryingFood() const
{
	return CarriedFood != nullptr;
}

void AAnt::PickUpFood(AFood& Food)
{
	CarriedFood = &Food;
	Food.AttachToComponent(GetRootComponent(), FAttachmentTransformRules::KeepWorldTransform);
	Food.SetActorRelativeLocation(ComputeCarriedFoodRelativeLocation(CarriedFoodHeight));
}

void AAnt::DropFood()
{
	if (CarriedFood == nullptr)
	{
		return;
	}

	CarriedFood->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	CarriedFood = nullptr;
	PickupCooldownRemainingSeconds = bUseFullSimulationMode
		? ComputePickupCooldownForSeparationDistance(AntPickupSeparationDistance, AntMovementSpeed)
		: AntPickupCooldownSeconds;

	if (!bUseFullSimulationMode)
	{
		MovementDirection = ComputeAntRetargetDirection(MovementDirection, 0.0f);
	}
}
