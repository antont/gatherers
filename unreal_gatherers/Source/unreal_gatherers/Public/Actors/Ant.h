#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Ant.generated.h"

class AFood;

UCLASS()
class UNREAL_GATHERERS_API AAnt : public AActor
{
	GENERATED_BODY()

public:
	AAnt();
	virtual void Tick(float DeltaSeconds) override;

private:
	AFood* FindClosestLooseFood() const;
	bool IsCarryingFood() const;
	void PickUpFood(AFood& Food);

	FVector MovementDirection = FVector(1.0f, 0.0f, 0.0f);
};
