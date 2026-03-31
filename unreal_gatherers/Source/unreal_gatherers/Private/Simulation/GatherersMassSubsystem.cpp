#include "Simulation/GatherersMassSubsystem.h"

#include "Actors/Ant.h"
#include "Actors/Food.h"
#include "Components/SceneComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "Math/RandomStream.h"
#include "MassEntityManager.h"
#include "MassEntitySubsystem.h"
#include "MassEntityView.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "StructUtils/InstancedStruct.h"
#include "Simulation/GatherersAntSimulation.h"
#include "Simulation/GatherersSpawnPlan.h"
#include "Simulation/GatherersWorldSpawner.h"

namespace
{
constexpr float MassPickupRadius = 15.0f;
constexpr float MassCarriedFoodHeight = 20.0f;
constexpr float MassPickupSeparationDistance = 50.0f;
const FLinearColor MassAntColor(0.8f, 0.8f, 0.8f, 1.0f);
const FLinearColor MassFoodColor(192.0f / 255.0f, 2.0f / 255.0f, 2.0f / 255.0f, 1.0f);
const FVector MassAntVisualScale(0.2f, 0.2f, 0.2f);
const FVector MassFoodVisualScale(0.1f, 0.1f, 0.1f);
constexpr TCHAR SphereMeshPath[] = TEXT("/Engine/BasicShapes/Sphere.Sphere");
constexpr TCHAR BasicShapeMaterialPath[] = TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial");
constexpr TCHAR VisualizerActorName[] = TEXT("GatherersMassVisualizer");
constexpr TCHAR VisualizerRootName[] = TEXT("Root");
constexpr TCHAR AntInstancesName[] = TEXT("AntInstances");
constexpr TCHAR FoodInstancesName[] = TEXT("FoodInstances");

FGatherersMassFoodFragment* FindLooseFoodInPickupRadius(
	FMassEntityManager& EntityManager,
	const TArray<FMassEntityHandle>& FoodEntities,
	const FVector& AntPosition,
	FMassEntityHandle& OutFoodEntity)
{
	for (const FMassEntityHandle FoodEntity : FoodEntities)
	{
		if (!EntityManager.IsEntityValid(FoodEntity))
		{
			continue;
		}

		FMassEntityView FoodView(EntityManager, FoodEntity);
		FGatherersMassFoodFragment& FoodFragment = FoodView.GetFragmentData<FGatherersMassFoodFragment>();
		if (!FoodFragment.bIsLoose)
		{
			continue;
		}

		if (ShouldAntPickUpFood(AntPosition, FoodFragment.Position, MassPickupRadius))
		{
			OutFoodEntity = FoodEntity;
			return &FoodFragment;
		}
	}

	OutFoodEntity.Reset();
	return nullptr;
}

FVector ConsumeAntTurnDirection(FGatherersMassAntFragment& AntFragment)
{
	FRandomStream RandomStream(AntFragment.RandomSeed);
	const FVector TurnDirection = ComputeAntTurnDirection(
		AntFragment.Direction,
		RandomStream.FRandRange(-1.0f, 1.0f),
		AntFragment.TurnJitterRadians);
	AntFragment.RandomSeed = RandomStream.GetCurrentSeed();
	return TurnDirection;
}

void ConfigureVisualComponent(
	UInstancedStaticMeshComponent& Component,
	USceneComponent& RootComponent,
	UStaticMesh& Mesh,
	UMaterialInterface& Material)
{
	if (!Component.IsRegistered())
	{
		Component.SetupAttachment(&RootComponent);
		Component.RegisterComponent();
	}

	Component.SetStaticMesh(&Mesh);
	Component.SetMaterial(0, &Material);
	Component.SetMobility(EComponentMobility::Movable);
	Component.SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Component.SetCastShadow(false);
	Component.SetCanEverAffectNavigation(false);
}

FTransform BuildVisualTransform(const FVector& Position, const FVector& VisualScale)
{
	return FTransform(FQuat::Identity, Position, VisualScale);
}

FVector ComputeFoodVisualPosition(
	FMassEntityManager& EntityManager,
	const TArray<FMassEntityHandle>& AntEntities,
	FMassEntityHandle FoodEntity,
	const FGatherersMassFoodFragment& FoodFragment)
{
	if (FoodFragment.bIsLoose)
	{
		return FoodFragment.Position;
	}

	for (const FMassEntityHandle AntEntity : AntEntities)
	{
		if (!EntityManager.IsEntityValid(AntEntity))
		{
			continue;
		}

		FMassEntityView AntView(EntityManager, AntEntity);
		const FGatherersMassAntFragment& AntFragment = AntView.GetFragmentData<FGatherersMassAntFragment>();
		if (AntFragment.CarriedFoodEntity == FoodEntity)
		{
			return AntFragment.Position + ComputeCarriedFoodRelativeLocation(MassCarriedFoodHeight);
		}
	}

	return FoodFragment.Position;
}
}

