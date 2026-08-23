using UnrealBuildTool;

public class SingularisCombineEditor : ModuleRules
{
	public SingularisCombineEditor(ReadOnlyTargetRules target) : base(target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateDependencyModuleNames.AddRange(
			[
				"Core",
				"CoreUObject",
				"Engine",

				"SingularisCombine",

				"BlueprintGraph",
				"KismetCompiler",
				"Kismet",

				"UMG",
				"UMGEditor",
				"UnrealEd",
				"AssetTools",
				"ContentBrowser"
			]
		);
	}
}