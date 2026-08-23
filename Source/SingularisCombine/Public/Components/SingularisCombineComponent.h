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
class UActorComponent;

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

	/**
	 * 依赖组件缓存：按作用域分组，每个作用域内按组件类型缓存
	 * 由 ResolveDependencies(Context) 每次评估前刷新
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

	/**
	 * 查询预缓存的声明式组件
	 * BlueprintPure + DeterminesOutputType：声明节点的输出引脚类型随 ComponentClass 自动推导。
	 * 必须在 ResolveDependencies(Context) 之后调用。
	 * @param Scope           依赖作用域（Instigator / Avatar / Target）
	 * @param ComponentClass  组件类型
	 * @return 预缓存的组件引用，未找到时返回 nullptr
	 */
	UFUNCTION(
		BlueprintPure,
		Category = "SingularisCombine|引力奇点化合组件|State",
		meta = (DisplayName = "获取声明组件", DeterminesOutputType = "ComponentClass")
	)
	UActorComponent* GetDeclaredComponent(
		ESingularisCombineDependencyScope Scope,
		TSubclassOf<UActorComponent> ComponentClass
	) const;

#pragma endregion

#pragma region API

	/**
	 * 静态工厂：从策略实例反查所属的化合组件
	 * 策略实例作为 UObject 子对象挂载于组件，通过 Outer 链定位。
	 * 供声明节点 K2Node 展开期生成 GetFromStrategy → GetDeclaredComponent 两步调用链使用，
	 * 避免策略类持有对组件的反向引用（依赖倒置）。
	 * @param Strategy  策略实例（不可为空，CDO 返回 nullptr）
	 * @return 挂载该策略的化合组件；Outer 非组件或 CDO 时返回 nullptr
	 */
	UFUNCTION(
		BlueprintPure,
		Category = "SingularisCombine|引力奇点化合组件|API",
		meta = (DisplayName = "GetFromStrategy")
	)
	static USingularisCombineComponent* GetFromStrategy(const USingularisCombine* Strategy);

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

	/**
	* 预解析管线中所有策略声明式配置的组件
	* 收集所有策略声明的依赖并集，将每个作用域映射到 Context 中的 Actor，
	 * 按声明类型查找并缓存到对应作用域槽位。
	 * 必须在策略评估前调用；策略通过 GetDeclaredComponent 读取缓存。
	*/
	void ResolveDependencies();

	/**
	 * 查询指定策略类声明式配置的组件类型集合
	 * 沿继承链聚合所有祖先注册表声明（含自身），合并子策略继承父策略的全部声明。
	 * 注册表为唯一真相源，原生路径与蓝图路径统一经由编译 hook 写入。
	 * @param StrategyClass  策略类
	 * @return 按作用域分组的声明组件类型列表
	 */
	static TMap<ESingularisCombineDependencyScope, FSingularisCombineDependencyList>
	GetDeclaredComponentClasses(const UClass* StrategyClass);

	/**
	* 检查策略声明的依赖是否全部满足
	* 空声明返回 true（无条件满足）；任一声明缺失返回 false
	* @param Strategy  目标策略
	 * @param Context   化合上下文，用于将作用域映射到 Actor
	 * @return 声明依赖是否全部命中缓存
	 */
	bool AreDependenciesSatisfied(const USingularisCombine* Strategy, const FSingularisCombineContext& Context) const;

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

	/** 将依赖作用域映射为 Context 中对应的 Actor */
	static AActor* GetContextActor(const FSingularisCombineContext& Context, ESingularisCombineDependencyScope Scope);

#pragma endregion
};
