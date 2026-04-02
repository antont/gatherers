#pragma once

#include "GameFramework/PlayerController.h"
#include "GatherersPlayerController.generated.h"

class UGatherersTimeControlWidget;

UCLASS()
class UNREAL_GATHERERS_API AGatherersPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	virtual void BeginPlay() override;
	virtual void SetupInputComponent() override;

private:
	UPROPERTY(Transient)
	TObjectPtr<UGatherersTimeControlWidget> TimeControlWidget = nullptr;
};
