#pragma once

#include "Components/InstancedStaticMeshComponent.h"
#include "CoreMinimal.h"
#include "MassEntityElementTypes.h"
#include "MassEntityHandle.h"
#include "MassExternalSubsystemTraits.h"
#include "MassProcessingTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "GatherersMassSubsystem.generated.h"

class AAnt;
class AFood;
class AActor;
class UMassEntitySubsystem;
class UMaterialInstanceDynamic;
class UStaticMesh;
struct FGatherersSpawnPlan;
struct FGatherersSpawnResult;

USTRUCT()
struct UNREAL_GATHERERS_API FGatherersMassAntTag : public FMassTag
{
	GENERATED_BODY()
};

USTRUCT()
struct UNREAL_GATHERERS_API FGatherersMassFoodTag : public FMassTag
{
	GENERATED_BODY()
};

USTRUCT()
struct UNREAL_GATHERERS_API FGatherersMassAntFragment : public FMassFragment
{
	GENERATED_BODY()

	FVector Position = FVector::ZeroVector;
	FVector PreviousPosition = FVector::ZeroVector;
	FVector Direction = FVector(1.0f, 0.0f, 0.0f);
	FMassEntityHandle CarriedFoodEntity;
	TWeakObjectPtr<AAnt> ProxyActor = nullptr;
	float PickupCooldownRemainingSeconds = 0.0f;
	float MovementSpeed = 100.0f;
	float TurnJitterRadians = PI / 2.0f;
	int32 RandomSeed = 0;
};

USTRUCT()
struct UNREAL_GATHERERS_API FGatherersMassFoodFragment : public FMassFragment
{
	GENERATED_BODY()

	FVector Position = FVector::ZeroVector;
	TWeakObjectPtr<AFood> ProxyActor = nullptr;
	bool bIsLoose = true;
};

template<>
struct TMassFragmentTraits<FGatherersMassAntFragment> final
{
	enum
	{
		AuthorAcceptsItsNotTriviallyCopyable = true
	};
};

template<>
struct TMassFragmentTraits<FGatherersMassFoodFragment> final
{
	enum
	{
		AuthorAcceptsItsNotTriviallyCopyable = true
	};
};

UCLASS()
class UNREAL_GATHERERS_API UGatherersMassSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	void InitializeHybridSimulation(const FGatherersSpawnResult& SpawnResult, const FGatherersSpawnPlan& Plan);
	void ResetSimulation();
	void RunSimulationProcessorsForTesting(float DeltaTime);
	void AdvanceAccumulatedSimulationSeconds(float DeltaTime);
	void SyncManagedVisuals();
	TArray<FMassEntityHandle> QueryLooseFoodEntitiesOverlappingSphere(const FVector& Center, float Radius) const;
	TArray<FMassEntityHandle> QueryLooseFoodEntitiesAlongSweep(const FVector& SweepStart, const FVector& SweepEnd, float Radius) const;

	int32 GetManagedAntCount() const;
	int32 GetManagedFoodCount() const;
	bool HasManagedSimulation() const;
	float GetAccumulatedSimulationSeconds() const;
	const FBox& GetSimulationBounds() const;
	const UInstancedStaticMeshComponent* GetAntVisualComponent() const;
	const UInstancedStaticMeshComponent* GetFoodRepresentationComponent() const;

private:
	bool EnsureProcessorPipelines(UMassEntitySubsystem& MassEntitySubsystem);
	void RunProcessorPipelines(float DeltaTime, bool bIncludeVisualSync);
	bool EnsureVisualComponents();
	void RebuildVisualInstances(UMassEntitySubsystem& MassEntitySubsystem);
	void SyncVisualInstances(UMassEntitySubsystem& MassEntitySubsystem);

public:
	TArray<FMassEntityHandle> ManagedAntEntities;
	TArray<FMassEntityHandle> ManagedFoodEntities;
	FBox SimulationBounds = FBox(EForceInit::ForceInit);
	float AccumulatedSimulationSeconds = 0.0f;

private:
	FMassRuntimePipeline SimulationProcessorPipeline;
	FMassRuntimePipeline VisualProcessorPipeline;
	bool bProcessorPipelinesInitialized = false;

	UPROPERTY(Transient)
	TObjectPtr<AActor> VisualizerActor = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UInstancedStaticMeshComponent> AntVisualComponent = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UInstancedStaticMeshComponent> FoodRepresentationComponent = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> VisualSphereMesh = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> AntVisualMaterial = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> FoodVisualMaterial = nullptr;
};

template<>
struct TMassExternalSubsystemTraits<UGatherersMassSubsystem>
{
	enum
	{
		GameThreadOnly = true,
		ThreadSafeWrite = false,
	};
};
