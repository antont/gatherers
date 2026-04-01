#include "Input/GatherersPlayerController.h"

#include "UI/GatherersTimeControlWidget.h"
#include "unreal_gatherers/unreal_gatherersGameModeBase.h"

void AGatherersPlayerController::BeginPlay()
{
	Super::BeginPlay();

	if (TimeControlWidget == nullptr && IsLocalController())
	{
		static TSubclassOf<UGatherersTimeControlWidget> WidgetClass;
		if (!WidgetClass)
		{
			WidgetClass = LoadClass<UGatherersTimeControlWidget>(nullptr,
				TEXT("/Game/SimBlank/Blueprints/WBP_TimeControl.WBP_TimeControl_C"));
		}
		if (!WidgetClass)
		{
			WidgetClass = UGatherersTimeControlWidget::StaticClass();
		}
		TimeControlWidget = CreateWidget<UGatherersTimeControlWidget>(this, WidgetClass);
		if (TimeControlWidget != nullptr)
		{
			if (Aunreal_gatherersGameModeBase* GatherersGameMode = GetWorld() ? GetWorld()->GetAuthGameMode<Aunreal_gatherersGameModeBase>() : nullptr)
			{
				TimeControlWidget->InitializeForGameMode(*GatherersGameMode);
				GatherersGameMode->SetTimeControlWidget(TimeControlWidget);
			}

			TimeControlWidget->AddSlateToViewport();
		}
	}
}
