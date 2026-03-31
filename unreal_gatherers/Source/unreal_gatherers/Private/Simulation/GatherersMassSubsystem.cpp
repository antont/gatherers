#include "Simulation/GatherersMassSubsystem.h"

#include "Actors/Ant.h"
#include "Actors/Food.h"
#include "MassEntityManager.h"
#include "MassEntitySubsystem.h"
#include "StructUtils/InstancedStruct.h"
#include "Simulation/GatherersSpawnPlan.h"
#include "Simulation/GatherersWorldSpawner.h"

void UGatherersMassSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
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
