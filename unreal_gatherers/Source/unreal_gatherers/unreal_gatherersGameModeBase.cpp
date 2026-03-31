// Copyright Epic Games, Inc. All Rights Reserved.


#include "unreal_gatherersGameModeBase.h"

#include "Simulation/GatherersSpawnPlan.h"
#include "Simulation/GatherersWorldSpawner.h"

void Aunreal_gatherersGameModeBase::StartPlay()
{
	Super::StartPlay();

	if (UWorld* World = GetWorld())
	{
		SpawnGatherersActors(*World, BuildInitialGatherersSpawnPlan());
	}
}

