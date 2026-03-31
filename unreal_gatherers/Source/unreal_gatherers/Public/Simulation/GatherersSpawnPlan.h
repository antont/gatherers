#pragma once

#include "CoreMinimal.h"

struct UNREAL_GATHERERS_API FGatherersSpawnPlan
{
	TArray<FTransform> AntSpawns;
	TArray<FVector> AntInitialDirections;
	TArray<FTransform> FoodSpawns;
	bool bUseFullSimulationMode = false;
	bool bSpawnActorVisuals = false;
	FBox PlayAreaBounds = FBox(EForceInit::ForceInit);
	int32 RandomSeedBase = 0;
	float FullSimulationMovementSpeed = 100.0f;
	float FullSimulationTurnJitterRadians = PI / 2.0f;
};

UNREAL_GATHERERS_API FGatherersSpawnPlan BuildInitialGatherersSpawnPlan();
UNREAL_GATHERERS_API FGatherersSpawnPlan BuildFullSimulationSpawnPlan(
	int32 AntCount,
	int32 FoodCount,
	int32 RandomSeed,
	const FBox& PlayAreaBounds);
UNREAL_GATHERERS_API FGatherersSpawnPlan BuildDefaultGameFullSimulationSpawnPlan(
	const FBox& PlayAreaBounds,
	int32 RandomSeed);
UNREAL_GATHERERS_API FGatherersSpawnPlan BuildFullSimulationVisualSpawnPlan();
