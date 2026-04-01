#include "Input/GatherersPlayerController.h"

#include "UI/GatherersTimeControlWidget.h"
#include "unreal_gatherers/unreal_gatherersGameModeBase.h"

void AGatherersPlayerController::BeginPlay()
{
	Super::BeginPlay();

	if (TimeControlWidget == nullptr && IsLocalController())
	{
		TimeControlWidget = CreateWidget<UGatherersTimeControlWidget>(this, UGatherersTimeControlWidget::StaticClass());
		if (TimeControlWidget != nullptr)
		{
			if (Aunreal_gatherersGameModeBase* GatherersGameMode = GetWorld() ? GetWorld()->GetAuthGameMode<Aunreal_gatherersGameModeBase>() : nullptr)
			{
				TimeControlWidget->InitializeForGameMode(*GatherersGameMode);
				GatherersGameMode->SetTimeControlWidget(TimeControlWidget);
			}

			TimeControlWidget->AddToViewport();
		}
	}
}
