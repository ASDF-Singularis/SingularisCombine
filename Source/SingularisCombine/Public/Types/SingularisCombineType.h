#pragma once

#include <CoreMinimal.h>
#include <UObject/ObjectPtr.h>

#include "SingularisCombineType.generated.h"

class AActor;
class USingularisCombineComponent;

/**
 * 引力奇点化合上下文
 * 承载化合反应所需的运行时场景引用：发起者、替身、目标及化合组件实例。
 */
USTRUCT(BlueprintType)
struct SINGULARISCOMBINE_API FSingularisCombineContext
{
	GENERATED_BODY()

	/** 化合操作的发起者（通常是玩家控制器或初始 Actor） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TObjectPtr<AActor> Instigator = nullptr;

	/** 化合操作的替身（通常为挂载化合组件的 Actor） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TObjectPtr<AActor> Avatar = nullptr;

	/** 化合操作的目标 Actor */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TObjectPtr<AActor> Target = nullptr;

	/** 触发化合的化合组件实例 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TObjectPtr<USingularisCombineComponent> CombineComponent = nullptr;
};
