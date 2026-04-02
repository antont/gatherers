#include "Input/GatherersPlayerController.h"

#include "GameFramework/InputSettings.h"
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

void AGatherersPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
	if (InputComponent == nullptr)
	{
		return;
	}

	auto BindSpeedKey = [this](FKey Key, EGatherersTimeControlMode Mode)
	{
		FInputKeyBinding Binding(FInputChord(Key), IE_Pressed);
		Binding.KeyDelegate.GetDelegateForManualSet().BindLambda([this, Mode]()
		{
			if (Aunreal_gatherersGameModeBase* GM = GetWorld() ? GetWorld()->GetAuthGameMode<Aunreal_gatherersGameModeBase>() : nullptr)
			{
				GM->ApplyTimeControlMode(Mode);
			}
		});
		InputComponent->KeyBindings.Add(MoveTemp(Binding));
	};

	BindSpeedKey(EKeys::One, EGatherersTimeControlMode::Normal);
	BindSpeedKey(EKeys::Two, EGatherersTimeControlMode::Fast);
	BindSpeedKey(EKeys::Three, EGatherersTimeControlMode::VeryFast);
	BindSpeedKey(EKeys::Zero, EGatherersTimeControlMode::Max);
}
