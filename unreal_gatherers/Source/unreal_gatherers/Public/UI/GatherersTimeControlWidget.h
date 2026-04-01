#pragma once

#include "Blueprint/UserWidget.h"
#include "unreal_gatherers/unreal_gatherersGameModeBase.h"
#include "GatherersTimeControlWidget.generated.h"

class SButton;
class STextBlock;

UCLASS()
class UNREAL_GATHERERS_API UGatherersTimeControlWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void InitializeForGameMode(Aunreal_gatherersGameModeBase& InGameMode);
	FString GetCurrentModeLabel() const;

	UFUNCTION()
	void TriggerToggleFromUI();

	/** Add Slate overlay directly to game viewport — call instead of AddToViewport. */
	void AddSlateToViewport();
	void RemoveSlateFromViewport();

protected:
	virtual void NativeConstruct() override;
	virtual void BeginDestroy() override;

private:
	void RefreshLabel();

	TWeakObjectPtr<Aunreal_gatherersGameModeBase> GameMode = nullptr;
	TSharedPtr<STextBlock> SlateLabel;
	TSharedPtr<SWidget> SlateRoot;
	FString CurrentLabel;
};
