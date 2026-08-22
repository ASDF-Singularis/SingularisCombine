#pragma once

#include <CoreMinimal.h>

#include "SingularisCombineComponentType.generated.h"

class USingularisCombine;

/**
 * 引力奇点化合条目
 * 单个化合策略的包装，包含名称、描述及策略实例引用。
 */
USTRUCT(BlueprintType)
struct SINGULARISCOMBINE_API FSingularisCombineEntry
{
	GENERATED_BODY()

	/** 化合策略的显示名称 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FText CombineName{};

	/** 化合策略的功能描述 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FText CombineDescription{};

	/** 化合策略实例 */
	UPROPERTY(Instanced, EditAnywhere, BlueprintReadWrite)
	TObjectPtr<USingularisCombine> Combine = nullptr;
};

/**
 * 引力奇点化合管线
 * 包装一组有序的化合条目，按数组顺序依次评估，所有策略独立判定互不中断。
 */
USTRUCT(BlueprintType)
struct SINGULARISCOMBINE_API FSingularisCombinePipeline
{
	GENERATED_BODY()

	/** 化合策略数组：顺序即为评估优先级 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (TitleProperty = "CombineName"))
	TArray<FSingularisCombineEntry> Combines{};
};
