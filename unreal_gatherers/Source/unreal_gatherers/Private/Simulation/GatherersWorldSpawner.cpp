#include "Simulation/GatherersWorldSpawner.h"

#include "Actors/Ant.h"
#include "Actors/Food.h"
#include "Engine/World.h"
#include "Simulation/GatherersSpawnPlan.h"

FGatherersSpawnResult SpawnGatherersActors(UWorld& World, const FGatherersSpawnPlan& Plan)
{
	FGatherersSpawnResult Result;

	for (const FTransform& SpawnTransform : Plan.AntSpawns)
	{
		if (AAnt* Ant = World.SpawnActor<AAnt>(AAnt::StaticClass(), SpawnTransform))
		{
			if (Plan.bUseFullSimulationMode)
			{
				const int32 SpawnIndex = Result.Ants.Num();
				const FVector InitialDirection = Plan.AntInitialDirections.IsValidIndex(SpawnIndex)
					? Plan.AntInitialDirections[SpawnIndex]
					: FVector(1.0f, 0.0f, 0.0f);
				Ant->ConfigureForFullSimulation(InitialDirection, Plan.PlayAreaBounds, Plan.RandomSeedBase + SpawnIndex);
			}

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

	return Result;
}
