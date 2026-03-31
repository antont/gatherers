#include "Simulation/GatherersSpawnPlan.h"

#include "Math/RandomStream.h"

namespace
{
constexpr int32 RustFoodCount = 80;
constexpr int32 RustAntSpawnStep = 50;
constexpr float RustAntSpawnY = 100.0f;
constexpr float SpawnZ = 50.0f;
}

FGatherersSpawnPlan BuildInitialGatherersSpawnPlan()
{
	FGatherersSpawnPlan Plan;
	Plan.bUseFullSimulationMode = true;
	Plan.PlayAreaBounds = FBox(FVector(-500.0f, -500.0f, -100.0f), FVector(500.0f, 500.0f, 100.0f));
	Plan.RandomSeedBase = 123;
	Plan.FullSimulationTurnJitterRadians = 0.0f;
	Plan.AntSpawns.Add(FTransform(FVector(0.0f, 0.0f, 50.0f)));
	Plan.AntInitialDirections.Add(FVector(1.0f, 0.0f, 0.0f));
	Plan.FoodSpawns.Add(FTransform(FVector(200.0f, 0.0f, 50.0f)));
	Plan.FoodSpawns.Add(FTransform(FVector(-200.0f, 0.0f, 50.0f)));
	return Plan;
}

FGatherersSpawnPlan BuildDefaultGameFullSimulationSpawnPlan(
	const FBox& PlayAreaBounds,
	int32 RandomSeed)
{
	FGatherersSpawnPlan Plan;
	Plan.bUseFullSimulationMode = true;
	Plan.PlayAreaBounds = PlayAreaBounds;
	Plan.RandomSeedBase = RandomSeed;

	FRandomStream RandomStream(RandomSeed);
	const FVector BoundsCenter = PlayAreaBounds.GetCenter();
	const FVector BoundsExtent = PlayAreaBounds.GetExtent();
	const int32 HalfX = static_cast<int32>(BoundsExtent.X);
	const int32 HalfY = static_cast<int32>(BoundsExtent.Y);

	for (int32 AntX = -HalfX; AntX < HalfX; AntX += RustAntSpawnStep)
	{
		Plan.AntSpawns.Add(FTransform(FVector(BoundsCenter.X + static_cast<float>(AntX), BoundsCenter.Y + RustAntSpawnY, SpawnZ)));

		const float HeadingAngle = RandomStream.FRandRange(0.0f, 2.0f * PI);
		Plan.AntInitialDirections.Add(FVector(FMath::Cos(HeadingAngle), FMath::Sin(HeadingAngle), 0.0f));
	}

	for (int32 FoodIndex = 0; FoodIndex < RustFoodCount; ++FoodIndex)
	{
		const int32 FoodX = RandomStream.RandRange(-HalfX, HalfX - 1);
		const int32 FoodY = RandomStream.RandRange(-HalfY, HalfY - 1);
		Plan.FoodSpawns.Add(FTransform(FVector(
			BoundsCenter.X + static_cast<float>(FoodX),
			BoundsCenter.Y + static_cast<float>(FoodY),
			SpawnZ)));
	}

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

FGatherersSpawnPlan BuildFullSimulationVisualSpawnPlan()
{
	FGatherersSpawnPlan Plan;
	Plan.bUseFullSimulationMode = true;
	Plan.PlayAreaBounds = FBox(FVector(-120.0f, -100.0f, -100.0f), FVector(120.0f, 100.0f, 100.0f));
	Plan.RandomSeedBase = 123;
	Plan.FullSimulationTurnJitterRadians = 0.0f;
	Plan.AntSpawns.Add(FTransform(FVector::ZeroVector));
	Plan.AntInitialDirections.Add(FVector(1.0f, 0.0f, 0.0f));
	Plan.FoodSpawns.Add(FTransform(FVector(8.0f, 0.0f, 0.0f)));
	Plan.FoodSpawns.Add(FTransform(FVector(-10.0f, 0.0f, 0.0f)));
	Plan.FoodSpawns.Add(FTransform(FVector(50.0f, 0.0f, 0.0f)));
	return Plan;
}