bool UGatherersMassSubsystem::EnsureVisualComponents()
{
	if (!VisualSphereMesh)
	{
		VisualSphereMesh = LoadObject<UStaticMesh>(nullptr, SphereMeshPath);
	}

	UMaterialInterface* BaseMaterial = LoadObject<UMaterialInterface>(nullptr, BasicShapeMaterialPath);
	if (VisualSphereMesh == nullptr || BaseMaterial == nullptr)
	{
		return false;
	}

	if (!AntVisualMaterial)
	{
		AntVisualMaterial = UMaterialInstanceDynamic::Create(BaseMaterial, this);
		if (AntVisualMaterial)
		{
			AntVisualMaterial->SetVectorParameterValue(TEXT("Color"), MassAntColor);
		}
	}

	if (!FoodVisualMaterial)
	{
		FoodVisualMaterial = UMaterialInstanceDynamic::Create(BaseMaterial, this);
		if (FoodVisualMaterial)
		{
			FoodVisualMaterial->SetVectorParameterValue(TEXT("Color"), MassFoodColor);
		}
	}

	if (AntVisualMaterial == nullptr || FoodVisualMaterial == nullptr)
	{
		return false;
	}

	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return false;
	}

	if (!VisualizerActor)
	{
		FActorSpawnParameters SpawnParameters;
		SpawnParameters.Name = VisualizerActorName;
		SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		VisualizerActor = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, SpawnParameters);
	}

	if (VisualizerActor == nullptr)
	{
		return false;
	}

	USceneComponent* RootComponent = VisualizerActor->GetRootComponent();
	if (RootComponent == nullptr)
	{
		RootComponent = NewObject<USceneComponent>(VisualizerActor, VisualizerRootName);
		VisualizerActor->AddInstanceComponent(RootComponent);
		RootComponent->RegisterComponent();
		VisualizerActor->SetRootComponent(RootComponent);
	}

	if (!AntVisualComponent)
	{
		AntVisualComponent = NewObject<UInstancedStaticMeshComponent>(VisualizerActor, AntInstancesName);
		VisualizerActor->AddInstanceComponent(AntVisualComponent);
	}

	if (!FoodVisualComponent)
	{
		FoodVisualComponent = NewObject<UInstancedStaticMeshComponent>(VisualizerActor, FoodInstancesName);
		VisualizerActor->AddInstanceComponent(FoodVisualComponent);
	}

	ConfigureVisualComponent(*AntVisualComponent, *RootComponent, *VisualSphereMesh, *AntVisualMaterial);
	ConfigureVisualComponent(*FoodVisualComponent, *RootComponent, *VisualSphereMesh, *FoodVisualMaterial);

	return true;
}

