#pragma once

#include "CoreMinimal.h"

struct UNREAL_GATHERERS_API FGatherersFoodTarget
{
	FVector Location;
	bool bIsLoose = false;
};

UNREAL_GATHERERS_API FVector ComputeAntNextLocation(
	const FVector& CurrentLocation,
	const FVector& FoodLocation,
	float MovementSpeed,
	float DeltaSeconds);

UNREAL_GATHERERS_API FVector ComputeAntHeadingMovementStep(
	const FVector& CurrentLocation,
	const FVector& HeadingDirection,
	float MovementSpeed,
	float SafeStepDistance,
	float DeltaSeconds);

UNREAL_GATHERERS_API bool ShouldAntPickUpFood(
	const FVector& AntLocation,
	const FVector& FoodLocation,
	float PickupRadius);

UNREAL_GATHERERS_API FVector ComputeAntRetargetDirection(
	const FVector& CurrentDirection,
	float RetargetJitterRadians);

UNREAL_GATHERERS_API FVector ComputeAntTurnDirection(
	const FVector& CurrentDirection,
	float NormalizedJitterAlpha,
	float MaxTurnJitterRadians);

UNREAL_GATHERERS_API int32 FindClosestLooseFoodTargetIndex(
	const FVector& AntLocation,
	const TArray<FGatherersFoodTarget>& FoodTargets);

UNREAL_GATHERERS_API float ComputeRemainingPickupCooldown(
	float CurrentCooldownSeconds,
	float DeltaSeconds);

UNREAL_GATHERERS_API FVector ComputeCarriedFoodRelativeLocation(float CarriedFoodHeight);
