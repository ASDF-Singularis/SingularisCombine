# 化合中层适配实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 为化合组件增加声明式依赖注入、事件驱动评估与瞬态负荷机制，解决中层使用时的查询样板、外部入参缺失与评估时机痛点。

**Architecture:** 不新增中层基类，改动集中在现有三个类。Context 与 Payload 分离（参考 CHANT `execute(ctx, payload)` 模式）；声明式依赖注入为主、辅助函数兜底；`TriggerEvaluate(Payload)` 驱动事件评估，周期轮询作为兜底。

**Tech Stack:** Unreal Engine 5.8 / C++ / GameplayTags / FInstancedStruct（CoreUObject 内置）

**Spec:** `Plugins/SingularisCombine/Docs/2026-08-22-combine-mid-layer-adaptation-design.md`

## Global Constraints

- 引擎版本：UE 5.8，引擎路径 `C:/Program Files/Epic Games/UE_5.8`
- `FInstancedStruct` 已内置在 `CoreUObject` 模块中，include 路径 `<StructUtils/InstancedStruct.h>`，Build.cs 无需新增依赖
- 蓝图支持由 `BlueprintInstancedStructLibrary`（Engine 模块）提供，原生可用
- 项目无 LSP，化合策略在蓝图实现，验证依赖编译通过
- 遵循 singularis-skeleton 的 region 组织风格与 coding-standards 的现代 C++ 规范
- 不提交 Git（用户指令）

## 编译验证命令

每个任务完成后执行编译验证：

```bash
"C:/Program Files/Epic Games/UE_5.8/Engine/Build/BatchFiles/Build.bat" VehicleTourEditor Win64 Development -Project="D:/UnrealProjects/VehicleTour/VehicleTour.uproject" -WaitMutex
```

预期：编译成功无错误。编译耗时较长（数分钟），属正常现象。

---

## Task 1: 新建瞬态负荷 USTRUCT

**Files:**
- Create: `Plugins/SingularisCombine/Source/SingularisCombine/Public/Types/SingularisCombineTransientPayload.h`

**Interfaces:**
- Consumes: 无
- Produces: `FSingularisCombineTransientPayload` USTRUCT（含 `EventTag: FGameplayTag` + `EventData: FInstancedStruct`）

- [ ] **Step 1: 创建 Payload 头文件**

创建 `Plugins/SingularisCombine/Source/SingularisCombine/Public/Types/SingularisCombineTransientPayload.h`：

```cpp
#pragma once

#include <CoreMinimal.h>
#include <GameplayTagContainer.h>
#include <StructUtils/InstancedStruct.h>

#include "SingularisCombinePayload.generated.h"

/**
 * 引力奇点化合瞬态负荷
 * 承载事件驱动评估的瞬态载荷：事件标识 tag 与结构化事件数据。
 * 周期轮询触发时为空，TriggerEvaluate 触发时携带事件载荷。
 * 参考设计：Context 与 Payload 分离，Payload 仅在本次评估有效。
 */
USTRUCT(BlueprintType)
struct SINGULARISCOMBINE_API FSingularisCombineTransientPayload
{
	GENERATED_BODY()

	/** 事件标识 tag，可空（语义为"无具体事件，仅标签变更"） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FGameplayTag EventTag;

	/** 结构化事件数据，强类型任意 struct，可空 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FInstancedStruct EventData;
};
```

- [ ] **Step 2: 编译验证**

运行编译命令。预期：编译成功。`FInstancedStruct` 的 `generated.h` 与 UPROPERTY 反射由 UHT 处理，无需额外配置。

---

## Task 2: SPI 签名扩展（Context + Payload）

本任务同步更新策略基类签名与所有调用方，保证编译通过。所有 Payload 暂传空值（`FSingularisCombineTransientPayload{}`），行为不变——事件驱动在 Task 4 实现。

**Files:**
- Modify: `Plugins/SingularisCombine/Source/SingularisCombine/Public/Objects/SingularisCombineBase.h`
- Modify: `Plugins/SingularisCombine/Source/SingularisCombine/Private/Objects/SingularisCombineBase.cpp`
- Modify: `Plugins/SingularisCombine/Source/SingularisCombine/Private/Components/SingularisCombineComponent.cpp`

**Interfaces:**
- Consumes: `FSingularisCombineTransientPayload`（Task 1 产生）
- Produces: 四方法新签名 `CanReaction(Context, Payload, Tags, NativeTags)` / `Reaction(Context, Payload, Tags, NativeTags)` / `ReactionRevert(Context, Payload, Tags, NativeTags)` / `SustainReaction(Context, Payload, Tags, NativeTags, DeltaTime)`

