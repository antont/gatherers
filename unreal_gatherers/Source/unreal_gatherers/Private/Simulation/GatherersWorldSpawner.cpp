#include "Simulation/GatherersWorldSpawner.h"

#include "Actors/Ant.h"
#include "Actors/Food.h"
#include "Engine/World.h"
#include "Simulation/GatherersMassSubsystem.h"
#include "Simulation/GatherersSpawnPlan.h"

FGatherersSpawnResult SpawnGatherersActors(UWorld& World, const FGatherersSpawnPlan& Plan)
{
	FGatherersSpawnResult Result;

	if (Plan.bSpawnActorVisuals)
	{
		for (const FTransform& SpawnTransform : Plan.AntSpawns)
		{
			if (AAnt* Ant = World.SpawnActor<AAnt>(AAnt::StaticClass(), SpawnTransform))
			{
				Result.Ants.Add(Ant);
			}
		}

		for (const FTransform& SpawnTransform : Plan.FoodSpawns)
		{
			if (AFood* Food = World.SpawnActor<AFood>(AFood::StaticClass(), SpawnTransform))
			{
				Result.Foods.Add(Food);
			}
		}
	}

	if (Plan.bUseFullSimulationMode)
	{
		if (UGatherersMassSubsystem* MassSubsystem = World.GetSubsystem<UGatherersMassSubsystem>())
		{
			MassSubsystem->InitializeHybridSimulation(Result, Plan);
		}
	}

	return Result;
}
