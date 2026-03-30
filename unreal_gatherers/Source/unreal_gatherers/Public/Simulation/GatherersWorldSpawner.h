#pragma once

#include "CoreMinimal.h"

class AAnt;
class AFood;
class UWorld;
struct FGatherersSpawnPlan;

struct UNREAL_GATHERERS_API FGatherersSpawnResult
{
	TArray<AAnt*> Ants;
	TArray<AFood*> Foods;
};

UNREAL_GATHERERS_API FGatherersSpawnResult SpawnGatherersActors(UWorld& World, const FGatherersSpawnPlan& Plan);