- [ ] **Step 1: 修改 SingularisCombineBase.h — 添加 include**

在现有 include 块末尾（`#include "SingularisCombineBase.generated.h"` 之前）添加 Payload include：

```cpp
#include "Types/SingularisCombineTransientPayload.h"
```

改动后 include 块完整形态：
```cpp
#include <CoreMinimal.h>
#include <GameplayTagContainer.h>
#include <UObject/Object.h>

#include "Types/SingularisCombineType.h"
#include "Types/SingularisCombineTransientPayload.h"
#include "SingularisCombineBase.generated.h"
```

- [ ] **Step 2: 修改 SingularisCombineBase.h — 四方法签名**

修改 `#pragma region SPI` 中的四个方法签名，新增 `Payload` 形参（`CanReaction` 额外新增 `Context` 形参）。

`CanReaction` 改动前：
```cpp
	bool CanReaction(
		const FGameplayTagContainer& BlackboardGameplayTags,
		const TArray<FName>& BlackboardNativeTags
	) const;
```

`CanReaction` 改动后：
```cpp
	bool CanReaction(
		const FSingularisCombineContext& Context,
		const FSingularisCombineTransientPayload& Payload,
		const FGameplayTagContainer& BlackboardGameplayTags,
		const TArray<FName>& BlackboardNativeTags
	) const;
```

`Reaction` 改动前：
```cpp
	void Reaction(
		const FSingularisCombineContext& Context,
		const FGameplayTagContainer& BlackboardGameplayTags,
		const TArray<FName>& BlackboardNativeTags
	);
```

`Reaction` 改动后：
```cpp
	void Reaction(
		const FSingularisCombineContext& Context,
		const FSingularisCombineTransientPayload& Payload,
		const FGameplayTagContainer& BlackboardGameplayTags,
		const TArray<FName>& BlackboardNativeTags
	);
```

`ReactionRevert` 改动前：
```cpp
	void ReactionRevert(
		const FSingularisCombineContext& Context,
		const FGameplayTagContainer& BlackboardGameplayTags,
		const TArray<FName>& BlackboardNativeTags
	);
```

`ReactionRevert` 改动后：
```cpp
	void ReactionRevert(
		const FSingularisCombineContext& Context,
		const FSingularisCombineTransientPayload& Payload,
		const FGameplayTagContainer& BlackboardGameplayTags,
		const TArray<FName>& BlackboardNativeTags
	);
```

`SustainReaction` 改动前：
```cpp
	void SustainReaction(
		const FSingularisCombineContext& Context,
		const FGameplayTagContainer& BlackboardGameplayTags,
		const TArray<FName>& BlackboardNativeTags,
		float DeltaTime
	);
```

`SustainReaction` 改动后：
```cpp
	void SustainReaction(
		const FSingularisCombineContext& Context,
		const FSingularisCombineTransientPayload& Payload,
		const FGameplayTagContainer& BlackboardGameplayTags,
		const TArray<FName>& BlackboardNativeTags,
		float DeltaTime
	);
```

- [ ] **Step 3: 修改 SingularisCombineBase.cpp — 添加 include**

在现有 include 块中添加 Payload include：

```cpp
#include "Types/SingularisCombineTransientPayload.h"
```

改动后 include 块完整形态：
```cpp
#include "Objects/SingularisCombineBase.h"

#include <Engine/NetDriver.h>
#include <GameFramework/Actor.h>
#include <Net/UnrealNetwork.h>

#include "Types/SingularisCombineTransientPayload.h"
```

- [ ] **Step 4: 修改 SingularisCombineBase.cpp — 四 _Implementation 签名**

同步四个 `_Implementation` 的签名与 .h 一致。

`CanReaction_Implementation` 改动后：
```cpp
bool USingularisCombine::CanReaction_Implementation(
	const FSingularisCombineContext& Context,
	const FSingularisCombineTransientPayload& Payload,
	const FGameplayTagContainer& BlackboardGameplayTags,
	const TArray<FName>& BlackboardNativeTags
) const
{
	return true;
}
```

`Reaction_Implementation` 改动后：
```cpp
void USingularisCombine::Reaction_Implementation(
	const FSingularisCombineContext& Context,
	const FSingularisCombineTransientPayload& Payload,
	const FGameplayTagContainer& BlackboardGameplayTags,
	const TArray<FName>& BlackboardNativeTags
) {}
```

