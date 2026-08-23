#include "Components/SingularisCombineComponent.h"

#include <GameplayTagAssetInterface.h>
#include <Engine/World.h>

#include "SingularisCombine.h"
#include "Objects/SingularisCombineBase.h"
#include "Types/SingularisCombineComponentType.h"
#include "Types/SingularisCombineDependencyRegistry.h"
#include "Types/SingularisCombineDependencyScope.h"
#include "Types/SingularisCombineTransientPayload.h"

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

	const UWorld* World = GetWorld();
	if (!World) return;

	// 1) 服务器端：将化合管线中的 Instanced 子对象注册至网络复制列表
	if (GetOwner() && GetOwner()->HasAuthority())
		RegisterCombineSubObjects();

	// 2) 启动周期性评估定时器：兜底检测组件移除与标签变更
	if (AutoEvaluateInterval > 0.0f)
	{
		World->GetTimerManager().SetTimer(
			PeriodicEvaluateHandle,
			FTimerDelegate::CreateUObject(this, &USingularisCombineComponent::EvaluatePipeline),
			AutoEvaluateInterval,
			true
		);
	}

	// 3) 初始前置声明依赖解析
	ResolveDependencies();

	// 3) 初始评估：收集标签并（服务端）执行管线
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

	// 3) 所有端：遍历激活的策略并调用 SustainReaction（驱动瞬态效果）
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

UActorComponent* USingularisCombineComponent::GetDeclaredComponent(
	const ESingularisCombineDependencyScope Scope,
	const TSubclassOf<UActorComponent> ComponentClass
) const
{
	if (!ComponentClass)
	{
		UE_LOG(LogSingularisCombine, Warning, TEXT("GetDeclaredComponent：ComponentClass 为空"));
		return nullptr;
	}

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

			UE_LOG(
				LogSingularisCombine,
				Warning,
				TEXT("[%s] GetDeclaredComponent：缓存命中但弱引用失效（Scope=%d, Class=%s）"),
				*GetNameSafe(GetOwner()),
				static_cast<int32>(Scope),
				*GetNameSafe(ComponentClass)
			);
		}
		else
		{
			UE_LOG(
				LogSingularisCombine,
				Warning,
				TEXT("[%s] GetDeclaredComponent：作用域缓存中未找到声明类型（Scope=%d, Class=%s）"),
				*GetNameSafe(GetOwner()),
				static_cast<int32>(Scope),
				*GetNameSafe(ComponentClass)
			);
		}
	}
	else
	{
		UE_LOG(
			LogSingularisCombine,
			Warning,
			TEXT("[%s] GetDeclaredComponent：作用域缓存槽位不存在（Scope=%d, Class=%s）"),
			*GetNameSafe(GetOwner()),
			static_cast<int32>(Scope),
			*GetNameSafe(ComponentClass)
		);
	}

	return nullptr;
}

USingularisCombineComponent* USingularisCombineComponent::GetFromStrategy(const USingularisCombine* Strategy)
{
	// 1) CDO 与空值保护：CDO 的 Outer 为其 UClass，非组件实例
	if (!Strategy || Strategy->HasAnyFlags(RF_ClassDefaultObject))
	{
		UE_LOG(
			LogSingularisCombine,
			Warning,
			TEXT("GetFromStrategy：Strategy 为空或为 CDO（Strategy=%s）"),
			*GetNameSafe(Strategy)
		);
		return nullptr;
	}

	// 2) 策略作为 UObject 子对象挂载于组件，Outer 即为化合组件
	USingularisCombineComponent* const OwnerComponent = Cast<USingularisCombineComponent>(Strategy->GetOuter());
	if (!OwnerComponent)
	{
		UE_LOG(
			LogSingularisCombine,
			Warning,
			TEXT("GetFromStrategy：Strategy 的 Outer 非化合组件（Strategy=%s, Outer=%s）"),
			*GetNameSafe(Strategy),
			*GetNameSafe(Strategy->GetOuter())
		);
	}
	return OwnerComponent;
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
	EvaluatePipeline();
}

void USingularisCombineComponent::ClearPipeline()
{
	SetPipeline(FSingularisCombinePipeline{});
}

