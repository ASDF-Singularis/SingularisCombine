#include "Styles/SingularisCombineEditorStyle.h"

#include <Brushes/SlateImageBrush.h>
#include <Interfaces/IPluginManager.h>
#include <Misc/Paths.h>
#include <Styling/SlateStyle.h>
#include <Styling/SlateStyleRegistry.h>

namespace
{
	// 样式集实例：模块生命周期内持有，Register / Unregister 成对维护
	TSharedPtr<FSlateStyleSet> StyleSetInstance = nullptr;

	// 节点图标显示尺寸（蓝图图表与调色板通用）
	const FVector2D IconSize(32.0f, 32.0f);

	FString GetIconPath()
	{
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("SingularisCombine"));
		if (!Plugin.IsValid())
			return FString();
		return FPaths::Combine(Plugin->GetBaseDir(), TEXT("Resources"), TEXT("Declarative.png"));
	}
}

FName FSingularisCombineEditorStyle::GetStyleSetName()
{
	static const FName StyleSetName(TEXT("SingularisCombineEditor"));
	return StyleSetName;
}

FName FSingularisCombineEditorStyle::GetDeclareDependencyIconName()
{
	static const FName IconName(TEXT("SingularisCombine.DeclareDependency"));
	return IconName;
}

void FSingularisCombineEditorStyle::Register()
{
	if (StyleSetInstance.IsValid())
		return;

	StyleSetInstance = MakeShared<FSlateStyleSet>(GetStyleSetName());

	// 复用插件图标作为 K2 节点图标：绝对路径加载，避免相对路径解析歧义
	const FString IconPath = GetIconPath();
	if (!IconPath.IsEmpty())
	{
		StyleSetInstance->Set(
			GetDeclareDependencyIconName(),
			new FSlateImageBrush(IconPath, IconSize)
		);
	}

	FSlateStyleRegistry::RegisterSlateStyle(*StyleSetInstance);
}

void FSingularisCombineEditorStyle::Unregister()
{
	if (!StyleSetInstance.IsValid())
		return;

	FSlateStyleRegistry::UnRegisterSlateStyle(*StyleSetInstance);
	StyleSetInstance.Reset();
}
