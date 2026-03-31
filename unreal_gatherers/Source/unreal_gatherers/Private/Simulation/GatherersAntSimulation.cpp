#include "Simulation/GatherersAntSimulation.h"

FVector ComputeAntNextLocation(
	const FVector& CurrentLocation,
	const FVector& FoodLocation,
	float MovementSpeed,
	float DeltaSeconds)
{
	const FVector ToFood = FoodLocation - CurrentLocation;
	const float MaxStepDistance = FMath::Max(0.0f, MovementSpeed) * FMath::Max(0.0f, DeltaSeconds);
	const float DistanceToFood = ToFood.Length();

	if (DistanceToFood <= KINDA_SMALL_NUMBER || MaxStepDistance <= 0.0f)
	{
		return CurrentLocation;
	}

	if (DistanceToFood <= MaxStepDistance)
	{
		return FoodLocation;
	}

	return CurrentLocation + ToFood.GetSafeNormal() * MaxStepDistance;
}

bool ShouldAntPickUpFood(
	const FVector& AntLocation,
	const FVector& FoodLocation,
	float PickupRadius)
{
	const float EffectivePickupRadius = FMath::Max(0.0f, PickupRadius);
	return FVector::DistSquared(AntLocation, FoodLocation) <= FMath::Square(EffectivePickupRadius);
}
