// Copyright Epic Games, Inc. All Rights Reserved.


#include "unreal_gatherersGameModeBase.h"

#include "HAL/PlatformTime.h"
#include "GameFramework/WorldSettings.h"
#include "Input/GatherersPlayerController.h"
#include "Simulation/GatherersSpawnPlan.h"
#include "Simulation/GatherersWorldSpawner.h"
#include "UI/GatherersTimeControlWidget.h"

namespace
{
const FBox DefaultGamePlayAreaBounds(FVector(-640.0f, -360.0f, -100.0f), FVector(640.0f, 360.0f, 100.0f));
constexpr float GatherersFastTimeDilation = 4.0f;
constexpr float GatherersMaxCorrectTimeDilation = 27.0f;
}

Aunreal_gatherersGameModeBase::Aunreal_gatherersGameModeBase()
{
	PlayerControllerClass = AGatherersPlayerController::StaticClass();
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
	if (UWorld* World = GetWorld())
	{
		ApplyTimeControlModeToWorld(*World, NewMode);
	}
	else
	{
		CurrentTimeControlMode = NewMode;
	}

	RefreshTimeControlWidget();
}

void Aunreal_gatherersGameModeBase::ToggleTimeControlMode()
{
	ApplyTimeControlMode(GetNextTimeControlMode(CurrentTimeControlMode));
}

void Aunreal_gatherersGameModeBase::ApplyTimeControlModeToWorld(UWorld& World, EGatherersTimeControlMode NewMode)
{
	if (AWorldSettings* WorldSettings = World.GetWorldSettings())
	{
		WorldSettings->SetTimeDilation(GetTimeDilationForMode(NewMode));
	}

	if (Aunreal_gatherersGameModeBase* GatherersGameMode = World.GetAuthGameMode<Aunreal_gatherersGameModeBase>())
	{
		GatherersGameMode->CurrentTimeControlMode = NewMode;
		GatherersGameMode->RefreshTimeControlWidget();
	}
}

EGatherersTimeControlMode Aunreal_gatherersGameModeBase::GetNextTimeControlMode(EGatherersTimeControlMode Mode)
{
	switch (Mode)
	{
	case EGatherersTimeControlMode::Normal:
		return EGatherersTimeControlMode::Fast;

	case EGatherersTimeControlMode::Fast:
		return EGatherersTimeControlMode::MaxCorrect;

	case EGatherersTimeControlMode::MaxCorrect:
	default:
		return EGatherersTimeControlMode::Normal;
	}
}

EGatherersTimeControlMode Aunreal_gatherersGameModeBase::GetTimeControlMode() const
{
	return CurrentTimeControlMode;
}

EGatherersTimeControlMode Aunreal_gatherersGameModeBase::GetStartupTimeControlMode() const
{
	return StartupTimeControlMode;
}

UGatherersTimeControlWidget* Aunreal_gatherersGameModeBase::GetTimeControlWidget() const
{
	return TimeControlWidget;
}

void Aunreal_gatherersGameModeBase::SetTimeControlWidget(UGatherersTimeControlWidget* Widget)
{
	TimeControlWidget = Widget;
	RefreshTimeControlWidget();
}

float Aunreal_gatherersGameModeBase::GetTimeDilationForMode(EGatherersTimeControlMode Mode)
{
	switch (Mode)
	{
	case EGatherersTimeControlMode::MaxCorrect:
		return GatherersMaxCorrectTimeDilation;

	case EGatherersTimeControlMode::Fast:
		return GatherersFastTimeDilation;

	case EGatherersTimeControlMode::Normal:
	default:
		return 1.0f;
	}
}

void Aunreal_gatherersGameModeBase::RefreshTimeControlWidget()
{
	if (TimeControlWidget != nullptr)
	{
		TimeControlWidget->InitializeForGameMode(*this);
	}
}

