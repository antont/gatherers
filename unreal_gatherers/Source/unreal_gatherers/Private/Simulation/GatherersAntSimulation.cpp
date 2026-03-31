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

FVector ComputeAntHeadingMovementStep(
	const FVector& CurrentLocation,
	const FVector& HeadingDirection,
	float MovementSpeed,
	float SafeStepDistance,
	float DeltaSeconds)
{
	const FVector SafeHeading = HeadingDirection.GetSafeNormal();
	if (SafeHeading.IsNearlyZero())
	{
		return CurrentLocation;
	}

	const float MaxDistanceThisFrame = FMath::Max(0.0f, MovementSpeed) * FMath::Max(0.0f, DeltaSeconds);
	const float ClampedStepDistance = FMath::Min(MaxDistanceThisFrame, FMath::Max(0.0f, SafeStepDistance));
	return CurrentLocation + SafeHeading * ClampedStepDistance;
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

int32 FindClosestLooseFoodTargetIndex(
	const FVector& AntLocation,
	const TArray<FGatherersFoodTarget>& FoodTargets)
{
	int32 ClosestLooseFoodIndex = INDEX_NONE;
	float ClosestDistanceSquared = TNumericLimits<float>::Max();

	for (int32 FoodIndex = 0; FoodIndex < FoodTargets.Num(); ++FoodIndex)
	{
		const FGatherersFoodTarget& FoodTarget = FoodTargets[FoodIndex];
		if (!FoodTarget.bIsLoose)
		{
			continue;
		}

		const float DistanceSquared = FVector::DistSquared(AntLocation, FoodTarget.Location);
		if (DistanceSquared < ClosestDistanceSquared)
		{
			ClosestDistanceSquared = DistanceSquared;
			ClosestLooseFoodIndex = FoodIndex;
		}
	}

	return ClosestLooseFoodIndex;
}

float ComputeRemainingPickupCooldown(
	float CurrentCooldownSeconds,
	float DeltaSeconds)
{
	return FMath::Max(0.0f, CurrentCooldownSeconds - FMath::Max(0.0f, DeltaSeconds));
}

FVector ComputeCarriedFoodRelativeLocation(float CarriedFoodHeight)
{
	return FVector(0.0f, 0.0f, CarriedFoodHeight);
}