void UGatherersMassSubsystem::RebuildVisualInstances(UMassEntitySubsystem& MassEntitySubsystem)
{
	if (!EnsureVisualComponents())
	{
		return;
	}

	AntVisualComponent->ClearInstances();
	FoodVisualComponent->ClearInstances();

	FMassEntityManager& EntityManager = MassEntitySubsystem.GetMutableEntityManager();
	for (const FMassEntityHandle AntEntity : ManagedAntEntities)
	{
		if (!EntityManager.IsEntityValid(AntEntity))
		{
			continue;
		}

		FMassEntityView AntView(EntityManager, AntEntity);
		const FGatherersMassAntFragment& AntFragment = AntView.GetFragmentData<FGatherersMassAntFragment>();
		AntVisualComponent->AddInstance(BuildVisualTransform(AntFragment.Position, MassAntVisualScale), true);
	}

	for (const FMassEntityHandle FoodEntity : ManagedFoodEntities)
	{
		if (!EntityManager.IsEntityValid(FoodEntity))
		{
			continue;
		}

		FMassEntityView FoodView(EntityManager, FoodEntity);
		const FGatherersMassFoodFragment& FoodFragment = FoodView.GetFragmentData<FGatherersMassFoodFragment>();
		FoodVisualComponent->AddInstance(
			BuildVisualTransform(
				ComputeFoodVisualPosition(EntityManager, ManagedAntEntities, FoodEntity, FoodFragment),
				MassFoodVisualScale),
			true);
	}
}

void UGatherersMassSubsystem::SyncVisualInstances(UMassEntitySubsystem& MassEntitySubsystem)
{
	if (!EnsureVisualComponents())
	{
		return;
	}

	if (AntVisualComponent->GetInstanceCount() != ManagedAntEntities.Num()
		|| FoodVisualComponent->GetInstanceCount() != ManagedFoodEntities.Num())
	{
		RebuildVisualInstances(MassEntitySubsystem);
		return;
	}

	FMassEntityManager& EntityManager = MassEntitySubsystem.GetMutableEntityManager();
	for (int32 AntIndex = 0; AntIndex < ManagedAntEntities.Num(); ++AntIndex)
	{
		const FMassEntityHandle AntEntity = ManagedAntEntities[AntIndex];
		if (!EntityManager.IsEntityValid(AntEntity))
		{
			RebuildVisualInstances(MassEntitySubsystem);
			return;
		}

		FMassEntityView AntView(EntityManager, AntEntity);
		const FGatherersMassAntFragment& AntFragment = AntView.GetFragmentData<FGatherersMassAntFragment>();
		AntVisualComponent->UpdateInstanceTransform(
			AntIndex,
			BuildVisualTransform(AntFragment.Position, MassAntVisualScale),
			true,
			AntIndex == ManagedAntEntities.Num() - 1 && ManagedFoodEntities.Num() == 0,
			true);
	}

	for (int32 FoodIndex = 0; FoodIndex < ManagedFoodEntities.Num(); ++FoodIndex)
	{
		const FMassEntityHandle FoodEntity = ManagedFoodEntities[FoodIndex];
		if (!EntityManager.IsEntityValid(FoodEntity))
		{
			RebuildVisualInstances(MassEntitySubsystem);
			return;
		}

		FMassEntityView FoodView(EntityManager, FoodEntity);
		const FGatherersMassFoodFragment& FoodFragment = FoodView.GetFragmentData<FGatherersMassFoodFragment>();
		FoodVisualComponent->UpdateInstanceTransform(
			FoodIndex,
			BuildVisualTransform(
				ComputeFoodVisualPosition(EntityManager, ManagedAntEntities, FoodEntity, FoodFragment),
				MassFoodVisualScale),
			true,
			FoodIndex == ManagedFoodEntities.Num() - 1,
			true);
	}
}

void UGatherersMassSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (!HasManagedSimulation())
	{
		return;
	}

	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	UMassEntitySubsystem* MassEntitySubsystem = World->GetSubsystem<UMassEntitySubsystem>();
	if (MassEntitySubsystem == nullptr)
	{
		return;
	}

	FMassEntityManager& EntityManager = MassEntitySubsystem->GetMutableEntityManager();
	for (const FMassEntityHandle Entity : ManagedAntEntities)
	{
		if (!EntityManager.IsEntityValid(Entity))
		{
			continue;
		}

		FMassEntityView AntView(EntityManager, Entity);
		FGatherersMassAntFragment& AntFragment = AntView.GetFragmentData<FGatherersMassAntFragment>();
		AntFragment.PickupCooldownRemainingSeconds = ComputeRemainingPickupCooldown(
			AntFragment.PickupCooldownRemainingSeconds,
			DeltaTime);
		AntFragment.Position = ComputeAntHeadingMovementStep(
			AntFragment.Position,
			AntFragment.Direction,
			AntFragment.MovementSpeed,
			18.0f,
			DeltaTime);

		if (SimulationBounds.IsValid)
		{
			FVector InwardBoundaryNormal = FVector::ZeroVector;
			if (AntFragment.Position.X < SimulationBounds.Min.X)
			{
				AntFragment.Position.X = SimulationBounds.Min.X;
				InwardBoundaryNormal += FVector(1.0f, 0.0f, 0.0f);
			}
			else if (AntFragment.Position.X > SimulationBounds.Max.X)
			{
				AntFragment.Position.X = SimulationBounds.Max.X;
				InwardBoundaryNormal += FVector(-1.0f, 0.0f, 0.0f);
			}

			if (AntFragment.Position.Y < SimulationBounds.Min.Y)
			{
				AntFragment.Position.Y = SimulationBounds.Min.Y;
				InwardBoundaryNormal += FVector(0.0f, 1.0f, 0.0f);
			}
			else if (AntFragment.Position.Y > SimulationBounds.Max.Y)
			{
				AntFragment.Position.Y = SimulationBounds.Max.Y;
				InwardBoundaryNormal += FVector(0.0f, -1.0f, 0.0f);
			}

			if (!InwardBoundaryNormal.IsNearlyZero())
			{
				AntFragment.Direction = ComputeBoundaryTurnBackDirection(AntFragment.Direction, InwardBoundaryNormal);
			}
		}

		FMassEntityHandle NearbyFoodEntity;
		FGatherersMassFoodFragment* NearbyFood = FindLooseFoodInPickupRadius(
			EntityManager,
			ManagedFoodEntities,
			AntFragment.Position,
			NearbyFoodEntity);

		if (AntFragment.CarriedFoodEntity.IsValid() && NearbyFood != nullptr)
		{
			AntFragment.Direction = ConsumeAntTurnDirection(AntFragment);

			if (EntityManager.IsEntityValid(AntFragment.CarriedFoodEntity))
			{
				FMassEntityView CarriedFoodView(EntityManager, AntFragment.CarriedFoodEntity);
				FGatherersMassFoodFragment& CarriedFoodFragment = CarriedFoodView.GetFragmentData<FGatherersMassFoodFragment>();
				CarriedFoodFragment.bIsLoose = true;
				CarriedFoodFragment.Position = AntFragment.Position;

				if (AFood* CarriedFoodProxy = CarriedFoodFragment.ProxyActor.Get())
				{
					CarriedFoodProxy->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
					CarriedFoodProxy->SetActorLocation(AntFragment.Position);
				}
			}

			AntFragment.CarriedFoodEntity.Reset();
			AntFragment.PickupCooldownRemainingSeconds = ComputePickupCooldownForSeparationDistance(
				MassPickupSeparationDistance,
				AntFragment.MovementSpeed);
		}
		else if (!AntFragment.CarriedFoodEntity.IsValid() && AntFragment.PickupCooldownRemainingSeconds <= 0.0f)
		{
			if (NearbyFood != nullptr)
			{
				AntFragment.Direction = ConsumeAntTurnDirection(AntFragment);
				AntFragment.CarriedFoodEntity = NearbyFoodEntity;
				NearbyFood->bIsLoose = false;
			}
		}
	}

	SyncVisualInstances(*MassEntitySubsystem);
}

TStatId UGatherersMassSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UGatherersMassSubsystem, STATGROUP_Tickables);
}

