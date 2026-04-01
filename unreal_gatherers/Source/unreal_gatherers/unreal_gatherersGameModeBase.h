// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "unreal_gatherersGameModeBase.generated.h"

UENUM()
enum class EGatherersTimeControlMode : uint8
{
	Normal,
	Fast,
};

UCLASS()
class UNREAL_GATHERERS_API Aunreal_gatherersGameModeBase : public AGameModeBase
{
	GENERATED_BODY()

public:
	virtual void StartPlay() override;

	void ApplyTimeControlMode(EGatherersTimeControlMode NewMode);
	EGatherersTimeControlMode GetTimeControlMode() const;
	static float GetTimeDilationForMode(EGatherersTimeControlMode Mode);

private:
	UPROPERTY(EditDefaultsOnly, Category = "Simulation")
	EGatherersTimeControlMode StartupTimeControlMode = EGatherersTimeControlMode::Normal;

	UPROPERTY(Transient)
	EGatherersTimeControlMode CurrentTimeControlMode = EGatherersTimeControlMode::Normal;
};
