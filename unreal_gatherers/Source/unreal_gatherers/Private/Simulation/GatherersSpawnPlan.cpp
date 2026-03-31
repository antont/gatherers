#include "Simulation/GatherersSpawnPlan.h"

FGatherersSpawnPlan BuildInitialGatherersSpawnPlan()
{
	FGatherersSpawnPlan Plan;
	Plan.AntSpawns.Add(FTransform(FVector(0.0f, 0.0f, 50.0f)));
	Plan.FoodSpawns.Add(FTransform(FVector(200.0f, 0.0f, 50.0f)));
	Plan.FoodSpawns.Add(FTransform(FVector(-200.0f, 0.0f, 50.0f)));
	return Plan;
}

FGatherersSpawnPlan BuildFullSimulationSpawnPlan(
	int32 AntCount,
	int32 FoodCount,
	int32 RandomSeed,
	const FBox& PlayAreaBounds)
{
	FGatherersSpawnPlan Plan;
	Plan.bUseFullSimulationMode = true;
	Plan.PlayAreaBounds = PlayAreaBounds;
	Plan.RandomSeedBase = RandomSeed;

	FRandomStream RandomStream(RandomSeed);
	const int32 SafeAntCount = FMath::Max(0, AntCount);
	const int32 SafeFoodCount = FMath::Max(0, FoodCount);
	const float AntZ = 50.0f;
	const float FoodZ = 50.0f;
	const float MinX = PlayAreaBounds.Min.X;
	const float MaxX = PlayAreaBounds.Max.X;
	const float MidY = (PlayAreaBounds.Min.Y + PlayAreaBounds.Max.Y) * 0.5f;

	for (int32 AntIndex = 0; AntIndex < SafeAntCount; ++AntIndex)
	{
		const float AntX = SafeAntCount <= 1
			? (MinX + MaxX) * 0.5f
			: FMath::Lerp(MinX, MaxX, static_cast<float>(AntIndex) / static_cast<float>(SafeAntCount - 1));
		Plan.AntSpawns.Add(FTransform(FVector(AntX, MidY, AntZ)));

		const float HeadingAngle = RandomStream.FRandRange(0.0f, 2.0f * PI);
		Plan.AntInitialDirections.Add(FVector(FMath::Cos(HeadingAngle), FMath::Sin(HeadingAngle), 0.0f));
	}

	for (int32 FoodIndex = 0; FoodIndex < SafeFoodCount; ++FoodIndex)
	{
		Plan.FoodSpawns.Add(FTransform(FVector(
			RandomStream.FRandRange(PlayAreaBounds.Min.X, PlayAreaBounds.Max.X),
			RandomStream.FRandRange(PlayAreaBounds.Min.Y, PlayAreaBounds.Max.Y),
			FoodZ)));
	}

	return Plan;
}
