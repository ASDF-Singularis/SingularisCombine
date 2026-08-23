#pragma once

#include <CoreMinimal.h>
#include <UObject/ObjectCompileContext.h>
#include <UObject/ObjectMacros.h>

class UBlueprint;
class UObject;
class FDelegateHandle;

/**
 * 蓝图编译期 CDO 回填 hook
 * 蓝图编译完成后扫描所有 DeclareDependency 节点，批量写入产物 CDO 的 DeclaredComponents。
 * 经 USingularisCombine 的 friend 授权访问私有 DeclaredComponents。
 */
class FSingularisCombineBlueprintCompileHook
{
public:
	static void Register(FDelegateHandle& OutHandle);
	static void Unregister(FDelegateHandle& Handle);
	static void HandleCDOCompiled(UObject* CDO, const FObjectPostCDOCompiledContext& Context);
};
