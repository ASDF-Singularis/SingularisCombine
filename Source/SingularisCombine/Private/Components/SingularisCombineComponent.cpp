#include "Components/SingularisCombineComponent.h"

#include <GameplayTagAssetInterface.h>
#include <Engine/World.h>

#include "Objects/SingularisCombineBase.h"
#include "Types/SingularisCombineComponentType.h"
#include "Types/SingularisCombineDependencyRegistry.h"
#include "Types/SingularisCombineDependencyScope.h"
#include "Types/SingularisCombineTransientPayload.h"

TMap<ESingularisCombineDependencyScope, FSingularisCombineDependencyList>
USingularisCombineComponent::CollectDeclaredComponentClasses(const USingularisCombine* Strategy)
{
	TMap<ESingularisCombineDependencyScope, FSingularisCombineDependencyList> Result;
	if (!Strategy)
		return Result;

	// 1) 沿继承链聚合所有祖先的原生注册表声明（含自身）：子策略继承父策略的全部声明
	for (UClass* Class = Strategy->GetClass(); Class; Class = Class->GetSuperClass())
	{
		if (const TMap<ESingularisCombineDependencyScope, FSingularisCombineDependencyList>* const Found =
			FSingularisCombineDependencyRegistry::Get().FindDeclaredClasses(Class))
		{
			for (const auto& Pair : *Found)
			{
				for (const TSubclassOf<UActorComponent>& DepClass : Pair.Value.Classes)
					Result.FindOrAdd(Pair.Key).Classes.AddUnique(DepClass);
			}
		}
	}

	// 2) 合并 CDO 声明（蓝图回填路径，friend 授权读取；读取 CDO 保证热重载后新鲜）
	if (const USingularisCombine* const CDO = Strategy->GetClass()->GetDefaultObject<USingularisCombine>())
	{
		for (const auto& Pair : CDO->DeclaredComponents)
		{
			for (const TSubclassOf<UActorComponent>& DepClass : Pair.Value.Classes)
				Result.FindOrAdd(Pair.Key).Classes.AddUnique(DepClass);
		}
	}

	return Result;
}

USingularisCombineComponent::USingularisCombineComponent()
{
	SetIsReplicatedByDefault(true);
	bReplicateUsingRegisteredSubObjectList = true;

	PrimaryComponentTick.bStartWithTickEnabled = true;
	PrimaryComponentTick.bCanEverTick = true;

	bAutoActivate = true;
}

void USingularisCombineComponent::BeginPlay()
{
	Super::BeginPlay();

	const UWorld* const World = GetWorld();
	if (!World)
		return;

	// 1) 服务器端：将化合管线中的 Instanced 子对象注册至网络复制列表
	if (GetOwner() && GetOwner()->HasAuthority())
		RegisterCombineSubObjects();

	// 2) 将本组件注入为管线策略的依赖查询提供者
	BindDependencyProvider();

	// 3) 启动周期性评估定时器：兜底检测组件移除与标签变更
	if (AutoEvaluateInterval > 0.0f)
	{
		World->GetTimerManager().SetTimer(
			PeriodicEvaluateHandle,
			FTimerDelegate::CreateUObject(this, &USingularisCombineComponent::EvaluatePipeline),
			AutoEvaluateInterval,
			true
		);
	}

	// 4) 初始评估：收集标签并（服务端）执行管线
	EvaluatePipeline();
}

void USingularisCombineComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UnregisterCombineSubObjects();

	// 1) 清理定时器
	if (const UWorld* const World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(PeriodicEvaluateHandle);
		World->GetTimerManager().ClearTimer(PendingEvaluateHandle);
	}

	Super::EndPlay(EndPlayReason);
}

void USingularisCombineComponent::GetLifetimeReplicatedProps(
	TArray<FLifetimeProperty>& OutLifetimeProps
) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
}

