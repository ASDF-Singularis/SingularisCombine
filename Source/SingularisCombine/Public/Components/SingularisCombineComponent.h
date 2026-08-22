#pragma once

#include <CoreMinimal.h>
#include <GameplayTagContainer.h>
#include <TimerManager.h>
#include <Components/ActorComponent.h>

#include "Types/SingularisCombineComponentType.h"
#include "Types/SingularisCombineTransientPayload.h"
#include "SingularisCombineComponent.generated.h"

#pragma region 委托签名

/** 黑板更新的动态多播委托：同时携带 GameplayTags 与 Actor/Component 原生 FName 标签 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FOnCombineBlackboardUpdatedSignature,
	const FGameplayTagContainer&,
	BlackboardTags,
	const TArray<FName>&,
	NativeTags
);

#pragma endregion

/**
 * 引力奇点化合组件
 * 挂载于 Actor 上，负责收集同 Actor 下所有组件的 GameplayTags 及原生 FName 标签并维护全局黑板，
 * 驱动化合管线逐策略评估：满足条件时触发 Reaction，不满足时触发 ReactionRevert，所有策略独立判定互不中断。
 */
UCLASS(
	Blueprintable,
	BlueprintType,
	ClassGroup = ("Singularis"),
	meta = (BlueprintSpawnableComponent, DisplayName = "引力奇点化合组件")
)
class SINGULARISCOMBINE_API USingularisCombineComponent : public UActorComponent
{
	GENERATED_BODY()

public:
#pragma region Parameter

	/**
	 * 自动评估轮询间隔（秒）
	 * 组件添加由构造委托即时捕获；组件移除及标签变更通过周期轮询兜底。
	 * 设为 0.0 则每帧执行评估（不启用定时器）。
	 */
	UPROPERTY(
		EditDefaultsOnly,
		BlueprintReadOnly,
		Category = "SingularisCombine|引力奇点化合组件|参数",
		meta = (DisplayName = "自动评估间隔", ClampMin = "0.0", UIMin = "0.0")
	)
	float AutoEvaluateInterval = 0.1f;

	/** 化合管线：按数组顺序依次评估的策略集合 */
	UPROPERTY(
		EditDefaultsOnly,
		BlueprintReadOnly,
		Category = "SingularisCombine|引力奇点化合组件|参数",
		meta = (DisplayName = "化合管线")
	)
	FSingularisCombinePipeline CombinePipeline{};

#pragma endregion

#pragma region 事件分发器

	/**
	 * 当组件重新收集全局 Tags 并更新黑板后触发
	 * 输出引脚 BlackboardTags 携带 GameplayTags 集合，NativeTags 携带 FName 原生标签集合。
	 */
	UPROPERTY(
		BlueprintAssignable,
		Category = "SingularisCombine|引力奇点化合组件|事件分发器",
		meta = (DisplayName = "OnCombineBlackboardUpdatedEvent")
	)
	FOnCombineBlackboardUpdatedSignature OnCombineBlackboardUpdatedEvent{};

#pragma endregion

private:
#pragma region Internal Variable

	/** 全局黑板：缓存当前 Actor 上收集到的所有 GameplayTags */
	FGameplayTagContainer BlackboardTags{};

	/** 全局黑板：缓存当前 Actor 上收集到的所有原生 FName 标签 */
	TArray<FName> NativeBlackboardTags{};

	/** 前次 GameplayTags 快照，用于广播幂等性比较 */
	FGameplayTagContainer PreviousBlackboardTags{};

	/** 前次 FName 标签快照，用于广播幂等性比较 */
	TArray<FName> PreviousNativeBlackboardTags{};

	/** UObject 构造委托句柄：检测 OwnerActor 上新组件的添加 */
	FDelegateHandle ObjectConstructedHandle{};

	/** 周期性评估定时器句柄 */
	FTimerHandle PeriodicEvaluateHandle{};

	/** 即时评估合并定时器句柄：将同帧内多次构造合并为一次评估 */
	FTimerHandle PendingEvaluateHandle{};

