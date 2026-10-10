#pragma once

#include <CoreMinimal.h>
#include <UObject/ObjectCompileContext.h>
#include <UObject/ObjectMacros.h>

class UBlueprint;
class UObject;
class FDelegateHandle;

/**
 * 蓝图编译期声明回填 hook
 * 订阅 FCoreUObjectDelegates::OnObjectPostCDOCompiled，在蓝图产物 CDO 编译完成后
 * 扫描全部图内的声明节点，按 (Scope, Class) 去重写入策略类 CDO 的 DeclaredDependencies。
 * 该字段为可烘焙 UPROPERTY：随蓝图资产序列化并参与烘焙，打包版本运行时无需编辑器即可读取。
 */
class FSingularisCombineBlueprintCompileHook
{
public:
	/**
	 * 注册 CDO 编译回调
	 * @param OutHandle 输出的委托句柄，供 Unregister 解绑使用
	 */
	static void Register(FDelegateHandle& OutHandle);

	/**
	 * 解绑 CDO 编译回调
	 * @param Handle 待解绑的委托句柄，解绑后复位
	 */
	static void Unregister(FDelegateHandle& Handle);

	/**
	 * CDO 编译完成回调：回填全局依赖注册表
	 * @param CDO      刚编译完成的类默认对象（骨架类编译被跳过）
	 * @param Context  编译上下文（含骨架编译标志）
	 */
	static void HandleCDOCompiled(UObject* CDO, const FObjectPostCDOCompiledContext& Context);
};
