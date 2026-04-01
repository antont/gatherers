#pragma once

#include "CoreMinimal.h"

struct FGatherersMassAntFragment;

inline constexpr float GatherersMassPickupRadius = 15.0f;
inline constexpr float GatherersMassCarriedFoodHeight = 20.0f;
inline constexpr float GatherersMassPickupSeparationDistance = 50.0f;

FVector ConsumeAntTurnDirection(FGatherersMassAntFragment& AntFragment);
