#pragma once

#include "CoreMinimal.h"

struct UNREAL_GATHERERS_API FGatherersSpawnPlan
{
	TArray<FTransform> AntSpawns;
	TArray<FTransform> FoodSpawns;
};

UNREAL_GATHERERS_API FGatherersSpawnPlan BuildInitialGatherersSpawnPlan();
