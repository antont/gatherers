#include "Actors/Food.h"

#include "Components/SceneComponent.h"

AFood::AFood()
{
	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
}
