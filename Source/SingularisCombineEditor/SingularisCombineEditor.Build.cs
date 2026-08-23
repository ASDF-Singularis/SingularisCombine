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

				"UMG",
				"UMGEditor",
				"UnrealEd",
				"AssetTools",
				"ContentBrowser",

				"BlueprintGraph",
				"KismetCompiler",
				"Kismet"
			]
		);
	}
}