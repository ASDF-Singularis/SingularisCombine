#pragma once

#include <CoreMinimal.h>
#include <GameplayTagContainer.h>
#include <StructUtils/InstancedStruct.h>

#include "SingularisCombineTransientPayload.generated.h"

/**
 * 引力奇点化合瞬态负荷
 * 承载事件驱动评估的瞬态载荷：事件标识 tag 与结构化事件数据。
 * 周期轮询触发时为空，TriggerEvaluate 触发时携带事件载荷。
 * 参考设计：Context 与 Payload 分离，Payload 仅在本次评估有效。
 */
USTRUCT(BlueprintType)
struct SINGULARISCOMBINE_API FSingularisCombineTransientPayload
{
	GENERATED_BODY()

	/** 事件标识 tag，可空（语义为"无具体事件，仅标签变更"） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FGameplayTag EventTag{};

	/** 结构化事件数据，强类型任意 struct，可空 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FInstancedStruct EventData{};
};
