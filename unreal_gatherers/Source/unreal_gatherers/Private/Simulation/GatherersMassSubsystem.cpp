#include "Simulation/GatherersMassSubsystem.h"

#include "Actors/Ant.h"
#include "Actors/Food.h"
#include "Components/SceneComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "MassExecutor.h"
#include "MassEntityManager.h"
#include "MassProcessingContext.h"
#include "MassEntitySubsystem.h"
#include "MassEntityView.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "StructUtils/InstancedStruct.h"
#include "Simulation/GatherersAntSimulation.h"
#include "Simulation/GatherersMassRuntime.h"
#include "Simulation/Processors/GatherersSimulationProcessors.h"
#include "Simulation/GatherersSpawnPlan.h"
#include "Simulation/GatherersWorldSpawner.h"

namespace
{
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
constexpr ECollisionChannel FoodQueryChannel = ECC_GameTraceChannel1;

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
			return AntFragment.Position + ComputeCarriedFoodRelativeLocation(GatherersMassCarriedFoodHeight);
		}
	}

	return FoodFragment.Position;
}
}

void UGatherersMassSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	bProcessorPipelinesInitialized = false;
}

void UGatherersMassSubsystem::Deinitialize()
{
	SimulationProcessorPipeline.Reset();
	VisualProcessorPipeline.Reset();
	bProcessorPipelinesInitialized = false;
	Super::Deinitialize();
}

bool UGatherersMassSubsystem::EnsureProcessorPipelines(UMassEntitySubsystem& MassEntitySubsystem)
{
	if (bProcessorPipelinesInitialized)
	{
		return true;
	}

	FMassEntityManager& EntityManager = MassEntitySubsystem.GetMutableEntityManager();
	const TArray<TSubclassOf<UMassProcessor>> SimulationProcessors = {
		UGatherersTimeAccumulationProcessor::StaticClass(),
		UGatherersAntMovementProcessor::StaticClass(),
		UGatherersFoodInteractionProcessor::StaticClass(),
	};
	SimulationProcessorPipeline.InitializeFromClassArray(SimulationProcessors, *this, EntityManager.AsShared());

	const TArray<TSubclassOf<UMassProcessor>> VisualProcessors = {
		UGatherersVisualSyncProcessor::StaticClass(),
	};
	VisualProcessorPipeline.InitializeFromClassArray(VisualProcessors, *this, EntityManager.AsShared());

	bProcessorPipelinesInitialized = true;
	return true;
}

void UGatherersMassSubsystem::RunProcessorPipelines(float DeltaTime, bool bIncludeVisualSync)
{
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
	if (MassEntitySubsystem == nullptr || !EnsureProcessorPipelines(*MassEntitySubsystem))
	{
		return;
	}

	FMassEntityManager& EntityManager = MassEntitySubsystem->GetMutableEntityManager();
	FMassProcessingContext SimulationContext(EntityManager, DeltaTime);
	UE::Mass::Executor::Run(SimulationProcessorPipeline, SimulationContext);

	if (bIncludeVisualSync)
	{
		FMassProcessingContext VisualContext(EntityManager, DeltaTime);
		UE::Mass::Executor::Run(VisualProcessorPipeline, VisualContext);
	}
}

void UGatherersMassSubsystem::RunSimulationProcessorsForTesting(float DeltaTime)
{
	RunProcessorPipelines(DeltaTime, false);
}

void UGatherersMassSubsystem::AdvanceAccumulatedSimulationSeconds(float DeltaTime)
{
	AccumulatedSimulationSeconds += FMath::Max(0.0f, DeltaTime);
}

void UGatherersMassSubsystem::SyncManagedVisuals()
{
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

	SyncVisualInstances(*MassEntitySubsystem);
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
		VisualizerActor = FindObject<AActor>(World->PersistentLevel, VisualizerActorName);
		if (!VisualizerActor)
		{
			FActorSpawnParameters SpawnParameters;
			SpawnParameters.Name = VisualizerActorName;
			SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			VisualizerActor = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, SpawnParameters);
		}
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

	if (!FoodRepresentationComponent)
	{
		FoodRepresentationComponent = NewObject<UInstancedStaticMeshComponent>(VisualizerActor, FoodInstancesName);
		VisualizerActor->AddInstanceComponent(FoodRepresentationComponent);
	}

	ConfigureVisualComponent(*AntVisualComponent, *RootComponent, *VisualSphereMesh, *AntVisualMaterial);
	ConfigureVisualComponent(*FoodRepresentationComponent, *RootComponent, *VisualSphereMesh, *FoodVisualMaterial);
	AntVisualComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	FoodRepresentationComponent->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	FoodRepresentationComponent->SetCollisionResponseToAllChannels(ECR_Ignore);
	FoodRepresentationComponent->SetCollisionResponseToChannel(FoodQueryChannel, ECR_Block);

	return true;
}

