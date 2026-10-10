#pragma once

#include <CoreMinimal.h>
#include <GameplayTagContainer.h>
#include <TimerManager.h>
#include <Components/ActorComponent.h>

#include "Types/SingularisCombineComponentType.h"
#include "Types/SingularisCombineDependencyList.h"
#include "Types/SingularisCombineDependencyScope.h"
#include "Types/SingularisCombineTransientPayload.h"
#include "SingularisCombineComponent.generated.h"

struct FSingularisCombineContext;
class USingularisCombine;

#pragma region 委托签名

/** 黑板更新时广播，同时携带 GameplayTags 与 Actor/Component 原生 FName 标签 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FOnCombineBlackboardUpdatedSignature,
	const FGameplayTagContainer&,
	BlackboardTags,
	const TArray<FName>&,
	NativeTags
);

#pragma endregion

/**
 * 引力奇点化合组件。
 *
 * 挂载于 Actor 上，负责收集同 Actor 下所有组件的 GameplayTags 及原生 FName 标签并维护全局黑板，
 * 驱动化合管线逐策略评估：满足条件时触发 Reaction，不满足时触发 ReactionRevert，所有策略独立判定互不中断。
 *
 * 同时承担声明式依赖的单一职责：登记、查询、缓存全部收口于本组件，
 * 策略类不知晓组件存在（依赖倒置），消除双向耦合。
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
	 * 自动评估轮询间隔（秒）。
	 *
	 * 大于 0.0 时按间隔轮询，兜底检测组件增删与标签变更；设为 0.0 则每帧评估（不启用定时器）。
	 */
	UPROPERTY(
		EditDefaultsOnly,
		BlueprintReadOnly,
		Category = "引力奇点化合组件",
		meta = (DisplayName = "自动评估间隔", ClampMin = "0.0", UIMin = "0.0")
	)
	float AutoEvaluateInterval = 0.1f;

	/** 化合管线：按数组顺序依次评估的策略集合 */
	UPROPERTY(
		EditDefaultsOnly,
		BlueprintReadOnly,
		Category = "引力奇点化合组件",
		meta = (DisplayName = "化合管线")
	)
	FSingularisCombinePipeline CombinePipeline{};

#pragma endregion

#pragma region Event Dispatcher

	/** 组件重新收集全局标签并更新黑板后广播 */
	UPROPERTY(
		BlueprintAssignable,
		Category = "引力奇点化合组件|事件分发器",
		meta = (DisplayName = "黑板更新时")
	)
	FOnCombineBlackboardUpdatedSignature OnCombineBlackboardUpdatedEvent{};

#pragma endregion

private:
#pragma region State

	/** 全局黑板：缓存当前 Actor 上收集到的所有 GameplayTags */
	FGameplayTagContainer BlackboardTags{};

	/** 全局黑板：缓存当前 Actor 上收集到的所有原生 FName 标签 */
	TArray<FName> NativeBlackboardTags{};

	/** 前次 GameplayTags 快照，用于广播幂等性比较 */
	FGameplayTagContainer PreviousBlackboardTags{};

	/** 前次 FName 标签快照，用于广播幂等性比较 */
	TArray<FName> PreviousNativeBlackboardTags{};

	/** 周期性评估定时器句柄 */
	FTimerHandle PeriodicEvaluateHandle{};

	/** TriggerEvaluate 写入的待处理事件载荷，本次评估读取后清空 */
	UPROPERTY(Transient)
	FSingularisCombineTransientPayload PendingPayload{};

	/**
	 * 依赖组件缓存：按作用域分组，每个作用域内按组件类型缓存。
	 * 由 ResolveDependencies 在 BeginPlay 一次性解析，供整个生命周期查询。
	 */
	TMap<ESingularisCombineDependencyScope, TMap<TSubclassOf<UActorComponent>, TWeakObjectPtr<UActorComponent>>>
	CachedDependencies{};

#pragma endregion

public:
#pragma region Constructors

	USingularisCombineComponent();

#pragma endregion

#pragma region ActorComponent Interface

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(
		float DeltaTime,
		ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction
	) override;

#pragma endregion

#pragma region API

	/**
	 * 设置化合管线。
	 *
	 * 回滚当前所有已激活策略的状态，替换为新管线并立即触发重估。
	 * @param InPipeline 新的化合管线。
	 */
	UFUNCTION(
		BlueprintCallable,
		BlueprintAuthorityOnly,
		Category = "引力奇点化合组件|API",
		meta = (DisplayName = "设置管线")
	)
	void SetPipeline(const FSingularisCombinePipeline& InPipeline);

	/** 清空化合管线：移除所有策略并回滚已激活状态后触发重估。 */
	UFUNCTION(
		BlueprintCallable,
		BlueprintAuthorityOnly,
		Category = "引力奇点化合组件|API",
		meta = (DisplayName = "清空管线")
	)
	void ClearPipeline();

	/** 向化合管线末尾追加一条策略条目并触发重估。 */
	UFUNCTION(
		BlueprintCallable,
		BlueprintAuthorityOnly,
		Category = "引力奇点化合组件|API",
		meta = (DisplayName = "追加化合条目")
	)
	void AddCombineEntry(const FSingularisCombineEntry& Entry);

	/** 触发核心重估循环：重新收集全局标签并逐一核对化合策略。 */
	UFUNCTION(
		BlueprintCallable,
		Category = "引力奇点化合组件|API",
		meta = (DisplayName = "评估管线")
	)
	void EvaluatePipeline();

	/**
	 * 事件驱动评估。
	 *
	 * 立即触发一次 EvaluatePipeline，将 Payload 贯穿本次评估所有策略。
	 * @param Payload 事件载荷（事件标识 + 结构化数据），周期轮询触发时为空。
	 */
	UFUNCTION(
		BlueprintCallable,
		BlueprintAuthorityOnly,
		Category = "引力奇点化合组件|API",
		meta = (DisplayName = "触发评估")
	)
	void TriggerEvaluate(const FSingularisCombineTransientPayload& Payload);

