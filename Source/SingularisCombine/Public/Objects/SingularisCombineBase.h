#pragma once

#include <CoreMinimal.h>
#include <GameplayTagContainer.h>
#include <Templates/Casts.h>
#include <UObject/Object.h>
#include <UObject/WeakInterfacePtr.h>

#include "Interfaces/SingularisCombineDependencyProvider.h"
#include "Types/SingularisCombineDependencyList.h"
#include "Types/SingularisCombineDependencyScope.h"
#include "Types/SingularisCombineTransientPayload.h"
#include "Types/SingularisCombineType.h"
#include "SingularisCombineBase.generated.h"

class UActorComponent;
class USingularisCombineComponent;

/**
 * 引力奇点抽象基类
 * 定义化合策略的标准接口：条件判断 (CanReaction)、正向反应 (Reaction) 与回滚剥离 (ReactionRevert)。
 * 子类通过覆写 BlueprintNativeEvent 实现自定义化合逻辑。
 * 所有 SPI 方法同时接收 GameplayTags 与 Actor/Component 原生 FName 标签，两者独立、互不冲突。
 *
 * 作为 UObject 子对象挂载于 USingularisCombineComponent，
 * 通过 AddReplicatedSubObject 注册至组件级复制列表，bIsActive 属性的变更将同步至所有客户端。
 */
UCLASS(Abstract, Blueprintable, EditInlineNew, CollapseCategories)
class SINGULARISCOMBINE_API USingularisCombine : public UObject
{
	GENERATED_BODY()

public:
#pragma region Parameter

	/**
	 * 当前策略是否处于激活状态。变更时复制至所有客户端并触发 OnRep_IsActive 回调。
	 */
	UPROPERTY(
		ReplicatedUsing = OnRep_IsActive,
		EditDefaultsOnly,
		BlueprintReadWrite,
		Category = "SingularisCombine|引力奇点化合|参数",
		meta = (DisplayName = "激活")
	)
	bool bIsActive = false;

#pragma endregion

#pragma region UObject Interface

	virtual UWorld* GetWorld() const override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	virtual bool IsSupportedForNetworking() const override;
	virtual int32 GetFunctionCallspace(UFunction* Function, FFrame* Stack) override;
	virtual bool CallRemoteFunction(UFunction* Function, void* Parms, FOutParmRec* OutParms, FFrame* Stack) override;

#pragma endregion

#pragma region State

	/** 查询当前化合策略是否处于激活状态 */
	UFUNCTION(
		BlueprintPure,
		Category = "SingularisCombine|引力奇点化合|State",
		meta = (DisplayName = "IsActive")
	)
	bool IsActive() const { return bIsActive; }

#pragma endregion

#pragma region API

	/** 设置化合策略的激活状态 */
	UFUNCTION(
		BlueprintCallable,
		Category = "SingularisCombine|引力奇点化合|API",
		meta = (DisplayName = "[Name]")
	)
	void SetActive(const bool bNewActive) { bIsActive = bNewActive; }

#pragma endregion

#pragma region SPI

	/**
	 * 化合条件判定
	 * 策略可同时检查 BlackboardGameplayTags（GameplayTags）与 BlackboardNativeTags（FName 原生标签），
	 * 两者互不干扰，策略按需择一或组合判定。
	 * @param Context                  化合上下文，内含 Instigator / Avatar / Target 等场景引用
	 * @param Payload                  事件驱动评估载荷（周期轮询时为空，TriggerEvaluate 触发时填充）
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
		const FSingularisCombineContext& Context,
		const FSingularisCombineTransientPayload& Payload,
		const FGameplayTagContainer& BlackboardGameplayTags,
		const TArray<FName>& BlackboardNativeTags
	) const;

	/**
	 * 执行化合正向反应
	 * @param Context         化合上下文，内含 Instigator / Avatar / Target 等场景引用
	 * @param Payload         事件驱动评估载荷（周期轮询时为空，TriggerEvaluate 触发时填充）
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
		const FSingularisCombineTransientPayload& Payload,
		const FGameplayTagContainer& BlackboardGameplayTags,
		const TArray<FName>& BlackboardNativeTags
	);

	/**
	 * 执行化合回滚剥离
	 * 当黑板条件不再满足或管线被前置策略中断时调用，用于清理状态。
	 * @param Context                  化合上下文，用于定位并还原 Reaction 产生的副作用
	 * @param Payload                  事件驱动评估载荷（周期轮询时为空，TriggerEvaluate 触发时填充）
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
		const FSingularisCombineTransientPayload& Payload,
		const FGameplayTagContainer& BlackboardGameplayTags,
		const TArray<FName>& BlackboardNativeTags
	);

	/**
	 * 化合持续反应（每帧调用）
	 * 当策略处于激活状态时每帧触发，仅用于编写不修改木偶状态的瞬态效果（粒子、音效、屏幕震动等）。
	 * 接收 BlackboardTags 供读取当前黑板全貌决策，但严禁在此修改任何状态。
	 * 仅在 bIsActive == true 时调用。
	 * @param Context                  化合上下文
	 * @param Payload                  事件驱动评估载荷（周期轮询时为空，TriggerEvaluate 触发时填充）
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
		const FSingularisCombineTransientPayload& Payload,
		const FGameplayTagContainer& BlackboardGameplayTags,
		const TArray<FName>& BlackboardNativeTags,
		float DeltaTime
	);

	UFUNCTION(
		BlueprintNativeEvent,
		BlueprintCallable,
		Category = "SingularisCombine|引力奇点化合|SPI",
		meta = (DisplayName = "OnRep_IsActive")
	)
	void OnRep_IsActive() const;

#pragma endregion

#pragma region Dependency Injection

	/**
	 * 注入依赖查询提供者
	 * 由化合组件在注册/替换管线时调用，将自身注入为策略的依赖查询入口，
	 * 解除策略对具体组件类的直接引用（依赖倒置）。
	 * @param Provider  依赖查询提供者（通常为挂载本策略的化合组件）
	 */
	void SetDependencyProvider(ISingularisCombineDependencyProvider* Provider);

	/**
	 * 获取注入的依赖查询提供者（化合组件实例）
	 * 仅供蓝图声明节点经反射调用；查询能力下沉于提供者自身，策略不承载查询逻辑。
	 * @return 注入的化合组件，未注入时返回 nullptr
	 */
	UFUNCTION()
	USingularisCombineComponent* GetDependencyProvider() const;

#pragma endregion

protected:
#pragma region Internal Variable

	/** 化合组件注入的依赖查询提供者（弱引用，随组件生命周期自动失效；宏访问器直调，不承载查询逻辑） */
	TWeakInterfacePtr<ISingularisCombineDependencyProvider> DependencyProvider{};

#pragma endregion

private:
#pragma region Internal Variable

	/** 友元：编辑器编译 hook 回填 CDO 声明 */
	friend class FSingularisCombineBlueprintCompileHook;

	/** 友元：化合组件归一声明集合时读取 CDO 声明 */
	friend class USingularisCombineComponent;

	/**
	 * 声明式组件（收口）
	 * 仅服务蓝图 CDO 回填路径与运行期内部读取；外部不可编辑、不可直接读写。
	 * 原生路径下注册表为唯一真相源，本字段闲置。
	 */
	UPROPERTY()
	TMap<ESingularisCombineDependencyScope, FSingularisCombineDependencyList> DeclaredComponents{};

#pragma endregion
};
