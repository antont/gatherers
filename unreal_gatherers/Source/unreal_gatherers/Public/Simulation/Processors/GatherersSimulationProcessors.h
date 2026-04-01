#pragma once

#include "CoreMinimal.h"
#include "MassEntityQuery.h"
#include "MassProcessor.h"
#include "GatherersSimulationProcessors.generated.h"

UCLASS()
class UNREAL_GATHERERS_API UGatherersTimeAccumulationProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	UGatherersTimeAccumulationProcessor();

protected:
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;
};

UCLASS()
class UNREAL_GATHERERS_API UGatherersAntMovementProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	UGatherersAntMovementProcessor();

protected:
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

private:
	FMassEntityQuery EntityQuery;
};

UCLASS()
class UNREAL_GATHERERS_API UGatherersFoodInteractionProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	UGatherersFoodInteractionProcessor();

protected:
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

private:
	FMassEntityQuery EntityQuery;
};

UCLASS()
class UNREAL_GATHERERS_API UGatherersVisualSyncProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	UGatherersVisualSyncProcessor();

protected:
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;
};
