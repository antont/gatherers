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
			"UnrealEd",
			"unreal_gatherers",
		});
	}
}
