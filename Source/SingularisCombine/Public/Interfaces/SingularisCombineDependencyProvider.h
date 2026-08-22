#pragma once

#include <CoreMinimal.h>
#include <UObject/Interface.h>

#include "Types/SingularisCombineDependencyScope.h"
#include "SingularisCombineDependencyProvider.generated.h"

class UActorComponent;

/**
 * 引力奇点化合依赖查询提供者（UInterface 生成类）
 */
UINTERFACE(MinimalAPI)
class USingularisCombineDependencyProvider : public UInterface
{
	GENERATED_BODY()
};

/**
 * 引力奇点化合依赖查询提供者
 * 由化合组件实现并注入到策略实例；策略通过该接口查询依赖组件，
 * 避免策略直接引用具体组件类（依赖倒置，解除双向耦合）。
 */
class SINGULARISCOMBINE_API ISingularisCombineDependencyProvider
{
	GENERATED_BODY()

public:
	/**
	 * 查询指定作用域下已预缓存的依赖组件
	 * @param Scope           依赖作用域（Instigator / Avatar / Target）
	 * @param ComponentClass  组件类型
	 * @return 预缓存的组件引用，未找到时返回 nullptr
	 */
	virtual UActorComponent* GetDependency(
		ESingularisCombineDependencyScope Scope,
		TSubclassOf<UActorComponent> ComponentClass
	) const = 0;

	/**
	 * 沿 Avatar（组件 Owner）即时查询目标组件，用于未声明依赖的动态访问
	 * @param ComponentClass  组件类型
	 * @return 查找到的组件引用，未找到时返回 nullptr
	 */
	virtual UActorComponent* GetAvatarComponent(TSubclassOf<UActorComponent> ComponentClass) const = 0;
};
