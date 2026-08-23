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
 * 引力奇点化合依赖注册表
 * 进程级函数局部静态单例，作为策略声明依赖的唯一真相源（SSOT）。
 * 写入路径两条：模块静态初始化期由声明宏的静态注册器经 RegisterDependency 追加；
 * 蓝图编译完成后由编辑器 hook 经 ReplaceDeclaredClasses 整体替换。
 * 评估期由 USingularisCombineComponent::GetDeclaredComponentClasses 只读查询。
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
	 * 整体替换指定策略类的全部声明依赖（蓝图编译 hook 路径使用）
	 * 保证幂等：每次编译全量重建后赋值，避免反复编译累积重复声明。
	 * @param StrategyClass   策略类
	 * @param InDeclared      本次编译产物（按作用域分组）
	 */
	void ReplaceDeclaredClasses(
		UClass* StrategyClass,
		TMap<ESingularisCombineDependencyScope, FSingularisCombineDependencyList> InDeclared
	)
	{
		if (!StrategyClass)
			return;

		FScopeLock Lock(&CriticalSection);
		Registry.FindOrAdd(StrategyClass) = MoveTemp(InDeclared);
	}

	/**
	 * 查询指定策略类的声明依赖（只读，沿继承链聚合由调用方处理）
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

	/** 清空全部登记（模块卸载时使用） */
	void Clear()
	{
		FScopeLock Lock(&CriticalSection);
		Registry.Empty();
	}

private:
	FSingularisCombineDependencyRegistry() = default;

	mutable FCriticalSection CriticalSection;
	TMap<UClass*, TMap<ESingularisCombineDependencyScope, FSingularisCombineDependencyList>> Registry;
};