void UGatherersMassSubsystem::InitializeHybridSimulation(const FGatherersSpawnResult& SpawnResult, const FGatherersSpawnPlan& Plan)
{
	ResetSimulation();

	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	UMassEntitySubsystem* MassEntitySubsystem = World->GetSubsystem<UMassEntitySubsystem>();
	if (MassEntitySubsystem == nullptr)
	{
		return;
	}

	FMassEntityManager& EntityManager = MassEntitySubsystem->GetMutableEntityManager();
	SimulationBounds = Plan.PlayAreaBounds;

	for (int32 FoodIndex = 0; FoodIndex < Plan.FoodSpawns.Num(); ++FoodIndex)
	{
		FGatherersMassFoodFragment FoodFragment;
		FoodFragment.Position = Plan.FoodSpawns[FoodIndex].GetLocation();
		FoodFragment.ProxyActor = SpawnResult.Foods.IsValidIndex(FoodIndex) ? SpawnResult.Foods[FoodIndex] : nullptr;

		TArray<FInstancedStruct, TInlineAllocator<1>> FoodFragments;
		FoodFragments.Add(FInstancedStruct::Make(FoodFragment));
		ManagedFoodEntities.Add(EntityManager.CreateEntity(FoodFragments));
	}

	for (int32 AntIndex = 0; AntIndex < Plan.AntSpawns.Num(); ++AntIndex)
	{
		FGatherersMassAntFragment AntFragment;
		AntFragment.Position = Plan.AntSpawns[AntIndex].GetLocation();
		AntFragment.Direction = Plan.AntInitialDirections.IsValidIndex(AntIndex)
			? Plan.AntInitialDirections[AntIndex].GetSafeNormal()
			: FVector(1.0f, 0.0f, 0.0f);
		if (AntFragment.Direction.IsNearlyZero())
		{
			AntFragment.Direction = FVector(1.0f, 0.0f, 0.0f);
		}

		AntFragment.ProxyActor = SpawnResult.Ants.IsValidIndex(AntIndex) ? SpawnResult.Ants[AntIndex] : nullptr;
		AntFragment.RandomSeed = Plan.RandomSeedBase + AntIndex;
		AntFragment.MovementSpeed = FMath::Max(0.0f, Plan.FullSimulationMovementSpeed);
		AntFragment.TurnJitterRadians = FMath::Max(0.0f, Plan.FullSimulationTurnJitterRadians);

		TArray<FInstancedStruct, TInlineAllocator<1>> AntFragments;
		AntFragments.Add(FInstancedStruct::Make(AntFragment));
		ManagedAntEntities.Add(EntityManager.CreateEntity(AntFragments));
	}

	RebuildVisualInstances(*MassEntitySubsystem);
}

void UGatherersMassSubsystem::ResetSimulation()
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		ManagedAntEntities.Reset();
		ManagedFoodEntities.Reset();
		return;
	}

	UMassEntitySubsystem* MassEntitySubsystem = World->GetSubsystem<UMassEntitySubsystem>();
	if (MassEntitySubsystem == nullptr)
	{
		ManagedAntEntities.Reset();
		ManagedFoodEntities.Reset();
		return;
	}

	FMassEntityManager& EntityManager = MassEntitySubsystem->GetMutableEntityManager();
	if (AntVisualComponent != nullptr)
	{
		AntVisualComponent->ClearInstances();
	}
	if (FoodVisualComponent != nullptr)
	{
		FoodVisualComponent->ClearInstances();
	}

	for (const FMassEntityHandle Entity : ManagedAntEntities)
	{
		if (EntityManager.IsEntityValid(Entity))
		{
			EntityManager.DestroyEntity(Entity);
		}
	}

	for (const FMassEntityHandle Entity : ManagedFoodEntities)
	{
		if (EntityManager.IsEntityValid(Entity))
		{
			EntityManager.DestroyEntity(Entity);
		}
	}

	ManagedAntEntities.Reset();
	ManagedFoodEntities.Reset();
	SimulationBounds = FBox(EForceInit::ForceInit);
}

int32 UGatherersMassSubsystem::GetManagedAntCount() const
{
	return ManagedAntEntities.Num();
}

int32 UGatherersMassSubsystem::GetManagedFoodCount() const
{
	return ManagedFoodEntities.Num();
}

bool UGatherersMassSubsystem::HasManagedSimulation() const
{
	return ManagedAntEntities.Num() > 0 || ManagedFoodEntities.Num() > 0;
}

const FBox& UGatherersMassSubsystem::GetSimulationBounds() const
{
	return SimulationBounds;
}

const UInstancedStaticMeshComponent* UGatherersMassSubsystem::GetAntVisualComponent() const
{
	return AntVisualComponent;
}

const UInstancedStaticMeshComponent* UGatherersMassSubsystem::GetFoodVisualComponent() const
{
	return FoodVisualComponent;
}
