using UnrealBuildTool;

public class unreal_gatherersTests : ModuleRules
{
	public unreal_gatherersTests(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"MassEntity",
			"MassCommon",
			"MassActors",
			"MassSimulation",
			"MassRepresentation",
			"UnrealEd",
			"unreal_gatherers",
		});
	}
}