`ReactionRevert_Implementation` 改动后：
```cpp
void USingularisCombine::ReactionRevert_Implementation(
	const FSingularisCombineContext& Context,
	const FSingularisCombineTransientPayload& Payload,
	const FGameplayTagContainer& BlackboardGameplayTags,
	const TArray<FName>& BlackboardNativeTags
) {}
```

`SustainReaction_Implementation` 改动后：
```cpp
void USingularisCombine::SustainReaction_Implementation(
	const FSingularisCombineContext& Context,
	const FSingularisCombineTransientPayload& Payload,
	const FGameplayTagContainer& BlackboardGameplayTags,
	const TArray<FName>& BlackboardNativeTags,
	float DeltaTime
) {}
```

- [ ] **Step 5: 修改 SingularisCombineComponent.cpp — 添加 include**

在现有 include 块中添加 Payload include：

```cpp
#include "Types/SingularisCombineTransientPayload.h"
```

改动后 include 块完整形态：
```cpp
#include "Components/SingularisCombineComponent.h"

#include <GameplayTagAssetInterface.h>
#include <Engine/World.h>
#include <Net/UnrealNetwork.h>

#include "Objects/SingularisCombineBase.h"
#include "Types/SingularisCombineType.h"
#include "Types/SingularisCombineTransientPayload.h"
```

- [ ] **Step 6: 修改 SingularisCombineComponent.cpp — TickComponent 调用点**

`TickComponent` 中 `SustainReaction` 调用（约第 109 行），新增空 Payload 形参。

改动前：
```cpp
		Combine->SustainReaction(Context, BlackboardTags, NativeBlackboardTags, DeltaTime);
```

改动后：
```cpp
		Combine->SustainReaction(Context, FSingularisCombineTransientPayload{}, BlackboardTags, NativeBlackboardTags, DeltaTime);
```

- [ ] **Step 7: 修改 SingularisCombineComponent.cpp — SetPipeline 调用点**

`SetPipeline` 中 `ReactionRevert` 调用（约第 128 行），新增空 Payload 形参。

改动前：
```cpp
				Combine->ReactionRevert(Context, BlackboardTags, NativeBlackboardTags);
```

改动后：
```cpp
				Combine->ReactionRevert(Context, FSingularisCombineTransientPayload{}, BlackboardTags, NativeBlackboardTags);
```

- [ ] **Step 8: 修改 SingularisCombineComponent.cpp — EvaluatePipeline 调用点**

`EvaluatePipeline` 中三个 SPI 调用（约第 191、195、203 行），新增 `Context` 与空 `Payload` 形参。

`CanReaction` 调用改动前：
```cpp
		if (Combine->CanReaction(BlackboardTags, NativeBlackboardTags))
```

`CanReaction` 调用改动后：
```cpp
		if (Combine->CanReaction(Context, FSingularisCombineTransientPayload{}, BlackboardTags, NativeBlackboardTags))
```

`Reaction` 调用改动前：
```cpp
				Combine->Reaction(Context, BlackboardTags, NativeBlackboardTags);
```

`Reaction` 调用改动后：
```cpp
				Combine->Reaction(Context, FSingularisCombineTransientPayload{}, BlackboardTags, NativeBlackboardTags);
```

`ReactionRevert` 调用改动前：
```cpp
				Combine->ReactionRevert(Context, BlackboardTags, NativeBlackboardTags);
```

`ReactionRevert` 调用改动后：
```cpp
				Combine->ReactionRevert(Context, FSingularisCombineTransientPayload{}, BlackboardTags, NativeBlackboardTags);
```

- [ ] **Step 9: 编译验证**

运行编译命令。预期：编译成功，行为不变（所有 Payload 传空值）。

---

## Task 3: 声明式依赖注入机制

为策略基类增加依赖声明配置、运行时缓存、依赖解析与查询函数。化合组件在评估前调用依赖解析。

**Files:**
- Modify: `Plugins/SingularisCombine/Source/SingularisCombine/Public/Objects/SingularisCombineBase.h`
- Modify: `Plugins/SingularisCombine/Source/SingularisCombine/Private/Objects/SingularisCombineBase.cpp`
- Modify: `Plugins/SingularisCombine/Source/SingularisCombine/Private/Components/SingularisCombineComponent.cpp`

