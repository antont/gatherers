// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "unreal_gatherersGameModeBase.generated.h"

class UGatherersTimeControlWidget;

UENUM()
enum class EGatherersTimeControlMode : uint8
{
	Normal,
	Fast,
	VeryFast,
	Fastest,
};

UCLASS()
class UNREAL_GATHERERS_API Aunreal_gatherersGameModeBase : public AGameModeBase
{
	GENERATED_BODY()

public:
	Aunreal_gatherersGameModeBase();
	virtual void StartPlay() override;

	void ApplyTimeControlMode(EGatherersTimeControlMode NewMode);
	void ToggleTimeControlMode();
	static void ApplyTimeControlModeToWorld(UWorld& World, EGatherersTimeControlMode NewMode);
	static EGatherersTimeControlMode GetNextTimeControlMode(EGatherersTimeControlMode Mode);
	EGatherersTimeControlMode GetTimeControlMode() const;
	EGatherersTimeControlMode GetStartupTimeControlMode() const;
	UGatherersTimeControlWidget* GetTimeControlWidget() const;
	void SetTimeControlWidget(UGatherersTimeControlWidget* Widget);
	static float GetSimulationRateForMode(EGatherersTimeControlMode Mode);

private:
	void RefreshTimeControlWidget();

	UPROPERTY(EditDefaultsOnly, Category = "Simulation")
	EGatherersTimeControlMode StartupTimeControlMode = EGatherersTimeControlMode::Normal;

	UPROPERTY(Transient)
	EGatherersTimeControlMode CurrentTimeControlMode = EGatherersTimeControlMode::Normal;

	UPROPERTY(Transient)
	TObjectPtr<UGatherersTimeControlWidget> TimeControlWidget = nullptr;
};