#pragma endregion

#pragma region SPI

	/** 获取当前全局黑板上的 GameplayTags 快照 */
	UFUNCTION(
		BlueprintPure,
		Category = "引力奇点化合组件|SPI",
		meta = (DisplayName = "获取黑板标签")
	)
	FGameplayTagContainer GetBlackboardTags() const { return BlackboardTags; }

	/** 获取当前全局黑板上收集的原生 FName 标签快照 */
	UFUNCTION(
		BlueprintPure,
		Category = "引力奇点化合组件|SPI",
		meta = (DisplayName = "获取黑板原生标签")
	)
	const TArray<FName>& GetNativeBlackboardTags() const { return NativeBlackboardTags; }

	/**
	 * 查询预缓存的声明式组件。
	 *
	 * BlueprintPure + DeterminesOutputType：声明节点的输出引脚类型随 ComponentClass 自动推导。
	 * 必须在 ResolveDependencies 之后调用。
	 * @param Scope 依赖作用域（Instigator / Avatar / Target）。
	 * @param ComponentClass 组件类型。
	 * @return 预缓存的组件引用，未找到时返回 nullptr。
	 */
	UFUNCTION(
		BlueprintPure,
		Category = "引力奇点化合组件|SPI",
		meta = (DisplayName = "获取声明组件", DeterminesOutputType = "ComponentClass")
	)
	UActorComponent* GetDeclaredComponent(
		ESingularisCombineDependencyScope Scope,
		TSubclassOf<UActorComponent> ComponentClass
	) const;

	/**
	 * 静态工厂：从策略实例反查所属的化合组件。
	 *
	 * 策略实例作为 UObject 子对象挂载于组件，通过 Outer 链定位。
	 * 供声明节点展开期生成 GetFromStrategy → GetDeclaredComponent 两步调用链，避免策略类反向引用组件。
	 * @param Strategy 策略实例（不可为空，CDO 返回 nullptr）。
	 * @return 挂载该策略的化合组件；Outer 非组件或 CDO 时返回 nullptr。
	 */
	UFUNCTION(
		BlueprintPure,
		Category = "引力奇点化合组件|SPI",
		meta = (DisplayName = "获取所属化合组件")
	)
	static USingularisCombineComponent* GetFromStrategy(const USingularisCombine* Strategy);

#pragma endregion

private:
#pragma region Internal Function

	/** 构建化合上下文：Avatar 与 Target 均为 Owner，Instigator 取 Owner 的发起者。 */
	FSingularisCombineContext MakeContext();

	/**
	 * 预解析管线中所有策略声明式配置的组件。
	 *
	 * 收集所有策略声明的依赖并集，将每个作用域映射到 Context 中的 Actor，
	 * 按声明类型查找并缓存到对应作用域槽位。
	 * @param Context 化合上下文，用于将作用域映射到对应 Actor。
	 */
	void ResolveDependencies(const FSingularisCombineContext& Context);

	/**
	 * 查询指定策略类声明式配置的组件类型集合。
	 *
	 * 沿继承链聚合所有祖先注册表声明（含自身），合并子策略继承父策略的全部声明。
	 * 注册表为唯一真相源，原生路径与蓝图路径统一经由编译 hook 写入。
	 * @param StrategyClass 策略类。
	 * @return 按作用域分组的声明组件类型列表。
	 */
	static TMap<ESingularisCombineDependencyScope, FSingularisCombineDependencyList>
	GetDeclaredComponentClasses(const UClass* StrategyClass);

	/**
	 * 检查策略声明的依赖是否全部满足。
	 *
	 * 空声明返回 true（无条件满足）；任一声明缺失返回 false。
	 * @param Strategy 目标策略。
	 * @param Context 化合上下文，用于将作用域映射到对应 Actor。
	 * @return 声明依赖是否全部命中缓存。
	 */
	bool AreDependenciesSatisfied(const USingularisCombine* Strategy, const FSingularisCombineContext& Context) const;

	/** 收集同 Actor 下所有 GameplayTags 及原生 FName 标签 */
	void CollectAllTags();

	/** 将化合管线中全部有效的 Instanced 化合子对象添加至网络复制列表。仅在服务器端执行。 */
	void RegisterCombineSubObjects();

	/** 将化合管线中全部已注册的 Instanced 化合子对象从网络复制列表中移除。仅在服务器端执行。 */
	void UnregisterCombineSubObjects();

	/** 将依赖作用域映射为 Context 中对应的 Actor */
	static AActor* GetContextActor(const FSingularisCombineContext& Context, ESingularisCombineDependencyScope Scope);

#pragma endregion
};