**Interfaces:**
- Consumes: 无
- Produces: `USingularisCombine::ComponentDependencies`（UPROPERTY 配置）、`CachedDependencies`（Transient 缓存）、`ResolveDependencies()`（私有，friend 授权组件调用）、`GetDependency(Class)`（BlueprintPure）、`GetAvatarComponent(Class)`（BlueprintPure）

- [ ] **Step 1: 修改 SingularisCombineBase.h — 前向声明与 friend**

在 `#include` 块之后、`UCLASS` 之前添加 `UActorComponent` 前向声明：

```cpp
class UActorComponent;
```

在 `GENERATED_BODY()` 之后添加 friend 声明，授权化合组件调用私有方法：

```cpp
	GENERATED_BODY()

	friend class USingularisCombineComponent;
```

- [ ] **Step 2: 修改 SingularisCombineBase.h — 依赖配置与缓存字段**

在现有 `#pragma region Parameter` 之后、`#pragma region UObject Interface` 之前，新建 `#pragma region 依赖`：

```cpp
// 声明式依赖配置与运行时缓存
#pragma region 依赖

	/**
	 * 策略声明的依赖组件类型
	 * 评估前由化合组件预解析，缓存到 CachedDependencies 供策略快速访问
	 */
	UPROPERTY(
		EditDefaultsOnly,
		BlueprintReadOnly,
		Category = "SingularisCombine|引力奇点化合|依赖",
		meta = (DisplayName = "组件依赖")
	)
	TArray<TSubclassOf<UActorComponent>> ComponentDependencies;

	/** 运行时缓存：每次评估前由 ResolveDependencies 刷新 */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "SingularisCombine|引力奇点化合|依赖")
	TMap<TSubclassOf<UActorComponent>, TWeakObjectPtr<UActorComponent>> CachedDependencies;

	/**
	 * 获取预缓存的依赖组件
	 * @param ComponentClass  声明在 ComponentDependencies 中的组件类型
	 * @return 预缓存的组件引用，未找到或未声明时返回 nullptr
	 */
	UFUNCTION(
		BlueprintPure,
		Category = "SingularisCombine|引力奇点化合|依赖",
		meta = (DisplayName = "GetDependency")
	)
	UActorComponent* GetDependency(TSubclassOf<UActorComponent> ComponentClass) const;

	/**
	 * 兜底查询：沿 Outer 链即时查找 Avatar 上的组件
	 * 用于未在 ComponentDependencies 中声明的临时/动态查询
	 * @param ComponentClass  目标组件类型
	 * @return 查找到的组件引用，未找到时返回 nullptr
	 */
	UFUNCTION(
		BlueprintPure,
		Category = "SingularisCombine|引力奇点化合|依赖",
		meta = (DisplayName = "GetAvatarComponent")
	)
	UActorComponent* GetAvatarComponent(TSubclassOf<UActorComponent> ComponentClass);

#pragma endregion
```

- [ ] **Step 3: 修改 SingularisCombineBase.h — ResolveDependencies 声明**

在文件末尾（`#pragma region SPI` 之后）添加 private 内部函数区域：

```cpp
private:
#pragma region Internal Function

	/** 预解析声明的组件依赖，刷新 CachedDependencies 缓存 */
	void ResolveDependencies();

#pragma endregion
```

- [ ] **Step 4: 修改 SingularisCombineBase.cpp — 添加 include**

在 include 块中添加化合组件头文件（`Cast<USingularisCombineComponent>` 需要完整定义）：

```cpp
#include "Components/SingularisCombineComponent.h"
```

改动后 include 块完整形态：
```cpp
#include "Objects/SingularisCombineBase.h"

#include <Engine/NetDriver.h>
#include <GameFramework/Actor.h>
#include <Net/UnrealNetwork.h>

#include "Components/SingularisCombineComponent.h"
#include "Types/SingularisCombineTransientPayload.h"
```

- [ ] **Step 5: 修改 SingularisCombineBase.cpp — 实现三个函数**

在文件末尾（`OnRep_IsActive_Implementation` 之后）添加三个函数实现：