void USingularisCombineComponent::AddCombineEntry(const FSingularisCombineEntry& Entry)
{
	CombinePipeline.Combines.Add(Entry);
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

void USingularisCombineComponent::ResolveDependencies()
{
	AActor* OwnerActor = GetOwner();
	if (!OwnerActor) return;

	FSingularisCombineContext Context;
	Context.Instigator = Cast<AActor>(OwnerActor->GetInstigator());
	Context.Avatar = OwnerActor;
	Context.Target = OwnerActor;
	Context.CombineComponent = this;

	UE_LOG(
		LogSingularisCombine,
		Display,
		TEXT("[%s] ResolveDependencies：开始预解析（管线策略=%d）"),
		*GetNameSafe(GetOwner()),
		CombinePipeline.Combines.Num()
	);

	CachedDependencies.Reset();

	// 1) 收集所有策略声明的依赖并集，按作用域分组
	TMap<ESingularisCombineDependencyScope, TSet<TSubclassOf<UActorComponent>>> AggregatedDeps;
	for (const FSingularisCombineEntry& Entry : CombinePipeline.Combines)
	{
		const USingularisCombine* const Combine = Entry.Combine;
		if (!Combine)
			continue;

		const TMap<ESingularisCombineDependencyScope, FSingularisCombineDependencyList> Declared =
			GetDeclaredComponentClasses(Combine->GetClass());

		UE_LOG(
			LogSingularisCombine,
			Display,
			TEXT("[%s] ResolveDependencies：策略 %s 声明 %d 个作用域"),
			*GetNameSafe(GetOwner()),
			*GetNameSafe(Combine->GetClass()),
			Declared.Num()
		);

		for (const auto& Pair : Declared)
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
	{
		UE_LOG(
			LogSingularisCombine,
			Display,
			TEXT("[%s] ResolveDependencies：管线策略无任何声明依赖（缓存为空）"),
			*GetNameSafe(GetOwner())
		);
		return;
	}

	// 2) 将每个作用域映射到 Context 中的 Actor，查找并缓存到对应作用域槽位
	for (const auto& Pair : AggregatedDeps)
	{
		const AActor* ScopeActor = GetContextActor(Context, Pair.Key);
		if (!ScopeActor || Pair.Value.IsEmpty())
		{
			UE_LOG(
				LogSingularisCombine,
				Warning,
				TEXT("[%s] ResolveDependencies：作用域 %d 的 Actor 缺失或声明为空（Actor=%s）"),
				*GetNameSafe(GetOwner()),
				static_cast<int32>(Pair.Key),
				*GetNameSafe(ScopeActor)
			);
			continue;
		}

		TMap<TSubclassOf<UActorComponent>, TWeakObjectPtr<UActorComponent>>& ScopeCache = CachedDependencies.FindOrAdd(
			Pair.Key
		);
		for (const TSubclassOf<UActorComponent>& DepClass : Pair.Value)
		{
			if (UActorComponent* const Found = ScopeActor->GetComponentByClass(DepClass))
				ScopeCache.Add(DepClass, Found);
			else
				UE_LOG(
				LogSingularisCombine,
				Warning,
				TEXT("[%s] ResolveDependencies：作用域 %d 的 Actor %s 上未找到声明组件 %s"),
				*GetNameSafe(GetOwner()),
				static_cast<int32>(Pair.Key),
				*GetNameSafe(ScopeActor),
				*GetNameSafe(DepClass)
			);
		}
	}
}

TMap<ESingularisCombineDependencyScope, FSingularisCombineDependencyList>
USingularisCombineComponent::GetDeclaredComponentClasses(const UClass* StrategyClass)
{
	TMap<ESingularisCombineDependencyScope, FSingularisCombineDependencyList> Result;

	if (!StrategyClass)
		return Result;

	// 1) 沿继承链聚合所有祖先的注册表声明（含自身）：子策略继承父策略的全部声明
	for (const UClass* Class = StrategyClass; Class; Class = Class->GetSuperClass())
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

	return Result;
}

bool USingularisCombineComponent::AreDependenciesSatisfied(
	const USingularisCombine* Strategy,
	const FSingularisCombineContext& Context
) const
{
	if (!Strategy)
		return true;

	const TMap<ESingularisCombineDependencyScope, FSingularisCombineDependencyList>& Declared =
		GetDeclaredComponentClasses(Strategy->GetClass());

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