void USingularisCombineComponent::TickComponent(
	const float DeltaTime,
	const ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction
)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	AActor* const OwnerActor = GetOwner();
	if (!OwnerActor)
		return;

	// 1) 构建化合上下文
	FSingularisCombineContext Context;
	Context.Avatar = OwnerActor;
	Context.Instigator = Cast<AActor>(OwnerActor->GetInstigator());
	Context.Target = nullptr;
	Context.CombineComponent = this;

	// 2) 服务端：评估间隔为 0 时每帧执行管线
	if (OwnerActor->HasAuthority() && AutoEvaluateInterval <= 0.0f)
		EvaluatePipeline();

	// 3) 所有端：预解析依赖（保证 SustainReaction 中 GetDeclaredComponent 可用）
	ResolveDependencies(Context);

	// 4) 所有端：遍历激活的策略并调用 SustainReaction（驱动瞬态效果）
	for (const FSingularisCombineEntry& Entry : CombinePipeline.Combines)
	{
		USingularisCombine* const Combine = Entry.Combine;
		if (!Combine || !Combine->IsActive())
			continue;

		Combine->SustainReaction(
			Context,
			FSingularisCombineTransientPayload{},
			BlackboardTags,
			NativeBlackboardTags,
			DeltaTime
		);
	}
}

void USingularisCombineComponent::ResolveDependencies(const FSingularisCombineContext& Context)
{
	CachedDependencies.Reset();

	// 1) 收集所有策略声明的依赖并集，按作用域分组
	TMap<ESingularisCombineDependencyScope, TSet<TSubclassOf<UActorComponent>>> AggregatedDeps;
	for (const FSingularisCombineEntry& Entry : CombinePipeline.Combines)
	{
		const USingularisCombine* const Combine = Entry.Combine;
		if (!Combine)
			continue;

		for (const auto& Pair : CollectDeclaredComponentClasses(Combine))
		{
			TSet<TSubclassOf<UActorComponent>>& TypeSet = AggregatedDeps.FindOrAdd(Pair.Key);
			for (const TSubclassOf<UActorComponent>& DepClass : Pair.Value.Classes)
			{
				if (DepClass)
					TypeSet.Add(DepClass);
			}
		}
	}

	if (AggregatedDeps.IsEmpty())
		return;

	// 2) 将每个作用域映射到 Context 中的 Actor，查找并缓存到对应作用域槽位
	for (const auto& Pair : AggregatedDeps)
	{
		AActor* const ScopeActor = GetContextActor(Context, Pair.Key);
		if (!ScopeActor || Pair.Value.IsEmpty())
			continue;

		TMap<TSubclassOf<UActorComponent>, TWeakObjectPtr<UActorComponent>>& ScopeCache = CachedDependencies.FindOrAdd(
			Pair.Key
		);
		for (const TSubclassOf<UActorComponent>& DepClass : Pair.Value)
		{
			if (UActorComponent* const Found = ScopeActor->GetComponentByClass(DepClass))
				ScopeCache.Add(DepClass, Found);
		}
	}
}

UActorComponent* USingularisCombineComponent::GetDeclaredComponent(
	const ESingularisCombineDependencyScope Scope,
	const TSubclassOf<UActorComponent> ComponentClass
) const
{
	if (!ComponentClass)
		return nullptr;

	// 1) 从缓存中查找作用域 → 组件类型映射
	if (const TMap<TSubclassOf<UActorComponent>, TWeakObjectPtr<UActorComponent>>* const ScopeCache = CachedDependencies
		.Find(
			Scope
		))
	{
		if (const TWeakObjectPtr<UActorComponent>* const Found = ScopeCache->Find(ComponentClass))
		{
			if (Found->IsValid())
				return Found->Get();
		}
	}

	return nullptr;
}

bool USingularisCombineComponent::AreDependenciesSatisfied(
	const USingularisCombine* Strategy,
	const FSingularisCombineContext& Context
) const
{
	if (!Strategy)
		return true;

	const TMap<ESingularisCombineDependencyScope, FSingularisCombineDependencyList>& Declared =
		CollectDeclaredComponentClasses(Strategy);

	// 1) 空声明视为无条件满足
	if (Declared.IsEmpty())
		return true;

	// 2) 逐作用域检查声明类型是否全部命中缓存
	for (const auto& Pair : Declared)
	{
		const TArray<TSubclassOf<UActorComponent>>& DeclaredTypes = Pair.Value.Classes;
		if (DeclaredTypes.IsEmpty())
			continue;

		// 3) 作用域映射的 Actor 缺失 → 声明无法满足
		if (!GetContextActor(Context, Pair.Key))
			return false;

		// 4) 作用域缓存槽位或任一声明类型缺失 → 不满足
		const TMap<TSubclassOf<UActorComponent>, TWeakObjectPtr<UActorComponent>>* const ScopeCache =
			CachedDependencies.Find(Pair.Key);
		if (!ScopeCache)
			return false;

		for (const TSubclassOf<UActorComponent>& DepClass : DeclaredTypes)
		{
			if (!DepClass)
				continue;

			const TWeakObjectPtr<UActorComponent>* const Found = ScopeCache->Find(DepClass);
			if (!Found || !Found->IsValid())
				return false;
		}
	}

	return true;
}

