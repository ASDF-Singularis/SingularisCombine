#pragma once

#include <CoreMinimal.h>
#include <HAL/CriticalSection.h>
#include <Templates/SubclassOf.h>
#include <UObject/ObjectMacros.h>

#include "Types/SingularisCombineDependencyList.h"
#include "Types/SingularisCombineDependencyScope.h"

class UClass;
class UActorComponent;

/**
 * 引力奇点化合依赖注册表（C++ 静态声明源）
 * 进程级函数局部静态单例。仅承载 C++ 侧经 SINGULARIS_DECLARE_DEPENDENCY 静态注册器登记的声明；
 * 蓝图侧声明随策略类 CDO（USingularisCombine::DeclaredDependencies）烘焙，不经过本表。
 * 评估期由 USingularisCombineComponent::GetDeclaredComponentClasses 将本表与 CDO 声明合并只读查询。
 * UClass 指针在引擎生命周期内稳定，作为 Map 键安全。
 */
class FSingularisCombineDependencyRegistry
{
public:
	/** 进程级单例访问（由 SingularisCombine 运行时模块导出，跨模块共享同一实例） */
	SINGULARISCOMBINE_API static FSingularisCombineDependencyRegistry& Get();

	/**
	 * 追加单条声明依赖（原生宏路径使用，幂等去重）
	 * @param StrategyClass   策略类
	 * @param Scope           依赖作用域
	 * @param ComponentClass  依赖组件类型
	 */
	void RegisterDependency(
		UClass* StrategyClass,
		ESingularisCombineDependencyScope Scope,
		TSubclassOf<UActorComponent> ComponentClass
	)
	{
		if (!StrategyClass || !ComponentClass)
			return;

		FScopeLock Lock(&CriticalSection);
		TMap<ESingularisCombineDependencyScope, FSingularisCombineDependencyList>& ScopeMap = Registry.FindOrAdd(
			StrategyClass
		);
		ScopeMap.FindOrAdd(Scope).Classes.AddUnique(ComponentClass);
	}

	/**
	 * 查询指定策略类登记的直接声明（只读，沿继承链聚合由调用方处理）
	 * @param StrategyClass  策略类
	 * @return 该类直接登记的声明依赖映射，未登记返回 nullptr
	 */
	const TMap<ESingularisCombineDependencyScope, FSingularisCombineDependencyList>* FindDeclaredClasses(
		const UClass* StrategyClass
	) const
	{
		if (!StrategyClass)
			return nullptr;

		FScopeLock Lock(&CriticalSection);
		return Registry.Find(StrategyClass);
	}

private:
	FSingularisCombineDependencyRegistry() = default;

	mutable FCriticalSection CriticalSection;
	TMap<UClass*, TMap<ESingularisCombineDependencyScope, FSingularisCombineDependencyList>> Registry;
};
