using UnrealBuildTool;

public class SingularisCombine : ModuleRules
{
	public SingularisCombine(ReadOnlyTargetRules target) : base(target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateDependencyModuleNames.AddRange(
			[
				"Core",
				"CoreUObject",
				"Engine",
				"NetCore",

				"GameplayTags"
			]
		);
	}
}