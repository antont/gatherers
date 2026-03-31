#include "Simulation/GatherersSpawnPlan.h"

FGatherersSpawnPlan BuildInitialGatherersSpawnPlan()
{
	FGatherersSpawnPlan Plan;
	Plan.AntSpawns.Add(FTransform(FVector(0.0f, 0.0f, 50.0f)));
	Plan.FoodSpawns.Add(FTransform(FVector(200.0f, 0.0f, 50.0f)));
	return Plan;
}
