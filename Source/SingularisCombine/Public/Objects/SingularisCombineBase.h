#pragma once

#include <CoreMinimal.h>
#include <GameplayTagContainer.h>
#include <UObject/Object.h>

#include "Types/SingularisCombineType.h"
#include "SingularisCombineBase.generated.h"

/**
 * 引力奇点通用能力化合抽象基类
 * 定义化合策略的标准接口：条件判断 (CanReaction)、正向反应 (Reaction) 与回滚剥离 (ReactionRevert)。
 * 子类通过覆写 BlueprintNativeEvent 实现自定义化合逻辑。
 * 所有 SPI 方法同时接收 GameplayTags 与 Actor/Component 原生 FName 标签，两者独立、互不冲突。
 */
UCLASS(Abstract, Blueprintable, EditInlineNew, CollapseCategories)
class SINGULARISCOMBINE_API USingularisCombine : public UObject
{
	GENERATED_BODY()

public:
#pragma region Parameter

	/** 当前策略是否处于激活状态（已触发 Reaction 且未被 ReactionRevert） */
	UPROPERTY(
		EditDefaultsOnly,
		BlueprintReadOnly,
		Category = "SingularisCombine|引力奇点化合|参数",
		meta = (DisplayName = "激活")
	)
	bool bIsActive = false;

#pragma endregion

#pragma region UObject Interface

	virtual UWorld* GetWorld() const override;

#pragma endregion

#pragma region API

	/** 查询当前化合策略是否处于激活状态 */
	bool IsActive() const { return bIsActive; }

	/** 设置化合策略的激活状态 */
	void SetActive(const bool bNewActive) { bIsActive = bNewActive; }

#pragma endregion

#pragma region SPI

	/**
	 * 化合条件判定
	 * 策略可同时检查 BlackboardGameplayTags（GameplayTags）与 BlackboardNativeTags（FName 原生标签），
	 * 两者互不干扰，策略按需择一或组合判定。
	 * @param BlackboardGameplayTags  当前全局黑板的 GameplayTags 快照
	 * @param BlackboardNativeTags      当前全局收集的 FName 原生标签快照（Actor.Tags + Component.ComponentTags）
	 * @return 标签组合是否满足本策略的化合方程式
	 */
	UFUNCTION(
		BlueprintNativeEvent,
		BlueprintCallable,
		Category = "SingularisCombine|引力奇点化合|SPI",
		meta = (DisplayName = "CanReaction")
	)
	bool CanReaction(
		const FGameplayTagContainer& BlackboardGameplayTags,
		const TArray<FName>& BlackboardNativeTags
	) const;

	/**
	 * 执行化合正向反应
	 * @param Context         化合上下文，内含 Instigator / Avatar / Target 等场景引用
	 * @param BlackboardGameplayTags  当前全局黑板的 GameplayTags 快照
	 * @param BlackboardNativeTags      当前全局收集的 FName 原生标签快照
	 */
	UFUNCTION(
		BlueprintNativeEvent,
		BlueprintCallable,
		Category = "SingularisCombine|引力奇点化合|SPI",
		meta = (DisplayName = "Reaction")
	)
	void Reaction(
		const FSingularisCombineContext& Context,
		const FGameplayTagContainer& BlackboardGameplayTags,
		const TArray<FName>& BlackboardNativeTags
	);

	/**
	 * 执行化合回滚剥离
	 * 当黑板条件不再满足或管线被前置策略中断时调用，用于清理状态。
	 * @param Context                  化合上下文，用于定位并还原 Reaction 产生的副作用
	 * @param BlackboardGameplayTags   当前全局黑板的 GameplayTags 快照（供回滚决策参考）
	 * @param BlackboardNativeTags     当前全局收集的 FName 原生标签快照（供回滚决策参考）
	 */
	UFUNCTION(
		BlueprintNativeEvent,
		BlueprintCallable,
		Category = "SingularisCombine|引力奇点化合|SPI",
		meta = (DisplayName = "ReactionRevert")
	)
	void ReactionRevert(
		const FSingularisCombineContext& Context,
		const FGameplayTagContainer& BlackboardGameplayTags,
		const TArray<FName>& BlackboardNativeTags
	);

	/**
	 * 化合持续反应（每帧调用）
	 * 当策略处于激活状态时每帧触发，仅用于编写不修改木偶状态的瞬态效果（粒子、音效、屏幕震动等）。
	 * 接收 BlackboardTags 供读取当前黑板全貌决策，但严禁在此修改任何状态。
	 * 仅在 bIsActive == true 时调用。
	 * @param Context                  化合上下文
	 * @param BlackboardGameplayTags   当前全局黑板的 GameplayTags 快照
	 * @param BlackboardNativeTags     当前全局收集的 FName 原生标签快照
	 * @param DeltaTime                帧间隔时间（秒）
	 */
	UFUNCTION(
		BlueprintNativeEvent,
		BlueprintCallable,
		Category = "SingularisCombine|引力奇点化合|SPI",
		meta = (DisplayName = "SustainReaction")
	)
	void SustainReaction(
		const FSingularisCombineContext& Context,
		const FGameplayTagContainer& BlackboardGameplayTags,
		const TArray<FName>& BlackboardNativeTags,
		float DeltaTime
	);

#pragma endregion
};
