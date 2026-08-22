#pragma once

#include <CoreMinimal.h>

#include "SingularisCombineDependencyScope.generated.h"

/**
 * 引力奇点化合依赖作用域
 * 标识声明式依赖归属的 Context Actor，用于结构化配置策略的组件依赖。
 */
UENUM(BlueprintType)
enum class ESingularisCombineDependencyScope : uint8
{
	/** 化合操作的发起者（Context.Instigator） */
	Instigator UMETA(DisplayName = "Instigator"),

	/** 化合操作的替身（Context.Avatar，通常为挂载化合组件的 Actor） */
	Avatar UMETA(DisplayName = "Avatar"),

	/** 化合操作的目标（Context.Target） */
	Target UMETA(DisplayName = "Target"),
};
