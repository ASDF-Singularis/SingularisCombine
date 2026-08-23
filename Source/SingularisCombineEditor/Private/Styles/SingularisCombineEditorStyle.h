#pragma once

#include <CoreMinimal.h>

/**
 * SingularisCombineEditor 模块的 Slate 样式集
 * 注册 K2 节点等编辑器图标（复用插件 Resources/Icon128.png），
 * 由模块 StartupModule / ShutdownModule 成对调用 Register / Unregister 维护生命周期。
 */
class FSingularisCombineEditorStyle
{
public:
	/** 样式集名称，构造 FSlateIcon 时传入 */
	static FName GetStyleSetName();

	/** 声明依赖节点图标槽位名 */
	static FName GetDeclareDependencyIconName();

	/** 注册样式集（模块启动时调用） */
	static void Register();

	/** 注销样式集（模块关闭时调用） */
	static void Unregister();
};