```cpp
void USingularisCombine::ResolveDependencies()
{
	CachedDependencies.Reset();

	if (ComponentDependencies.IsEmpty())
	{
		return;
	}

	// 1) 沿 Outer 链查找化合组件 → OwnerActor
	USingularisCombineComponent* const CombineComponent = Cast<USingularisCombineComponent>(GetOuter());
	if (!CombineComponent)
	{
		return;
	}

	AActor* const OwnerActor = CombineComponent->GetOwner();
	if (!OwnerActor)
	{
		return;
	}

	// 2) 遍历声明的依赖类型，在 OwnerActor 上查找并缓存
	for (const TSubclassOf<UActorComponent>& DepClass : ComponentDependencies)
	{
		if (!DepClass)
		{
			continue;
		}

		UActorComponent* const Found = OwnerActor->GetComponentByClass(DepClass);
		if (Found)
		{
			CachedDependencies.Add(DepClass, Found);
		}
	}
}

UActorComponent* USingularisCombine::GetDependency(TSubclassOf<UActorComponent> ComponentClass) const
{
	if (!ComponentClass)
	{
		return nullptr;
	}

	// 1) 从预解析缓存中读取
	const TWeakObjectPtr<UActorComponent>* const Found = CachedDependencies.Find(ComponentClass);
	if (Found && Found->IsValid())
	{
		return Found->Get();
	}

	return nullptr;
}

UActorComponent* USingularisCombine::GetAvatarComponent(TSubclassOf<UActorComponent> ComponentClass)
{
	if (!ComponentClass)
	{
		return nullptr;
	}

	// 1) 沿 Outer 链查找化合组件 → OwnerActor
	USingularisCombineComponent* const CombineComponent = Cast<USingularisCombineComponent>(GetOuter());
	if (!CombineComponent)
	{
		return nullptr;
	}

	AActor* const OwnerActor = CombineComponent->GetOwner();
	if (!OwnerActor)
	{
		return nullptr;
	}

	// 2) 在 OwnerActor 上即时查找目标组件
	return OwnerActor->GetComponentByClass(ComponentClass);
}
```

- [ ] **Step 6: 修改 SingularisCombineComponent.cpp — EvaluatePipeline 增加依赖预解析**

在 `EvaluatePipeline` 中 `CollectAllTags()` 调用之后、标签变更检测之前，插入依赖解析循环。

现有代码：
```cpp
	// 1) 所有端：刷新全局黑板（GameplayTags + 原生 FName 标签）；仅在标签集合实际变更时广播
	CollectAllTags();

	const bool bGameplayTagsChanged = BlackboardTags != PreviousBlackboardTags;
```

改动后：
```cpp
	// 1) 所有端：刷新全局黑板（GameplayTags + 原生 FName 标签）；仅在标签集合实际变更时广播
	CollectAllTags();

	// 2) 所有端：预解析策略声明的组件依赖，刷新策略内部缓存
	for (const FSingularisCombineEntry& Entry : CombinePipeline.Combines)
	{
		if (USingularisCombine* const Combine = Entry.Combine)
		{
			Combine->ResolveDependencies();
		}
	}

	const bool bGameplayTagsChanged = BlackboardTags != PreviousBlackboardTags;
```

- [ ] **Step 7: 编译验证**

运行编译命令。预期：编译成功。依赖注入字段与函数就绪，但因策略尚未配置 `ComponentDependencies`，行为不变。

---

## Task 4: 事件驱动评估

实现 `TriggerEvaluate` API 与 Payload 贯穿机制。评估时从 `PendingPayload` 读取事件载荷并传给策略 SPI。

**Files:**
- Modify: `Plugins/SingularisCombine/Source/SingularisCombine/Public/Components/SingularisCombineComponent.h`
- Modify: `Plugins/SingularisCombine/Source/SingularisCombine/Private/Components/SingularisCombineComponent.cpp`

**Interfaces:**
- Consumes: `FSingularisCombineTransientPayload`（Task 1）、新 SPI 签名（Task 2）
- Produces: `USingularisCombineComponent::TriggerEvaluate(Payload)`（BlueprintCallable, BlueprintAuthorityOnly）、`PendingPayload` 内部变量

- [ ] **Step 1: 修改 SingularisCombineComponent.h — 添加 include**

在 include 块中添加 Payload include：

```cpp
#include "Types/SingularisCombineTransientPayload.h"
```

改动后 include 块完整形态：
```cpp
#include <CoreMinimal.h>
#include <GameplayTagContainer.h>
#include <TimerManager.h>
#include <Components/ActorComponent.h>

#include "Types/SingularisCombineComponentType.h"
#include "Types/SingularisCombineTransientPayload.h"
#include "SingularisCombineComponent.generated.h"
```

- [ ] **Step 2: 修改 SingularisCombineComponent.h — TriggerEvaluate 声明**

在 `#pragma region API` 中，`EvaluatePipeline` 声明之后添加 `TriggerEvaluate`：

```cpp
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
```

- [ ] **Step 3: 修改 SingularisCombineComponent.h — PendingPayload 内部变量**

