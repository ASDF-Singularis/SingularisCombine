#include "Components/SingularisCombineComponent.h"

#include <GameplayTagAssetInterface.h>
#include <Engine/World.h>

#include "Objects/SingularisCombineBase.h"
#include "Types/SingularisCombineType.h"

USingularisCombineComponent::USingularisCombineComponent()
{
	SetIsReplicatedByDefault(false);

	PrimaryComponentTick.bStartWithTickEnabled = true;
	PrimaryComponentTick.bCanEverTick = true;

	bAutoActivate = true;
}

void USingularisCombineComponent::BeginPlay()
{
	Super::BeginPlay();

	const UWorld* const World = GetWorld();
	if (!World)
	{
		return;
	}

	/*// 1) 订阅全局 UObject 构造委托：捕获 OwnerActor 上的组件新增
	ObjectConstructedHandle = FCoreUObjectDelegates::OnObjectConstructed.AddUObject(
		this,
		&USingularisCombineComponent::OnOwnerChildComponentConstructed
	);*/

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

	// 3) 初始评估
	EvaluatePipeline();
}

void USingularisCombineComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	/*// 1) 移除全局构造委托订阅
	if (ObjectConstructedHandle.IsValid())
	{
		FCoreUObjectDelegates::OnObjectConstructed.Remove(ObjectConstructedHandle);
		ObjectConstructedHandle.Reset();
	}*/

	// 2) 清理定时器
	if (const UWorld* const World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(PeriodicEvaluateHandle);
		World->GetTimerManager().ClearTimer(PendingEvaluateHandle);
	}

	Super::EndPlay(EndPlayReason);
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
	{
		return;
	}

	// 1) 构建化合上下文
	FSingularisCombineContext Context;
	Context.Avatar = OwnerActor;
	Context.Instigator = Cast<AActor>(OwnerActor->GetInstigator());
	Context.Target = nullptr;
	Context.CombineComponent = this;

	// 2) 当评估间隔为 0 时，每帧执行管线评估
	if (AutoEvaluateInterval <= 0.0f)
	{
		EvaluatePipeline();
	}

	// 3) 遍历激活的策略并调用 SustainReaction
	for (const FSingularisCombineEntry& Entry : CombinePipeline.Combines)
	{
		USingularisCombine* const Combine = Entry.Combine;
		if (!Combine || !Combine->IsActive())
		{
			continue;
		}

		Combine->SustainReaction(Context, BlackboardTags, NativeBlackboardTags, DeltaTime);
	}
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
				Combine->ReactionRevert(Context, BlackboardTags, NativeBlackboardTags);
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
	{
		return;
	}

	// 1) 刷新全局黑板（GameplayTags + 原生 FName 标签）；仅在标签集合实际变更时广播
	CollectAllTags();

	const bool bGameplayTagsChanged = BlackboardTags != PreviousBlackboardTags;
	const bool bNativeTagsChanged = NativeBlackboardTags != PreviousNativeBlackboardTags;

	if (bGameplayTagsChanged || bNativeTagsChanged)
	{
		PreviousBlackboardTags = BlackboardTags;
		PreviousNativeBlackboardTags = NativeBlackboardTags;
		OnCombineBlackboardUpdatedEvent.Broadcast(BlackboardTags, NativeBlackboardTags);
	}

	// 2) 构建化合上下文
	FSingularisCombineContext Context;
	Context.Instigator = Cast<AActor>(OwnerActor->GetInstigator());
	Context.Avatar = OwnerActor;
	Context.Target = OwnerActor;
	Context.CombineComponent = this;

	// 3) 遍历并执行化合管线中的策略列表
	for (FSingularisCombineEntry& Entry : CombinePipeline.Combines)
	{
		USingularisCombine* const Combine = Entry.Combine;
		if (!Combine)
		{
			continue;
		}

		if (Combine->CanReaction(BlackboardTags, NativeBlackboardTags))
		{
			if (!Combine->IsActive())
			{
				Combine->Reaction(Context, BlackboardTags, NativeBlackboardTags);
				Combine->SetActive(true);
			}
		}
		else
		{
			if (Combine->IsActive())
			{
				Combine->ReactionRevert(Context, BlackboardTags, NativeBlackboardTags);
				Combine->SetActive(false);
			}
		}
	}
}

void USingularisCombineComponent::CollectAllTags()
{
	BlackboardTags.Reset();
	NativeBlackboardTags.Reset();

	const AActor* const OwnerActor = GetOwner();
	if (!OwnerActor)
	{
		return;
	}

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
	{
		NativeBlackboardTags.Append(Comp->ComponentTags);
	}
}

// ReSharper disable once CppParameterMayBeConstPtrOrRef
void USingularisCombineComponent::OnOwnerChildComponentConstructed(UObject* Object)
{
	// 过滤：仅关注以本组件 OwnerActor 为 Outer 的 UActorComponent 构造事件
	if (!IsValid(Object) || !Object->IsA<UActorComponent>())
	{
		return;
	}

	const AActor* const OwnerActor = GetOwner();
	if (!OwnerActor || Object->GetOuter() != OwnerActor)
	{
		return;
	}

	// 合并同帧内的多次构造为单次评估，避免管线被重复触发
	const UWorld* const World = GetWorld();
	if (!World)
	{
		return;
	}

	World->GetTimerManager().ClearTimer(PendingEvaluateHandle);
	World->GetTimerManager().SetTimer(
		PendingEvaluateHandle,
		FTimerDelegate::CreateUObject(this, &USingularisCombineComponent::EvaluatePipeline),
		0.0f,
		false
	);
}
