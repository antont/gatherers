#include "Simulation/GatherersMassSubsystem.h"

#include "Actors/Ant.h"
#include "Actors/Food.h"
#include "MassEntityManager.h"
#include "MassEntitySubsystem.h"
#include "MassEntityView.h"
#include "StructUtils/InstancedStruct.h"
#include "Simulation/GatherersAntSimulation.h"
#include "Simulation/GatherersSpawnPlan.h"
#include "Simulation/GatherersWorldSpawner.h"

namespace
{
constexpr float MassPickupRadius = 15.0f;
constexpr float MassCarriedFoodHeight = 20.0f;
constexpr float MassPickupSeparationDistance = 50.0f;

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

		FMassEntityHandle NearbyFoodEntity;
		FGatherersMassFoodFragment* NearbyFood = FindLooseFoodInPickupRadius(
			EntityManager,
			ManagedFoodEntities,
			AntFragment.Position,
			NearbyFoodEntity);

		if (AntFragment.CarriedFoodEntity.IsValid() && NearbyFood != nullptr)
		{
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
				AntFragment.CarriedFoodEntity = NearbyFoodEntity;
				NearbyFood->bIsLoose = false;
			}
		}

		if (AAnt* AntProxy = AntFragment.ProxyActor.Get())
		{
			AntProxy->SetActorLocation(AntFragment.Position);

			if (AntFragment.CarriedFoodEntity.IsValid() && EntityManager.IsEntityValid(AntFragment.CarriedFoodEntity))
			{
				FMassEntityView FoodView(EntityManager, AntFragment.CarriedFoodEntity);
				FGatherersMassFoodFragment& FoodFragment = FoodView.GetFragmentData<FGatherersMassFoodFragment>();
				if (AFood* FoodProxy = FoodFragment.ProxyActor.Get())
				{
					FoodProxy->AttachToComponent(AntProxy->GetRootComponent(), FAttachmentTransformRules::KeepWorldTransform);
					FoodProxy->SetActorRelativeLocation(ComputeCarriedFoodRelativeLocation(MassCarriedFoodHeight));
				}
			}
		}
	}
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

	for (AFood* FoodProxy : SpawnResult.Foods)
	{
		if (FoodProxy == nullptr)
		{
			continue;
		}

		FGatherersMassFoodFragment FoodFragment;
		FoodFragment.Position = FoodProxy->GetActorLocation();
		FoodFragment.ProxyActor = FoodProxy;

		TArray<FInstancedStruct, TInlineAllocator<1>> FoodFragments;
		FoodFragments.Add(FInstancedStruct::Make(FoodFragment));
		ManagedFoodEntities.Add(EntityManager.CreateEntity(FoodFragments));
	}

	for (int32 AntIndex = 0; AntIndex < SpawnResult.Ants.Num(); ++AntIndex)
	{
		AAnt* AntProxy = SpawnResult.Ants[AntIndex];
		if (AntProxy == nullptr)
		{
			continue;
		}

		FGatherersMassAntFragment AntFragment;
		AntFragment.Position = AntProxy->GetActorLocation();
		AntFragment.Direction = Plan.AntInitialDirections.IsValidIndex(AntIndex)
			? Plan.AntInitialDirections[AntIndex].GetSafeNormal()
			: FVector(1.0f, 0.0f, 0.0f);
		if (AntFragment.Direction.IsNearlyZero())
		{
			AntFragment.Direction = FVector(1.0f, 0.0f, 0.0f);
		}

		AntFragment.PlayAreaBounds = Plan.PlayAreaBounds;
		AntFragment.ProxyActor = AntProxy;
		AntFragment.RandomSeed = Plan.RandomSeedBase + AntIndex;

		TArray<FInstancedStruct, TInlineAllocator<1>> AntFragments;
		AntFragments.Add(FInstancedStruct::Make(AntFragment));
		ManagedAntEntities.Add(EntityManager.CreateEntity(AntFragments));
		AntProxy->SetActorTickEnabled(false);
	}
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