void USingularisCombineComponent::SetPipeline(const FSingularisCombinePipeline& InPipeline)
{
	// 1) 回滚当前管线中所有已激活策略的状态
	if (AActor* const OwnerActor = GetOwner())
	{
		FSingularisCombineContext Context;
		Context.Avatar = OwnerActor;
		Context.Instigator = Cast<AActor>(OwnerActor->GetInstigator());
		Context.Target = nullptr;
		Context.CombineComponent = this;

		for (const FSingularisCombineEntry& Entry : CombinePipeline.Combines)
		{
			if (USingularisCombine* Combine = Entry.Combine; Combine && Combine->IsActive())
			{
				Combine->ReactionRevert(
					Context,
					FSingularisCombineTransientPayload{},
					BlackboardTags,
					NativeBlackboardTags
				);
				Combine->SetActive(false);
			}
		}
	}

	// 2) 替换管线并立即重估
	CombinePipeline = InPipeline;
	BindDependencyProvider();
	EvaluatePipeline();
}

void USingularisCombineComponent::ClearPipeline()
{
	SetPipeline(FSingularisCombinePipeline{});
}

void USingularisCombineComponent::AddCombineEntry(const FSingularisCombineEntry& Entry)
{
	CombinePipeline.Combines.Add(Entry);
	BindDependencyProvider();
	EvaluatePipeline();
}

void USingularisCombineComponent::EvaluatePipeline()
{
	AActor* const OwnerActor = GetOwner();
	if (!OwnerActor)
		return;

	// 1) 所有端：刷新全局黑板（GameplayTags + 原生 FName 标签）；仅在标签集合实际变更时广播
	CollectAllTags();

	const bool bGameplayTagsChanged = BlackboardTags != PreviousBlackboardTags;
	const bool bNativeTagsChanged = NativeBlackboardTags != PreviousNativeBlackboardTags;

	if (bGameplayTagsChanged || bNativeTagsChanged)
	{
		PreviousBlackboardTags = BlackboardTags;
		PreviousNativeBlackboardTags = NativeBlackboardTags;
		OnCombineBlackboardUpdatedEvent.Broadcast(BlackboardTags, NativeBlackboardTags);
	}

	// 2) 服务端权威：构建化合上下文并逐个评估策略管线
	if (!OwnerActor->HasAuthority())
		return;

	FSingularisCombineContext Context;
	Context.Instigator = Cast<AActor>(OwnerActor->GetInstigator());
	Context.Avatar = OwnerActor;
	Context.Target = OwnerActor;
	Context.CombineComponent = this;

	// 3) 读取待处理事件载荷（TriggerEvaluate 写入，周期轮询时为空）
	const FSingularisCombineTransientPayload Payload = PendingPayload;

	// 4) 预解析管线所有策略声明的组件依赖（按作用域映射到 Context 中的 Actor 缓存）
	ResolveDependencies(Context);

	for (FSingularisCombineEntry& Entry : CombinePipeline.Combines)
	{
		USingularisCombine* const Combine = Entry.Combine;
		if (!Combine)
			continue;

		// 前置预检：声明依赖未全部满足 → 不进入 CanReaction，直接回滚
		if (!AreDependenciesSatisfied(Combine, Context))
		{
			if (Combine->IsActive())
			{
				Combine->ReactionRevert(Context, Payload, BlackboardTags, NativeBlackboardTags);
				Combine->SetActive(false);
			}
			continue;
		}

		// 依赖已就绪：CanReaction 可假定声明依赖存在
		if (Combine->CanReaction(Context, Payload, BlackboardTags, NativeBlackboardTags))
		{
			if (!Combine->IsActive())
			{
				Combine->Reaction(Context, Payload, BlackboardTags, NativeBlackboardTags);
				Combine->SetActive(true);
			}
		}
		else
		{
			if (Combine->IsActive())
			{
				Combine->ReactionRevert(Context, Payload, BlackboardTags, NativeBlackboardTags);
				Combine->SetActive(false);
			}
		}
	}
}

void USingularisCombineComponent::TriggerEvaluate(const FSingularisCombineTransientPayload& Payload)
{
	PendingPayload = Payload;
	EvaluatePipeline();
	PendingPayload = FSingularisCombineTransientPayload{};
}

