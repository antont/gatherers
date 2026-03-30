#include "Actors/Ant.h"

#include "Components/SceneComponent.h"

AAnt::AAnt()
{
	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
}