TArray<FMassEntityHandle> UGatherersMassSubsystem::QueryLooseFoodEntitiesOverlappingSphere(const FVector& Center, float Radius) const
{
	TArray<FMassEntityHandle> OverlappingFoodEntities;
	if (!HasManagedSimulation() || FoodRepresentationComponent == nullptr)
	{
		return OverlappingFoodEntities;
	}

	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return OverlappingFoodEntities;
	}

	UMassEntitySubsystem* MassEntitySubsystem = World->GetSubsystem<UMassEntitySubsystem>();
	if (MassEntitySubsystem == nullptr)
	{
		return OverlappingFoodEntities;
	}

	FMassEntityManager& EntityManager = MassEntitySubsystem->GetMutableEntityManager();
	const TArray<int32> OverlappingInstanceIndices = FoodRepresentationComponent->GetInstancesOverlappingSphere(
		Center,
		FMath::Max(0.0f, Radius),
		true);

	struct FIndexedLooseFoodHit
	{
		FMassEntityHandle Entity;
		int32 InstanceIndex = INDEX_NONE;
		float DistanceSquared = TNumericLimits<float>::Max();
	};

	TArray<FIndexedLooseFoodHit> Hits;
	for (const int32 InstanceIndex : OverlappingInstanceIndices)
	{
		if (!ManagedFoodEntities.IsValidIndex(InstanceIndex))
		{
			continue;
		}

		const FMassEntityHandle FoodEntity = ManagedFoodEntities[InstanceIndex];
		if (!EntityManager.IsEntityValid(FoodEntity))
		{
			continue;
		}

		FMassEntityView FoodView(EntityManager, FoodEntity);
		const FGatherersMassFoodFragment& FoodFragment = FoodView.GetFragmentData<FGatherersMassFoodFragment>();
		if (!FoodFragment.bIsLoose)
		{
			continue;
		}

		Hits.Add({
			FoodEntity,
			InstanceIndex,
			FVector::DistSquared(Center, FoodFragment.Position),
		});
	}

	Hits.Sort([](const FIndexedLooseFoodHit& A, const FIndexedLooseFoodHit& B)
	{
		if (!FMath::IsNearlyEqual(A.DistanceSquared, B.DistanceSquared))
		{
			return A.DistanceSquared < B.DistanceSquared;
		}

		return A.InstanceIndex < B.InstanceIndex;
	});

	for (const FIndexedLooseFoodHit& Hit : Hits)
	{
		OverlappingFoodEntities.Add(Hit.Entity);
	}

	return OverlappingFoodEntities;
}

TArray<FMassEntityHandle> UGatherersMassSubsystem::QueryLooseFoodEntitiesAlongSweep(
	const FVector& SweepStart,
	const FVector& SweepEnd,
	float Radius) const
{
	const float QueryRadius = FMath::Max(0.0f, Radius);
	if (SweepStart.Equals(SweepEnd))
	{
		return QueryLooseFoodEntitiesOverlappingSphere(SweepStart, QueryRadius);
	}

	TArray<FMassEntityHandle> SweptFoodEntities;
	if (!HasManagedSimulation() || FoodRepresentationComponent == nullptr)
	{
		return SweptFoodEntities;
	}

	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return SweptFoodEntities;
	}

	UMassEntitySubsystem* MassEntitySubsystem = World->GetSubsystem<UMassEntitySubsystem>();
	if (MassEntitySubsystem == nullptr)
	{
		return SweptFoodEntities;
	}

	TArray<FHitResult> SweepHits;
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(GatherersMassFoodSweep), false);
	QueryParams.bTraceComplex = false;
	const bool bAnySweepHits = World->SweepMultiByChannel(
		SweepHits,
		SweepStart,
		SweepEnd,
		FQuat::Identity,
		FoodQueryChannel,
		FCollisionShape::MakeSphere(QueryRadius),
		QueryParams);

	TSet<int32> CandidateInstanceIndices;
	if (bAnySweepHits)
	{
		for (const FHitResult& Hit : SweepHits)
		{
			if (Hit.Component.Get() == FoodRepresentationComponent && ManagedFoodEntities.IsValidIndex(Hit.Item))
			{
				CandidateInstanceIndices.Add(Hit.Item);
			}
		}

		FBox SweptBounds(ForceInit);
		SweptBounds += SweepStart;
		SweptBounds += SweepEnd;
		SweptBounds = SweptBounds.ExpandBy(QueryRadius);
		for (const int32 InstanceIndex : FoodRepresentationComponent->GetInstancesOverlappingBox(SweptBounds, true))
		{
			if (ManagedFoodEntities.IsValidIndex(InstanceIndex))
			{
				CandidateInstanceIndices.Add(InstanceIndex);
			}
		}
	}

	struct FSweptLooseFoodHit
	{
		FMassEntityHandle Entity;
		int32 InstanceIndex = INDEX_NONE;
		bool bStartsOverlapped = false;
		float StartDistanceSquared = TNumericLimits<float>::Max();
		float DistanceAlongPathSquared = TNumericLimits<float>::Max();
	};

	FMassEntityManager& EntityManager = MassEntitySubsystem->GetMutableEntityManager();
	TArray<FSweptLooseFoodHit> Hits;
	for (const int32 InstanceIndex : CandidateInstanceIndices)
	{
		const FMassEntityHandle FoodEntity = ManagedFoodEntities[InstanceIndex];
		if (!EntityManager.IsEntityValid(FoodEntity))
		{
			continue;
		}

		FMassEntityView FoodView(EntityManager, FoodEntity);
		const FGatherersMassFoodFragment& FoodFragment = FoodView.GetFragmentData<FGatherersMassFoodFragment>();
		if (!FoodFragment.bIsLoose)
		{
			continue;
		}

		const float StartDistanceSquared = FVector::DistSquared(SweepStart, FoodFragment.Position);
		const FVector ClosestPoint = FMath::ClosestPointOnSegment(FoodFragment.Position, SweepStart, SweepEnd);
		if (FVector::DistSquared(ClosestPoint, FoodFragment.Position) > FMath::Square(QueryRadius))
		{
			continue;
		}

		Hits.Add({
			FoodEntity,
			InstanceIndex,
			StartDistanceSquared <= FMath::Square(QueryRadius),
			StartDistanceSquared,
			FVector::DistSquared(SweepStart, ClosestPoint),
		});
	}

	Hits.Sort([](const FSweptLooseFoodHit& A, const FSweptLooseFoodHit& B)
	{
		if (A.bStartsOverlapped != B.bStartsOverlapped)
		{
			return A.bStartsOverlapped;
		}

		if (A.bStartsOverlapped && B.bStartsOverlapped)
		{
			return A.InstanceIndex < B.InstanceIndex;
		}

		if (!FMath::IsNearlyEqual(A.DistanceAlongPathSquared, B.DistanceAlongPathSquared))
		{
			return A.DistanceAlongPathSquared < B.DistanceAlongPathSquared;
		}

		return A.InstanceIndex < B.InstanceIndex;
	});

	for (const FSweptLooseFoodHit& Hit : Hits)
	{
		SweptFoodEntities.Add(Hit.Entity);
	}

	return SweptFoodEntities;
}

