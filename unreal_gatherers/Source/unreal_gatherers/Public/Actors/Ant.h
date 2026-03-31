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
	void ConfigureForFullSimulation(const FVector& InitialDirection, const FBox& PlayAreaBounds, int32 RandomSeed);

private:
	AFood* FindClosestLooseFood() const;
	AFood* FindLooseFoodInPickupRadius() const;
	bool IsCarryingFood() const;
	void PickUpFood(AFood& Food);
	void DropFood();

	bool bUseFullSimulationMode = false;
	FVector MovementDirection = FVector(1.0f, 0.0f, 0.0f);
	FBox FullSimulationBounds = FBox(EForceInit::ForceInit);
	TObjectPtr<AFood> CarriedFood = nullptr;
	float PickupCooldownRemainingSeconds = 0.0f;
	FRandomStream FullSimulationRandomStream;
};
