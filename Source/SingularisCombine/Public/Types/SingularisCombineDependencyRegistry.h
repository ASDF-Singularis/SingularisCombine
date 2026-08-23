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
 * 进程级函数局部静态单例，按策略类登记其原生声明依赖。
 * 模块静态初始化期由声明宏的静态注册器写入；评估期由 GetDeclaredComponentClasses() 只读查询。
 * UClass 指针在引擎生命周期内稳定，作为 Map 键安全。
 */
class FSingularisCombineDependencyRegistry
{
public:
    static FSingularisCombineDependencyRegistry& Get()
    {
        static FSingularisCombineDependencyRegistry Instance;
        return Instance;
    }

    void RegisterDependency(
        UClass* StrategyClass,
        ESingularisCombineDependencyScope Scope,
        TSubclassOf<UActorComponent> ComponentClass)
    {
        if (!StrategyClass || !ComponentClass)
            return;

        FScopeLock Lock(&CriticalSection);
        TMap<ESingularisCombineDependencyScope, FSingularisCombineDependencyList>& ScopeMap = Registry.FindOrAdd(
            StrategyClass);
        ScopeMap.FindOrAdd(Scope).Classes.AddUnique(ComponentClass);
    }

    const TMap<ESingularisCombineDependencyScope, FSingularisCombineDependencyList>* FindDeclaredClasses(
        UClass* StrategyClass) const
    {
        if (!StrategyClass)
            return nullptr;

        FScopeLock Lock(&CriticalSection);
        return Registry.Find(StrategyClass);
    }

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
