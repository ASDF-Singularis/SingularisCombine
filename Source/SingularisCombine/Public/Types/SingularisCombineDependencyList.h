#pragma once

#include <CoreMinimal.h>

#include "SingularisCombineDependencyList.generated.h"

class UActorComponent;

/**
 * 引力奇点化合依赖组件列表
 * 包装单个作用域（Instigator/Avatar/Target）下声明的依赖组件类型数组。
 * 作为依赖注册表 TMap 的值类型，供原生宏与蓝图编译 hook 共用同一真相源。
 */
USTRUCT(BlueprintType)
struct SINGULARISCOMBINE_API FSingularisCombineDependencyList
{
	GENERATED_BODY()

	/** 该作用域下声明的依赖组件类型 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TArray<TSubclassOf<UActorComponent>> Classes{};
};
