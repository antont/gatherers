#include "UI/GatherersTimeControlWidget.h"

#include "Engine/GameViewportClient.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SWeakWidget.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
FString BuildModeLabel(EGatherersTimeControlMode Mode)
{
	switch (Mode)
	{
	case EGatherersTimeControlMode::Fastest:
		return TEXT("Fastest (100x)");

	case EGatherersTimeControlMode::VeryFast:
		return TEXT("Very Fast (27x)");

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
	return CurrentLabel;
}

void UGatherersTimeControlWidget::TriggerToggleFromUI()
{
	if (Aunreal_gatherersGameModeBase* GatherersGameMode = GameMode.Get())
	{
		GatherersGameMode->ToggleTimeControlMode();
	}

	RefreshLabel();
}

void UGatherersTimeControlWidget::AddSlateToViewport()
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	UGameViewportClient* ViewportClient = World->GetGameViewport();
	if (ViewportClient == nullptr)
	{
		return;
	}

	SlateRoot = SNew(SBox)
		.Padding(FMargin(16.0f, 16.0f, 0.0f, 0.0f))
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Top)
		[
			SNew(SButton)
			.OnClicked_Lambda([this]()
			{
				TriggerToggleFromUI();
				return FReply::Handled();
			})
			[
				SAssignNew(SlateLabel, STextBlock)
				.Text(FText::FromString(CurrentLabel.IsEmpty() ? TEXT("Normal") : CurrentLabel))
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 16))
				.ColorAndOpacity(FSlateColor(FLinearColor::Black))
			]
		];

	ViewportClient->AddViewportWidgetContent(
		SNew(SWeakWidget).PossiblyNullContent(SlateRoot),
		10);

}

void UGatherersTimeControlWidget::RemoveSlateFromViewport()
{
	if (!SlateRoot.IsValid())
	{
		return;
	}

	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	UGameViewportClient* ViewportClient = World->GetGameViewport();
	if (ViewportClient != nullptr)
	{
		ViewportClient->RemoveViewportWidgetContent(SlateRoot.ToSharedRef());
	}

	SlateRoot.Reset();
	SlateLabel.Reset();
}

void UGatherersTimeControlWidget::NativeConstruct()
{
	Super::NativeConstruct();
	RefreshLabel();
}

void UGatherersTimeControlWidget::BeginDestroy()
{
	RemoveSlateFromViewport();
	Super::BeginDestroy();
}

void UGatherersTimeControlWidget::RefreshLabel()
{
	const EGatherersTimeControlMode Mode = GameMode.IsValid()
		? GameMode->GetTimeControlMode()
		: EGatherersTimeControlMode::Normal;
	CurrentLabel = BuildModeLabel(Mode);

	if (SlateLabel.IsValid())
	{
		SlateLabel->SetText(FText::FromString(CurrentLabel));
	}
}
