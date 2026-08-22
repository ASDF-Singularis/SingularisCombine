#pragma once

#include <CoreMinimal.h>

#include "SingularisCombineDependencyList.generated.h"

class UActorComponent;

/**
 * 引力奇点化合依赖组件列表
 * 包装单个作用域（Instigator/Avatar/Target）下声明的依赖组件类型数组。
 * 作为 ComponentDependencies TMap 的值类型，绕过 UHT 不支持 TMap<K, TArray<TSubclassOf<UObject>>> 作为反射 UPROPERTY 的限制。
 */
USTRUCT(BlueprintType)
struct SINGULARISCOMBINE_API FSingularisCombineDependencyList
{
	GENERATED_BODY()

	/** 该作用域下声明的依赖组件类型 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TArray<TSubclassOf<UActorComponent>> Classes{};
};