void USingularisCombineComponent::CollectAllTags()
{
	BlackboardTags.Reset();
	NativeBlackboardTags.Reset();

	const AActor* const OwnerActor = GetOwner();
	if (!OwnerActor)
		return;

	// --- GameplayTags 收集 ---

	// 1) 收集 Actor 自身的 GameplayTags
	if (const IGameplayTagAssetInterface* const ActorTagInterface = Cast<IGameplayTagAssetInterface>(OwnerActor))
	{
		FGameplayTagContainer ActorTags;
		ActorTagInterface->GetOwnedGameplayTags(ActorTags);
		BlackboardTags.AppendTags(ActorTags);
	}

	// 2) 收集同 Actor 下所有组件的 GameplayTags
	TArray<UActorComponent*> Components;
	OwnerActor->GetComponents(UActorComponent::StaticClass(), Components);

	for (const UActorComponent* Comp : Components)
	{
		if (const IGameplayTagAssetInterface* const TagInterface = Cast<IGameplayTagAssetInterface>(Comp))
		{
			FGameplayTagContainer CompTags;
			TagInterface->GetOwnedGameplayTags(CompTags);
			BlackboardTags.AppendTags(CompTags);
		}
	}

	// --- 原生 FName 标签收集 ---

	// 3) 收集 Actor 自身的原生 FName 标签
	NativeBlackboardTags.Append(OwnerActor->Tags);

	// 4) 收集同 Actor 下所有组件的原生 FName 标签
	for (const UActorComponent* Comp : Components)
		NativeBlackboardTags.Append(Comp->ComponentTags);
}

void USingularisCombineComponent::RegisterCombineSubObjects()
{
	// 1) 仅服务器端执行子对象注册
	if (!GetOwner() || !GetOwner()->HasAuthority())
		return;

	// 2) 遍历化合管线，将有效的 Instanced 化合子对象添加至网络复制列表
	for (const FSingularisCombineEntry& Entry : CombinePipeline.Combines)
	{
		if (!IsValid(Entry.Combine))
			continue;

		AddReplicatedSubObject(Entry.Combine);
	}
}

void USingularisCombineComponent::UnregisterCombineSubObjects()
{
	// 1) 仅服务器端执行子对象注销
	if (!GetOwner() || !GetOwner()->HasAuthority())
		return;

	// 2) 遍历化合管线，将已注册的化合子对象从网络复制列表中移除
	for (const FSingularisCombineEntry& Entry : CombinePipeline.Combines)
	{
		if (!IsValid(Entry.Combine))
			continue;

		RemoveReplicatedSubObject(Entry.Combine);
	}
}

// ReSharper disable once CppParameterMayBeConstPtrOrRef
void USingularisCombineComponent::OnOwnerChildComponentConstructed(UObject* Object)
{
	// 过滤：仅关注以本组件 OwnerActor 为 Outer 的 UActorComponent 构造事件
	if (!IsValid(Object) || !Object->IsA<UActorComponent>())
		return;

	const AActor* const OwnerActor = GetOwner();
	if (!OwnerActor || Object->GetOuter() != OwnerActor)
		return;

	// 合并同帧内的多次构造为单次评估，避免管线被重复触发
	const UWorld* const World = GetWorld();
	if (!World)
		return;

	World->GetTimerManager().ClearTimer(PendingEvaluateHandle);
	World->GetTimerManager().SetTimer(
		PendingEvaluateHandle,
		FTimerDelegate::CreateUObject(this, &USingularisCombineComponent::EvaluatePipeline),
		0.0f,
		false
	);
}

void USingularisCombineComponent::BindDependencyProvider()
{
	// 1) 将本组件注入为管线中所有策略的依赖查询提供者
	for (const FSingularisCombineEntry& Entry : CombinePipeline.Combines)
	{
		if (USingularisCombine* const Combine = Entry.Combine)
			Combine->SetDependencyProvider(this);
	}
}

AActor* USingularisCombineComponent::GetContextActor(
	const FSingularisCombineContext& Context,
	const ESingularisCombineDependencyScope Scope
)
{
	switch (Scope)
	{
	case ESingularisCombineDependencyScope::Instigator:
		return Context.Instigator;
	case ESingularisCombineDependencyScope::Avatar:
		return Context.Avatar;
	case ESingularisCombineDependencyScope::Target:
		return Context.Target;
	default:
		return nullptr;
	}
}
