// Copyright Epic Games, Inc. All Rights Reserved.


#include "unreal_gatherersGameModeBase.h"

#include "HAL/PlatformTime.h"
#include "Simulation/GatherersSpawnPlan.h"
#include "Simulation/GatherersWorldSpawner.h"

namespace
{
const FBox DefaultGamePlayAreaBounds(FVector(-640.0f, -360.0f, -100.0f), FVector(640.0f, 360.0f, 100.0f));
}

void Aunreal_gatherersGameModeBase::StartPlay()
{
	Super::StartPlay();

	if (UWorld* World = GetWorld())
	{
		SpawnGatherersActors(
			*World,
			BuildDefaultGameFullSimulationSpawnPlan(DefaultGamePlayAreaBounds, static_cast<int32>(FPlatformTime::Cycles())));
	}
}

