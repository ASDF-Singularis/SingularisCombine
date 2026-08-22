#include "Objects/SingularisCombineBase.h"

#include <Engine/NetDriver.h>
#include <GameFramework/Actor.h>
#include <Net/UnrealNetwork.h>

#include "Components/SingularisCombineComponent.h"
#include "Types/SingularisCombineTransientPayload.h"

UWorld* USingularisCombine::GetWorld() const
{
	// 1) 排除 CDO：防止在编辑器启动或序列化时获取错误的上下文
	if (HasAnyFlags(RF_ClassDefaultObject)) return nullptr;

	// 2) 通过 Outer 链（CombineComponent → OwnerActor）获取 WorldContext
	if (const UObject* const Outer = GetOuter()) return Outer->GetWorld();

	return Super::GetWorld();
}

void USingularisCombine::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(USingularisCombine, bIsActive);
}

bool USingularisCombine::IsSupportedForNetworking() const
{
	return true;
}

int32 USingularisCombine::GetFunctionCallspace(UFunction* Function, FFrame* Stack)
{
	// 1) CDO 不支持网络调用，直接返回 Local
	if (HasAnyFlags(RF_ClassDefaultObject) || !IsSupportedForNetworking()) return FunctionCallspace::Local;

	// 2) 通过 Outer（CombineComponent）链式委托，转由 UActorComponent::GetFunctionCallspace 再委托至 Owner Actor
	return GetOuter()->GetFunctionCallspace(Function, Stack);
}

bool USingularisCombine::CallRemoteFunction(
	UFunction* Function,
	void* Parms,
	FOutParmRec* OutParms,
	FFrame* Stack
)
{
	// 1) CDO 不支持网络调用
	if (HasAnyFlags(RF_ClassDefaultObject)) return false;

	// 2) 沿 Outer 链查找 Owner Actor，通过其 NetDriver 转发 RPC
	AActor* OwnerActor = GetTypedOuter<AActor>();
	if (!IsValid(OwnerActor)) return false;

	UNetDriver* NetDriver = OwnerActor->GetNetDriver();
	if (!IsValid(NetDriver)) return false;

	// 3) 将 this（子对象）作为最后一个参数传入，使 NetDriver 正确路由子对象上的 RPC
	NetDriver->ProcessRemoteFunction(OwnerActor, Function, Parms, OutParms, Stack, this);

	return true;
}

bool USingularisCombine::CanReaction_Implementation(
	const FSingularisCombineContext& Context,
	const FSingularisCombineTransientPayload& Payload,
	const FGameplayTagContainer& BlackboardGameplayTags,
	const TArray<FName>& BlackboardNativeTags
) const
{
	return true;
}

void USingularisCombine::Reaction_Implementation(
	const FSingularisCombineContext& Context,
	const FSingularisCombineTransientPayload& Payload,
	const FGameplayTagContainer& BlackboardGameplayTags,
	const TArray<FName>& BlackboardNativeTags
) {}

void USingularisCombine::ReactionRevert_Implementation(
	const FSingularisCombineContext& Context,
	const FSingularisCombineTransientPayload& Payload,
	const FGameplayTagContainer& BlackboardGameplayTags,
	const TArray<FName>& BlackboardNativeTags
) {}

void USingularisCombine::SustainReaction_Implementation(
	const FSingularisCombineContext& Context,
	const FSingularisCombineTransientPayload& Payload,
	const FGameplayTagContainer& BlackboardGameplayTags,
	const TArray<FName>& BlackboardNativeTags,
	float DeltaTime
) {}

void USingularisCombine::OnRep_IsActive_Implementation() const {}

void USingularisCombine::ResolveDependencies()
{
	CachedDependencies.Reset();

	if (ComponentDependencies.IsEmpty())
		return;

	// 1) 沿 Outer 链查找化合组件 → OwnerActor
	USingularisCombineComponent* const CombineComponent = Cast<USingularisCombineComponent>(GetOuter());
	if (!CombineComponent)
		return;

	AActor* const OwnerActor = CombineComponent->GetOwner();
	if (!OwnerActor)
		return;

	// 2) 遍历声明的依赖类型，在 OwnerActor 上查找并缓存
	for (const TSubclassOf<UActorComponent>& DepClass : ComponentDependencies)
	{
		if (!DepClass)
			continue;

		UActorComponent* const Found = OwnerActor->GetComponentByClass(DepClass);
		if (Found)
			CachedDependencies.Add(DepClass, Found);
	}
}

UActorComponent* USingularisCombine::GetDependency(TSubclassOf<UActorComponent> ComponentClass) const
{
	if (!ComponentClass)
		return nullptr;

	// 1) 从预解析缓存中读取
	const TWeakObjectPtr<UActorComponent>* const Found = CachedDependencies.Find(ComponentClass);
	if (Found && Found->IsValid())
		return Found->Get();

	return nullptr;
}

UActorComponent* USingularisCombine::GetAvatarComponent(TSubclassOf<UActorComponent> ComponentClass) const
{
	if (!ComponentClass)
		return nullptr;

	// 1) 沿 Outer 链查找化合组件 → OwnerActor
	USingularisCombineComponent* const CombineComponent = Cast<USingularisCombineComponent>(GetOuter());
	if (!CombineComponent)
		return nullptr;

	AActor* const OwnerActor = CombineComponent->GetOwner();
	if (!OwnerActor)
		return nullptr;

	// 2) 在 OwnerActor 上即时查找目标组件
	return OwnerActor->GetComponentByClass(ComponentClass);
}
