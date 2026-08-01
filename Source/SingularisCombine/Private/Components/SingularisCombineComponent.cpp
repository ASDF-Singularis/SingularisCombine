#include "Components/SingularisCombineComponent.h"

#include <GameplayTagAssetInterface.h>
#include <Engine/World.h>
#include <Net/UnrealNetwork.h>

#include "Objects/SingularisCombineBase.h"
#include "Types/SingularisCombineType.h"

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
	{
		return;
	}

	// 1) 服务器端：将化合管线中的 Instanced 子对象注册至网络复制列表
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		RegisterCombineSubObjects();
	}

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
	{
		return;
	}

	// 1) 构建化合上下文
	FSingularisCombineContext Context;
	Context.Avatar = OwnerActor;
	Context.Instigator = Cast<AActor>(OwnerActor->GetInstigator());
	Context.Target = nullptr;
	Context.CombineComponent = this;

	// 2) 服务端：评估间隔为 0 时每帧执行管线
	if (OwnerActor->HasAuthority() && AutoEvaluateInterval <= 0.0f)
	{
		EvaluatePipeline();
	}

	// 3) 所有端：遍历激活的策略并调用 SustainReaction（驱动瞬态效果）
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
	{
		return;
	}

	FSingularisCombineContext Context;
	Context.Instigator = Cast<AActor>(OwnerActor->GetInstigator());
	Context.Avatar = OwnerActor;
	Context.Target = OwnerActor;
	Context.CombineComponent = this;

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

void USingularisCombineComponent::RegisterCombineSubObjects()
{
	// 1) 仅服务器端执行子对象注册
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}

	// 2) 遍历化合管线，将有效的 Instanced 化合子对象添加至网络复制列表
	for (const FSingularisCombineEntry& Entry : CombinePipeline.Combines)
	{
		if (!IsValid(Entry.Combine))
		{
			continue;
		}

		AddReplicatedSubObject(Entry.Combine);
	}
}

void USingularisCombineComponent::UnregisterCombineSubObjects()
{
	// 1) 仅服务器端执行子对象注销
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}

	// 2) 遍历化合管线，将已注册的化合子对象从网络复制列表中移除
	for (const FSingularisCombineEntry& Entry : CombinePipeline.Combines)
	{
		if (!IsValid(Entry.Combine))
		{
			continue;
		}

		RemoveReplicatedSubObject(Entry.Combine);
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
