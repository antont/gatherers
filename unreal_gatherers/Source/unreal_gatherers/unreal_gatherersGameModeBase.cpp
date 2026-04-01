// Copyright Epic Games, Inc. All Rights Reserved.


#include "unreal_gatherersGameModeBase.h"

#include "HAL/PlatformTime.h"
#include "GameFramework/WorldSettings.h"
#include "Simulation/GatherersSpawnPlan.h"
#include "Simulation/GatherersWorldSpawner.h"

namespace
{
const FBox DefaultGamePlayAreaBounds(FVector(-640.0f, -360.0f, -100.0f), FVector(640.0f, 360.0f, 100.0f));
constexpr float GatherersFastTimeDilation = 4.0f;
}

void Aunreal_gatherersGameModeBase::StartPlay()
{
	Super::StartPlay();

	if (UWorld* World = GetWorld())
	{
		ApplyTimeControlMode(StartupTimeControlMode);
		SpawnGatherersActors(
			*World,
			BuildDefaultGameFullSimulationSpawnPlan(DefaultGamePlayAreaBounds, static_cast<int32>(FPlatformTime::Cycles())));
	}
}

void Aunreal_gatherersGameModeBase::ApplyTimeControlMode(EGatherersTimeControlMode NewMode)
{
	CurrentTimeControlMode = NewMode;

	if (UWorld* World = GetWorld())
	{
		if (AWorldSettings* WorldSettings = World->GetWorldSettings())
		{
			WorldSettings->SetTimeDilation(GetTimeDilationForMode(NewMode));
		}
	}
}

EGatherersTimeControlMode Aunreal_gatherersGameModeBase::GetTimeControlMode() const
{
	return CurrentTimeControlMode;
}

float Aunreal_gatherersGameModeBase::GetTimeDilationForMode(EGatherersTimeControlMode Mode)
{
	switch (Mode)
	{
	case EGatherersTimeControlMode::Fast:
		return GatherersFastTimeDilation;

	case EGatherersTimeControlMode::Normal:
	default:
		return 1.0f;
	}
}

