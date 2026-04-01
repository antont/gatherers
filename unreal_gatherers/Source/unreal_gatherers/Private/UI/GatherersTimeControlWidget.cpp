#include "UI/GatherersTimeControlWidget.h"

#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/TextBlock.h"
#include "Blueprint/WidgetTree.h"

namespace
{
FString BuildModeLabel(EGatherersTimeControlMode Mode)
{
	switch (Mode)
	{
	case EGatherersTimeControlMode::MaxCorrect:
		return TEXT("Max Correct (27x)");

	case EGatherersTimeControlMode::Fast:
		return TEXT("Fast");

	case EGatherersTimeControlMode::Normal:
	default:
		return TEXT("Normal");
	}
}
}

void UGatherersTimeControlWidget::InitializeForGameMode(Aunreal_gatherersGameModeBase& InGameMode)
{
	GameMode = &InGameMode;
	RefreshLabel();
}

FString UGatherersTimeControlWidget::GetCurrentModeLabel() const
{
	return ModeLabelText ? ModeLabelText->GetText().ToString() : FString();
}

void UGatherersTimeControlWidget::TriggerToggleFromUI()
{
	if (Aunreal_gatherersGameModeBase* GatherersGameMode = GameMode.Get())
	{
		GatherersGameMode->ToggleTimeControlMode();
	}

	RefreshLabel();
}

void UGatherersTimeControlWidget::NativeConstruct()
{
	Super::NativeConstruct();

	BuildWidgetTreeIfNeeded();
	if (ToggleButton != nullptr && !ToggleButton->OnClicked.IsAlreadyBound(this, &UGatherersTimeControlWidget::TriggerToggleFromUI))
	{
		ToggleButton->OnClicked.AddDynamic(this, &UGatherersTimeControlWidget::TriggerToggleFromUI);
	}

	RefreshLabel();
}

void UGatherersTimeControlWidget::BuildWidgetTreeIfNeeded()
{
	if (WidgetTree == nullptr)
	{
		return;
	}

	if (ToggleButton != nullptr && ModeLabelText != nullptr)
	{
		return;
	}

	UCanvasPanel* RootPanel = Cast<UCanvasPanel>(WidgetTree->RootWidget);
	if (RootPanel == nullptr)
	{
		RootPanel = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("RootPanel"));
		WidgetTree->RootWidget = RootPanel;
	}

	if (ToggleButton == nullptr)
	{
		ToggleButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("TimeToggleButton"));
		UCanvasPanelSlot* ButtonSlot = RootPanel->AddChildToCanvas(ToggleButton);
		if (ButtonSlot != nullptr)
		{
			ButtonSlot->SetAutoSize(true);
			ButtonSlot->SetPosition(FVector2D(16.0f, 16.0f));
		}
	}

	if (ModeLabelText == nullptr)
	{
		ModeLabelText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("TimeModeLabel"));
		if (ToggleButton != nullptr)
		{
			ToggleButton->AddChild(ModeLabelText);
		}
	}
}

void UGatherersTimeControlWidget::RefreshLabel()
{
	if (ModeLabelText == nullptr)
	{
		return;
	}

	const EGatherersTimeControlMode Mode = GameMode.IsValid()
		? GameMode->GetTimeControlMode()
		: EGatherersTimeControlMode::Normal;
	ModeLabelText->SetText(FText::FromString(BuildModeLabel(Mode)));
}