	/** TriggerEvaluate 写入的待处理事件载荷，EvaluatePipeline 读取后由 TriggerEvaluate 清空 */
	UPROPERTY(Transient)
	FSingularisCombineTransientPayload PendingPayload{};

#pragma endregion

public:
#pragma region Constructors

	USingularisCombineComponent();

#pragma endregion

#pragma region ActorComponent Interface

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void TickComponent(
		float DeltaTime,
		ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction
	) override;

#pragma endregion

#pragma region State

	/** 获取当前全局黑板上的 GameplayTags 快照 */
	UFUNCTION(
		BlueprintPure,
		BlueprintCallable,
		Category = "SingularisCombine|引力奇点化合组件|State",
		meta = (DisplayName = "GetBlackboardTags")
	)
	FGameplayTagContainer GetBlackboardTags() const { return BlackboardTags; }

	/** 获取当前全局黑板上收集的原生 FName 标签快照 */
	UFUNCTION(
		BlueprintPure,
		BlueprintCallable,
		Category = "SingularisCombine|引力奇点化合组件|State",
		meta = (DisplayName = "GetNativeBlackboardTags")
	)
	const TArray<FName>& GetNativeBlackboardTags() const { return NativeBlackboardTags; }

#pragma endregion

#pragma region API

	/**
	 * 设置化合管线
	 * 回滚当前所有已激活策略的状态，替换为新管线并立即触发重估。
	 */
	UFUNCTION(
		BlueprintCallable,
		BlueprintAuthorityOnly,
		Category = "SingularisCombine|引力奇点化合组件|API",
		meta = (DisplayName = "SetPipeline")
	)
	void SetPipeline(const FSingularisCombinePipeline& InPipeline);

	/** 清空化合管线：移除所有策略并回滚已激活状态后触发重估。 */
	UFUNCTION(
		BlueprintCallable,
		BlueprintAuthorityOnly,
		Category = "SingularisCombine|引力奇点化合组件|API",
		meta = (DisplayName = "ClearPipeline")
	)
	void ClearPipeline();

	/** 向化合管线末尾追加一条策略条目并触发重估。 */
	UFUNCTION(
		BlueprintCallable,
		BlueprintAuthorityOnly,
		Category = "SingularisCombine|引力奇点化合组件|API",
		meta = (DisplayName = "AddCombineEntry")
	)
	void AddCombineEntry(const FSingularisCombineEntry& Entry);

	/**
	 * 触发化合管线的核心重估循环 (Evaluate)
	 * 重新收集 GameplayTags 与原生 FName 标签，并逐一核对化合策略。
	 */
	UFUNCTION(
		BlueprintCallable,
		Category = "SingularisCombine|引力奇点化合组件|API",
		meta = (DisplayName = "EvaluatePipeline")
	)
	void EvaluatePipeline();

	/**
	 * 事件驱动评估
	 * 立即触发一次 EvaluatePipeline，将 Payload 贯穿本次评估所有策略
	 * @param Payload  事件载荷（事件标识 + 结构化数据），周期轮询触发时为空
	 */
	UFUNCTION(
		BlueprintCallable,
		BlueprintAuthorityOnly,
		Category = "SingularisCombine|引力奇点化合组件|API",
		meta = (DisplayName = "TriggerEvaluate")
	)
	void TriggerEvaluate(const FSingularisCombineTransientPayload& Payload);

#pragma endregion

private:
#pragma region Internal Function

	/** 收集同 Actor 下所有 GameplayTags 及原生 FName 标签 */
	void CollectAllTags();

	/** 将化合管线中全部有效的 Instanced 化合子对象添加至网络复制列表。仅在服务器端执行。 */
	void RegisterCombineSubObjects();

	/** 将化合管线中全部已注册的 Instanced 化合子对象从网络复制列表中移除。仅在服务器端执行。 */
	void UnregisterCombineSubObjects();

	/**
	 * UObject 构造全局回调
	 * 当任意 UActorComponent 被构造且其 Outer 为本组件 OwnerActor 时，合并触发管线评估。
	 */
	void OnOwnerChildComponentConstructed(UObject* Object);

#pragma endregion
};
