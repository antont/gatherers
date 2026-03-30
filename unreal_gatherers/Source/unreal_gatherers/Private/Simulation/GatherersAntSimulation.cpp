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

FVector ComputeAntRetargetDirection(
	const FVector& CurrentDirection,
	float RetargetJitterRadians)
{
	const FVector SafeDirection = CurrentDirection.GetSafeNormal();
	if (SafeDirection.IsNearlyZero())
	{
		return FVector::ZeroVector;
	}

	const float CurrentAngle = FMath::Atan2(SafeDirection.Y, SafeDirection.X);
	const float RetargetAngle = CurrentAngle + PI + RetargetJitterRadians;
	return FVector(FMath::Cos(RetargetAngle), FMath::Sin(RetargetAngle), 0.0f).GetSafeNormal();
}
