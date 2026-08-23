using UnrealBuildTool;

public class SingularisCombineEditor : ModuleRules
{
	public SingularisCombineEditor(ReadOnlyTargetRules target) : base(target)
	{
		// K2 节点类不允许定义于纯 Editor 模块（烘焙后运行时蓝图无法解析节点类），
		// 宿主类型由 .uplugin 声明为 UncookedOnly，此处仅声明模块实现类型
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