在 `#pragma region Internal Variable` 中添加 `PendingPayload` 变量：

```cpp
	/** TriggerEvaluate 写入的待处理事件载荷，EvaluatePipeline 读取后由 TriggerEvaluate 清空 */
	FSingularisCombineTransientPayload PendingPayload{};
```

- [ ] **Step 4: 修改 SingularisCombineComponent.cpp — 实现 TriggerEvaluate**

在 `EvaluatePipeline` 实现之前添加 `TriggerEvaluate` 实现：

```cpp
void USingularisCombineComponent::TriggerEvaluate(const FSingularisCombineTransientPayload& Payload)
{
	PendingPayload = Payload;
	EvaluatePipeline();
	PendingPayload = FSingularisCombineTransientPayload{};
}
```

- [ ] **Step 5: 修改 SingularisCombineComponent.cpp — EvaluatePipeline 读取 PendingPayload**

在 `EvaluatePipeline` 的服务端权威部分，构建 Context 之后、策略评估循环之前，读取 `PendingPayload` 为局部快照。

现有代码（策略评估循环前）：
```cpp
	FSingularisCombineContext Context;
	Context.Instigator = Cast<AActor>(OwnerActor->GetInstigator());
	Context.Avatar = OwnerActor;
	Context.Target = OwnerActor;
	Context.CombineComponent = this;

	for (FSingularisCombineEntry& Entry : CombinePipeline.Combines)
```

改动后：
```cpp
	FSingularisCombineContext Context;
	Context.Instigator = Cast<AActor>(OwnerActor->GetInstigator());
	Context.Avatar = OwnerActor;
	Context.Target = OwnerActor;
	Context.CombineComponent = this;

	// 3) 读取待处理事件载荷（TriggerEvaluate 写入，周期轮询时为空）
	const FSingularisCombineTransientPayload Payload = PendingPayload;

	for (FSingularisCombineEntry& Entry : CombinePipeline.Combines)
```

- [ ] **Step 6: 修改 SingularisCombineComponent.cpp — 策略评估循环使用 Payload**

将策略评估循环中三处 `FSingularisCombineTransientPayload{}` 替换为局部变量 `Payload`。

`CanReaction` 调用改动前（Task 2 状态）：
```cpp
		if (Combine->CanReaction(Context, FSingularisCombineTransientPayload{}, BlackboardTags, NativeBlackboardTags))
```

`CanReaction` 调用改动后：
```cpp
		if (Combine->CanReaction(Context, Payload, BlackboardTags, NativeBlackboardTags))
```

`Reaction` 调用改动前（Task 2 状态）：
```cpp
				Combine->Reaction(Context, FSingularisCombineTransientPayload{}, BlackboardTags, NativeBlackboardTags);
```

`Reaction` 调用改动后：
```cpp
				Combine->Reaction(Context, Payload, BlackboardTags, NativeBlackboardTags);
```

`ReactionRevert` 调用改动前（Task 2 状态）：
```cpp
				Combine->ReactionRevert(Context, FSingularisCombineTransientPayload{}, BlackboardTags, NativeBlackboardTags);
```

`ReactionRevert` 调用改动后：
```cpp
				Combine->ReactionRevert(Context, Payload, BlackboardTags, NativeBlackboardTags);
```

注意：`TickComponent` 与 `SetPipeline` 中的调用保持传 `FSingularisCombineTransientPayload{}`（非事件驱动，无 Payload 语义）。

- [ ] **Step 7: 编译验证**

运行编译命令。预期：编译成功。事件驱动评估就绪，`TriggerEvaluate` 可被蓝图调用，Payload 贯穿策略评估。

---

## 实现后手动验证（可选）

编译通过后，在编辑器中验证功能：

1. **依赖注入**：打开测试蓝图 `BP_TestSingularisCombine`，在某个策略的"组件依赖"中配置一个组件类型，在 `Reaction` 中调用 `GetDependency` + Cast，验证返回正确引用
2. **事件触发**：在蓝图中调用 `TriggerEvaluate`，传入带 `EventTag` 的 Payload，在策略 `CanReaction` 中读取 `Payload.EventTag`，验证非空
3. **FInstancedStruct**：在 `TriggerEvaluate` 调用前用 `Make Instanced Struct` 节点构造结构化数据，在策略中用 `Get Instanced Struct Value` 节点读取，验证 Valid 分支
4. **回归**：验证现有测试策略（未配置依赖、不读 Payload）行为不变
