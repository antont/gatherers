#pragma once

#include "Blueprint/UserWidget.h"
#include "unreal_gatherers/unreal_gatherersGameModeBase.h"
#include "GatherersTimeControlWidget.generated.h"

class UButton;
class UTextBlock;

UCLASS()
class UNREAL_GATHERERS_API UGatherersTimeControlWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void InitializeForGameMode(Aunreal_gatherersGameModeBase& InGameMode);
	FString GetCurrentModeLabel() const;

	UFUNCTION()
	void TriggerToggleFromUI();

protected:
	virtual void NativeConstruct() override;

private:
	void BuildWidgetTreeIfNeeded();
	void RefreshLabel();

	TWeakObjectPtr<Aunreal_gatherersGameModeBase> GameMode = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UButton> ToggleButton = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> ModeLabelText = nullptr;
};
