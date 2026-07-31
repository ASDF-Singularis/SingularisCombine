#include "Objects/SingularisCombineBase.h"

UWorld* USingularisCombine::GetWorld() const
{
	// 1) CDO 在编辑器启动或序列化期间没有有效的 World 上下文
	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		return nullptr;
	}

	// 2) 通过 Outer 获取 World 上下文
	if (const UObject* const Outer = GetOuter())
	{
		return Outer->GetWorld();
	}

	return Super::GetWorld();
}

bool USingularisCombine::CanReaction_Implementation(
	const FGameplayTagContainer& BlackboardGameplayTags,
	const TArray<FName>& BlackboardNativeTags
) const
{
	return true;
}

void USingularisCombine::Reaction_Implementation(
	const FSingularisCombineContext& Context,
	const FGameplayTagContainer& BlackboardGameplayTags,
	const TArray<FName>& BlackboardNativeTags
) {}

void USingularisCombine::ReactionRevert_Implementation(
	const FSingularisCombineContext& Context,
	const FGameplayTagContainer& BlackboardGameplayTags,
	const TArray<FName>& BlackboardNativeTags
) {}

void USingularisCombine::SustainReaction_Implementation(
	const FSingularisCombineContext& Context,
	const FGameplayTagContainer& BlackboardGameplayTags,
	const TArray<FName>& BlackboardNativeTags,
	float DeltaTime
) {}
