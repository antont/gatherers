#pragma once

#include "CoreMinimal.h"

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

UNREAL_GATHERERS_API float ComputeRemainingPickupCooldown(
	float CurrentCooldownSeconds,
	float DeltaSeconds);

UNREAL_GATHERERS_API float ComputePickupCooldownForSeparationDistance(
	float DesiredSeparationDistance,
	float MovementSpeed);

UNREAL_GATHERERS_API FVector ComputeBoundaryTurnBackDirection(
	const FVector& CurrentDirection,
	const FVector& InwardBoundaryNormal);

UNREAL_GATHERERS_API FVector ComputeCarriedFoodRelativeLocation(float CarriedFoodHeight);