void UGatherersMassSubsystem::RebuildVisualInstances(UMassEntitySubsystem& MassEntitySubsystem)
{
	if (!EnsureVisualComponents())
	{
		return;
	}

	AntVisualComponent->ClearInstances();
	FoodRepresentationComponent->ClearInstances();

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
		FoodRepresentationComponent->AddInstance(
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
		|| FoodRepresentationComponent->GetInstanceCount() != ManagedFoodEntities.Num())
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
		FoodRepresentationComponent->UpdateInstanceTransform(
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
	RunProcessorPipelines(DeltaTime, true);
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
		const FMassEntityHandle FoodEntity = EntityManager.CreateEntity(FoodFragments);
		EntityManager.AddTagToEntity(FoodEntity, FGatherersMassFoodTag::StaticStruct());
		ManagedFoodEntities.Add(FoodEntity);
	}

	for (int32 AntIndex = 0; AntIndex < Plan.AntSpawns.Num(); ++AntIndex)
	{
		FGatherersMassAntFragment AntFragment;
		AntFragment.Position = Plan.AntSpawns[AntIndex].GetLocation();
		AntFragment.PreviousPosition = AntFragment.Position;
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
		const FMassEntityHandle AntEntity = EntityManager.CreateEntity(AntFragments);
		EntityManager.AddTagToEntity(AntEntity, FGatherersMassAntTag::StaticStruct());
		ManagedAntEntities.Add(AntEntity);
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
	if (FoodRepresentationComponent != nullptr)
	{
		FoodRepresentationComponent->ClearInstances();
	}
	if (VisualizerActor != nullptr)
	{
		VisualizerActor->Destroy();
		VisualizerActor = nullptr;
		AntVisualComponent = nullptr;
		FoodRepresentationComponent = nullptr;
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
	AccumulatedSimulationSeconds = 0.0f;
	SimulationProcessorPipeline.Reset();
	VisualProcessorPipeline.Reset();
	bProcessorPipelinesInitialized = false;
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

float UGatherersMassSubsystem::GetAccumulatedSimulationSeconds() const
{
	return AccumulatedSimulationSeconds;
}

const FBox& UGatherersMassSubsystem::GetSimulationBounds() const
{
	return SimulationBounds;
}

const UInstancedStaticMeshComponent* UGatherersMassSubsystem::GetAntVisualComponent() const
{
	return AntVisualComponent;
}

const UInstancedStaticMeshComponent* UGatherersMassSubsystem::GetFoodRepresentationComponent() const
{
	return FoodRepresentationComponent;
}
