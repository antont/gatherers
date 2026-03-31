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

FVector ComputeAntTurnDirection(
	const FVector& CurrentDirection,
	float NormalizedJitterAlpha,
	float MaxTurnJitterRadians)
{
	const float ClampedJitterAlpha = FMath::Clamp(NormalizedJitterAlpha, -1.0f, 1.0f);
	return ComputeAntRetargetDirection(CurrentDirection, ClampedJitterAlpha * FMath::Max(0.0f, MaxTurnJitterRadians));
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

float ComputePickupCooldownForSeparationDistance(
	float DesiredSeparationDistance,
	float MovementSpeed)
{
	const float SafeMovementSpeed = FMath::Max(0.0f, MovementSpeed);
	if (SafeMovementSpeed <= KINDA_SMALL_NUMBER)
	{
		return 0.0f;
	}

	return FMath::Max(0.0f, DesiredSeparationDistance) / SafeMovementSpeed;
}

FVector ComputeBoundaryTurnBackDirection(
	const FVector& CurrentDirection,
	const FVector& InwardBoundaryNormal)
{
	const FVector SafeDirection = CurrentDirection.GetSafeNormal();
	const FVector SafeNormal = InwardBoundaryNormal.GetSafeNormal();
	if (SafeDirection.IsNearlyZero() || SafeNormal.IsNearlyZero())
	{
		return FVector::ZeroVector;
	}

	const FVector ReflectedDirection = SafeDirection - 2.0f * FVector::DotProduct(SafeDirection, SafeNormal) * SafeNormal;
	return ReflectedDirection.GetSafeNormal();
}

FVector ComputeCarriedFoodRelativeLocation(float CarriedFoodHeight)
{
	return FVector(0.0f, 0.0f, CarriedFoodHeight);
}
