# 依赖注入扩展实现计划：结构化声明 + Instigator/Target 支持 + 强约束前置预检

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将声明式依赖注入从仅支持 Avatar 扩展到 Instigator/Avatar/Target 三者；把依赖缓存与解析从策略层移到组件层；增加强约束前置预检——声明依赖未全部满足时不进入 CanReaction，直接走回滚路径。

**Architecture:** 新增作用域枚举 `ESingularisCombineDependencyScope`（Instigator/Avatar/Target）；策略层 `ComponentDependencies` 改为 `TMap<Scope, TArray<Subclass>>` 结构化声明；组件层维护 `TMap<Actor, TMap<Class, WeakPtr>>` 嵌套缓存并提供 `ResolveDependencies(Context)` + `GetDependency(Actor, Class)` + `AreDependenciesSatisfied(Strategy)`；`EvaluatePipeline` 策略循环增加前置预检门控。

**Tech Stack:** Unreal Engine 5.8 / C++ / GameplayTags

**Spec:** `Plugins/SingularisCombine/Docs/2026-08-22-combine-mid-layer-adaptation-design.md`（需同步更新）

## Global Constraints

- 引擎版本：UE 5.8，引擎路径 `C:/Program Files/Epic Games/UE_5.8`
- 遵循 singularis-skeleton 的 region 组织风格与 coding-standards 的现代 C++ 规范
- 不提交 Git（用户指令）
- 编译验证用 `-NoLiveCoding` 标志绕过运行中编辑器
- 空声明 = 无条件满足（现有未配置依赖的策略行为完全不变）
- 全有才执行：声明依赖任一缺失 → AreDependenciesSatisfied 返回 false → 不进入 CanReaction
- `AreDependenciesSatisfied` 是 `CanReaction` 的前置条件，非平级判断

## 编译验证命令

```bash
"C:/Program Files/Epic Games/UE_5.8/Engine/Build/BatchFiles/Build.bat" VehicleTourEditor Win64 Development -Project="D:/UnrealProjects/VehicleTour/VehicleTour.uproject" -WaitMutex -NoLiveCoding
```

预期：编译成功无错误。Set timeout_ms to 600000 (10 minutes)。

## 当前代码状态（基线）

- `USingularisCombine` 中：
  - `ComponentDependencies` 是 `TArray<TSubclassOf<UActorComponent>>`（需改为 TMap 结构）
  - `CachedDependencies` 是策略私有的 `TMap<TSubclassOf<UActorComponent>, TWeakObjectPtr<UActorComponent>>`（需移除，缓存移到组件层）
  - `ResolveDependencies()` 是策略私有方法（需移除，逻辑移到组件层）
  - `friend class USingularisCombineComponent`（需移除，组件不再调用策略私有方法）
  - `GetDependency(TSubclassOf<UActorComponent>)` 单参数（需改为 Actor + Class）
  - `GetAvatarComponent(TSubclassOf<UActorComponent>)` 保留（无 Context 便捷）
- `USingularisCombineComponent` 中：
  - `EvaluatePipeline` 在 CollectAllTags 后调用 `Combine->ResolveDependencies()`（需移除，改为 Context 构建后调用组件层 ResolveDependencies）
  - 无依赖缓存与查询机制（需新增）

---

## Task 1: 新建依赖作用域枚举

**Files:**
- Create: `Plugins/SingularisCombine/Source/SingularisCombine/Public/Types/SingularisCombineDependencyScope.h`

**Interfaces:**
- Consumes: 无
- Produces: `ESingularisCombineDependencyScope` 枚举（Instigator / Avatar / Target）

- [ ] **Step 1: 创建枚举头文件**

创建 `Plugins/SingularisCombine/Source/SingularisCombine/Public/Types/SingularisCombineDependencyScope.h`：

```cpp
#pragma once

#include <CoreMinimal.h>

#include "SingularisCombineDependencyScope.generated.h"

/**
 * 引力奇点化合依赖作用域
 * 标识声明式依赖归属的 Context Actor，用于结构化配置策略的组件依赖。
 */
UENUM(BlueprintType)
enum class ESingularisCombineDependencyScope : uint8
{
	/** 化合操作的发起者（Context.Instigator） */
	Instigator UMETA(DisplayName = "Instigator"),

	/** 化合操作的替身（Context.Avatar，通常为挂载化合组件的 Actor） */
	Avatar UMETA(DisplayName = "Avatar"),

	/** 化合操作的目标（Context.Target） */
	Target UMETA(DisplayName = "Target"),
};
```

- [ ] **Step 2: 编译验证**

运行编译命令。预期：编译成功。

---

## Task 2: 策略层重构——结构化声明 + 移除缓存与解析

**Files:**
- Modify: `Plugins/SingularisCombine/Source/SingularisCombine/Public/Objects/SingularisCombineBase.h`
- Modify: `Plugins/SingularisCombine/Source/SingularisCombine/Private/Objects/SingularisCombineBase.cpp`

**Interfaces:**
- Consumes: `ESingularisCombineDependencyScope`（Task 1 产生）
- Produces: `ComponentDependencies` 新结构（TMap<Scope, TArray<Subclass>>）；`GetDependency(AActor*, Class)` 新签名；移除 `CachedDependencies`/`ResolveDependencies`/`friend`

- [ ] **Step 1: 修改 SingularisCombineBase.h — 添加 include**

在 include 块中添加 Scope 枚举 include，置于 TransientPayload 与 Type 之间（保持字母序）：

```cpp
#include "Types/SingularisCombineDependencyScope.h"
#include "Types/SingularisCombineTransientPayload.h"
#include "Types/SingularisCombineType.h"
```

- [ ] **Step 2: 修改 SingularisCombineBase.h — 移除 friend 声明**

删除 `GENERATED_BODY()` 之后的 `friend class USingularisCombineComponent;` 行。

删除前：
```cpp
	GENERATED_BODY()

	friend class USingularisCombineComponent;

public:
```

删除后：
```cpp
	GENERATED_BODY()

public:
```

- [ ] **Step 3: 修改 SingularisCombineBase.h — ComponentDependencies 改为 TMap 结构**

在 `#pragma region Parameter` 中，将 `ComponentDependencies` 从 `TArray<TSubclassOf<UActorComponent>>` 改为 `TMap<ESingularisCombineDependencyScope, TArray<TSubclassOf<UActorComponent>>>`。

改动前：
```cpp
	/**
	 * 策略声明的依赖组件类型
	 * 评估前由化合组件预解析，缓存到 CachedDependencies 供策略快速访问
	 */
	UPROPERTY(
		EditDefaultsOnly,
		BlueprintReadOnly,
		Category = "SingularisCombine|引力奇点化合|参数",
		meta = (DisplayName = "组件依赖")
	)
	TArray<TSubclassOf<UActorComponent>> ComponentDependencies{};
```

改动后：
```cpp
	/**
	 * 策略声明的组件依赖
	 * 按作用域（Instigator/Avatar/Target）结构化声明；评估前由化合组件预解析并缓存。
	 * 声明未全部满足时，不进入 CanReaction，直接走回滚路径。
	 * 空声明视为无条件满足。
	 */
	UPROPERTY(
		EditDefaultsOnly,
		BlueprintReadOnly,
		Category = "SingularisCombine|引力奇点化合|参数",
		meta = (DisplayName = "组件依赖")
	)
	TMap<ESingularisCombineDependencyScope, TArray<TSubclassOf<UActorComponent>>> ComponentDependencies{};
```

- [ ] **Step 4: 修改 SingularisCombineBase.h — 移除 CachedDependencies 与 Internal Variable region**

删除整个 private `#pragma region Internal Variable` 块（包含 `CachedDependencies`）。删除后 private 区域消失，文件从 Parameter region 直接进入下一个 public region。

删除前：
```cpp
#pragma endregion

private:
#pragma region Internal Variable

	/** 运行时缓存：每次评估前由 ResolveDependencies 刷新 */
	UPROPERTY(Transient)
	TMap<TSubclassOf<UActorComponent>, TWeakObjectPtr<UActorComponent>> CachedDependencies{};

#pragma endregion

public:
#pragma region UObject Interface
```

删除后：
```cpp
#pragma endregion

public:
#pragma region UObject Interface
```

- [ ] **Step 5: 修改 SingularisCombineBase.h — GetDependency 改签名**

在 `#pragma region State` 中，将 `GetDependency` 签名改为接收 `AActor*` + `TSubclassOf<UActorComponent>`。

改动前：
```cpp
	/**
	 * 获取预缓存的依赖组件
	 * @param ComponentClass  声明在 ComponentDependencies 中的组件类型
	 * @return 预缓存的组件引用，未找到或未声明时返回 nullptr
	 */
	UFUNCTION(
		BlueprintPure,
		Category = "SingularisCombine|引力奇点化合|State",
		meta = (DisplayName = "GetDependency")
	)
	UActorComponent* GetDependency(TSubclassOf<UActorComponent> ComponentClass) const;
```

改动后：
```cpp
	/**
	 * 获取预缓存的依赖组件
	 * 内部委托化合组件的缓存查询；必须在 ResolveDependencies 之后调用。
	 * @param Actor           目标 Actor（通常来自 Context.Instigator/Avatar/Target）
	 * @param ComponentClass  声明在 ComponentDependencies 中的组件类型
	 * @return 预缓存的组件引用，未找到或未声明时返回 nullptr
	 */
	UFUNCTION(
		BlueprintPure,
		Category = "SingularisCombine|引力奇点化合|State",
		meta = (DisplayName = "GetDependency")
	)
	UActorComponent* GetDependency(AActor* Actor, TSubclassOf<UActorComponent> ComponentClass) const;
```

- [ ] **Step 6: 修改 SingularisCombineBase.h — 移除 ResolveDependencies 声明**

删除文件末尾的 private `#pragma region Internal Function` 块（包含 `ResolveDependencies` 声明）。

删除前：
```cpp
#pragma endregion

private:
#pragma region Internal Function

	/** 预解析声明的组件依赖，刷新 CachedDependencies 缓存 */
	void ResolveDependencies();

#pragma endregion
};
```

删除后：
```cpp
#pragma endregion
};
```

- [ ] **Step 7: 修改 SingularisCombineBase.h — 前向声明 AActor**

在 `class UActorComponent;` 之前添加 `class AActor;` 前向声明（Step 5 的 GetDependency 签名需要）：

```cpp
class AActor;
class UActorComponent;
```

- [ ] **Step 8: 修改 SingularisCombineBase.cpp — 添加 include**

在 include 块中添加化合组件头文件（GetDependency 实现需委托组件层，需要完整定义）：

```cpp
#include "Components/SingularisCombineComponent.h"
```

（注：此 include 已存在，确认保留即可。）

- [ ] **Step 9: 修改 SingularisCombineBase.cpp — 移除 ResolveDependencies 实现**

删除 `ResolveDependencies` 函数实现。

删除前：
```cpp
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
```

删除后此函数不再存在。

- [ ] **Step 10: 修改 SingularisCombineBase.cpp — GetDependency 改实现**

将 `GetDependency` 实现改为委托组件层查询。

改动前：
```cpp
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
```

改动后：
```cpp
UActorComponent* USingularisCombine::GetDependency(AActor* Actor, TSubclassOf<UActorComponent> ComponentClass) const
{
	if (!Actor || !ComponentClass)
		return nullptr;

	// 1) 委托化合组件的缓存查询
	if (const USingularisCombineComponent* const CombineComponent = Cast<USingularisCombineComponent>(GetOuter()))
		return CombineComponent->GetDependency(Actor, ComponentClass);

	return nullptr;
}
```

- [ ] **Step 11: 编译验证**

运行编译命令。预期：编译失败——`USingularisCombineComponent` 还没有 `GetDependency(AActor*, Class)` 方法。这是预期的，因 Task 3 尚未实现组件层方法。

**注意**：此 Task 编译失败是中间状态。如果团队要求每个 Task 编译通过，可临时在 `GetDependency` 实现中 `return nullptr;` 跳过委托，Task 3 完成后再恢复委托。本计划采用此策略：Step 10 暂用占位实现，Task 3 Step 中恢复委托。

若采用占位策略，Step 10 改动后为：
```cpp
UActorComponent* USingularisCombine::GetDependency(AActor* Actor, TSubclassOf<UActorComponent> ComponentClass) const
{
	// 占位：Task 3 完成后恢复委托至 CombineComponent->GetDependency
	return nullptr;
}
```

采用占位策略后编译预期：成功。

- [ ] **Step 12: 编译验证（占位策略）**

若采用占位策略，运行编译命令。预期：编译成功。

---

## Task 3: 组件层新增依赖缓存与查询

**Files:**
- Modify: `Plugins/SingularisCombine/Source/SingularisCombine/Public/Components/SingularisCombineComponent.h`
- Modify: `Plugins/SingularisCombine/Source/SingularisCombine/Private/Components/SingularisCombineComponent.cpp`
- Modify: `Plugins/SingularisCombine/Source/SingularisCombine/Private/Objects/SingularisCombineBase.cpp`（恢复 GetDependency 委托）

**Interfaces:**
- Consumes: `ESingularisCombineDependencyScope`（Task 1）；`USingularisCombine::ComponentDependencies` 新结构（Task 2）
- Produces: `USingularisCombineComponent::ResolveDependencies(Context)`、`GetDependency(Actor, Class)`、`AreDependenciesSatisfied(Strategy)`、`CachedDependencies` 缓存

- [ ] **Step 1: 修改 SingularisCombineComponent.h — 添加 include**

在 include 块中添加依赖作用域枚举 include：

```cpp
#include "Types/SingularisCombineDependencyScope.h"
```

改动后 include 块完整形态：
```cpp
#include <CoreMinimal.h>
#include <GameplayTagContainer.h>
#include <TimerManager.h>
#include <Components/ActorComponent.h>

#include "Types/SingularisCombineComponentType.h"
#include "Types/SingularisCombineDependencyScope.h"
#include "Types/SingularisCombineTransientPayload.h"
#include "SingularisCombineComponent.generated.h"
```

- [ ] **Step 2: 修改 SingularisCombineComponent.h — 前向声明**

在 `#pragma region 委托签名` 之前添加前向声明：

```cpp
class USingularisCombine;
class UActorComponent;
```

- [ ] **Step 3: 修改 SingularisCombineComponent.h — 新增缓存成员**

在 `#pragma region Internal Variable` 中，在 `PendingPayload` 之后添加缓存成员：

```cpp
	/** TriggerEvaluate 写入的待处理事件载荷，EvaluatePipeline 读取后由 TriggerEvaluate 清空 */
	UPROPERTY(Transient)
	FSingularisCombineTransientPayload PendingPayload{};

	/**
	 * 依赖组件缓存：按 Actor 分组，每个 Actor 内按组件类型缓存
	 * 由 ResolveDependencies(Context) 每次评估前刷新
	 */
	TMap<TWeakObjectPtr<AActor>, TMap<TSubclassOf<UActorComponent>, TWeakObjectPtr<UActorComponent>>> CachedDependencies{};

#pragma endregion
```

- [ ] **Step 4: 修改 SingularisCombineComponent.h — 新增三个公开方法声明**

在 `#pragma region State` 中，在 `GetNativeBlackboardTags` 之后添加三个公开方法声明：

```cpp
	/** 获取当前全局黑板上收集的原生 FName 标签快照 */
	UFUNCTION(
		BlueprintPure,
		BlueprintCallable,
		Category = "SingularisCombine|引力奇点化合组件|State",
		meta = (DisplayName = "GetNativeBlackboardTags")
	)
	const TArray<FName>& GetNativeBlackboardTags() const { return NativeBlackboardTags; }

	/**
	 * 预解析管线中所有策略声明的组件依赖
	 * 收集所有策略的 ComponentDependencies 并集，对 Context 中三个 Actor 分别按声明类型查找并缓存。
	 * 必须在策略评估前调用；策略通过 GetDependency 读取缓存。
	 * @param Context  化合上下文，提供 Instigator/Avatar/Target 三个 Actor
	 */
	void ResolveDependencies(const FSingularisCombineContext& Context);

	/**
	 * 查询预缓存的依赖组件
	 * @param Actor           目标 Actor
	 * @param ComponentClass  组件类型
	 * @return 预缓存的组件引用，未找到时返回 nullptr
	 */
	UActorComponent* GetDependency(AActor* Actor, TSubclassOf<UActorComponent> ComponentClass) const;

	/**
	 * 检查策略声明的依赖是否全部满足
	 * 空声明返回 true（无条件满足）；任一声明缺失返回 false
	 * @param Strategy  目标策略
	 * @param Context   化合上下文，用于将作用域映射到 Actor
	 * @return 声明依赖是否全部命中缓存
	 */
	bool AreDependenciesSatisfied(const USingularisCombine* Strategy, const FSingularisCombineContext& Context) const;

#pragma endregion
```

注意：这三个方法不加 `UFUNCTION`——它们是 C++ 内部方法，不暴露蓝图。`ResolveDependencies` 和 `AreDependenciesSatisfied` 仅组件内部与策略基类调用；`GetDependency` 虽然策略基类会委托调用，但策略基类的 `GetDependency` 才是蓝图入口。

`AreDependenciesSatisfied` 必须接收 `Context`——作用域（Instigator/Avatar/Target）需通过 Context 映射到具体 Actor 才能查缓存。

- [ ] **Step 5: 修改 SingularisCombineComponent.cpp — 添加 include**

在 include 块中添加依赖作用域枚举 include：

```cpp
#include "Types/SingularisCombineDependencyScope.h"
```

改动后 include 块完整形态：
```cpp
#include "Components/SingularisCombineComponent.h"

#include <GameplayTagAssetInterface.h>
#include <Engine/World.h>

#include "Objects/SingularisCombineBase.h"
#include "Types/SingularisCombineComponentType.h"
#include "Types/SingularisCombineDependencyScope.h"
#include "Types/SingularisCombineTransientPayload.h"
```

- [ ] **Step 6: 修改 SingularisCombineComponent.cpp — 实现三个方法**

在文件末尾（`OnOwnerChildComponentConstructed` 之后）添加三个方法实现：

```cpp
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

		for (const auto& Pair : Combine->ComponentDependencies)
		{
			TSet<TSubclassOf<UActorComponent>>& TypeSet = AggregatedDeps.FindOrAdd(Pair.Key);
			for (const TSubclassOf<UActorComponent>& DepClass : Pair.Value)
			{
				if (DepClass)
					TypeSet.Add(DepClass);
			}
		}
	}

	if (AggregatedDeps.IsEmpty())
		return;

	// 2) 按作用域映射到 Context 中的 Actor，分别查找并缓存
	auto ResolveForActor = [this](AActor* Actor, const TSet<TSubclassOf<UActorComponent>>& TypeSet)
	{
		if (!Actor || TypeSet.IsEmpty())
			return;

		TMap<TSubclassOf<UActorComponent>, TWeakObjectPtr<UActorComponent>>& ActorCache = CachedDependencies.Add(Actor);
		for (const TSubclassOf<UActorComponent>& DepClass : TypeSet)
		{
			if (UActorComponent* const Found = Actor->GetComponentByClass(DepClass))
				ActorCache.Add(DepClass, Found);
		}
	};

	if (const TSet<TSubclassOf<UActorComponent>>* InstigatorDeps = AggregatedDeps.Find(ESingularisCombineDependencyScope::Instigator))
		ResolveForActor(Context.Instigator, *InstigatorDeps);

	if (const TSet<TSubclassOf<UActorComponent>>* AvatarDeps = AggregatedDeps.Find(ESingularisCombineDependencyScope::Avatar))
		ResolveForActor(Context.Avatar, *AvatarDeps);

	if (const TSet<TSubclassOf<UActorComponent>>* TargetDeps = AggregatedDeps.Find(ESingularisCombineDependencyScope::Target))
		ResolveForActor(Context.Target, *TargetDeps);
}

UActorComponent* USingularisCombineComponent::GetDependency(AActor* Actor, TSubclassOf<UActorComponent> ComponentClass) const
{
	if (!Actor || !ComponentClass)
		return nullptr;

	// 1) 从缓存中查找 Actor → 组件类型映射
	if (const TMap<TSubclassOf<UActorComponent>, TWeakObjectPtr<UActorComponent>>* ActorCache = CachedDependencies.Find(Actor))
	{
		if (const TWeakObjectPtr<UActorComponent>* const Found = ActorCache->Find(ComponentClass))
		{
			if (Found->IsValid())
				return Found->Get();
		}
	}

	return nullptr;
}

bool USingularisCombineComponent::AreDependenciesSatisfied(const USingularisCombine* Strategy, const FSingularisCombineContext& Context) const
{
	if (!Strategy)
		return true;

	// 1) 空声明视为无条件满足
	if (Strategy->ComponentDependencies.IsEmpty())
		return true;

	// 2) 逐作用域检查声明类型是否全部命中缓存
	for (const auto& Pair : Strategy->ComponentDependencies)
	{
		const ESingularisCombineDependencyScope Scope = Pair.Key;
		const TArray<TSubclassOf<UActorComponent>>& DeclaredTypes = Pair.Value;

		if (DeclaredTypes.IsEmpty())
			continue;

		const AActor* ScopeActor = nullptr;
		switch (Scope)
		{
		case ESingularisCombineDependencyScope::Instigator:
			ScopeActor = Context.Instigator;
			break;
		case ESingularisCombineDependencyScope::Avatar:
			ScopeActor = Context.Avatar;
			break;
		case ESingularisCombineDependencyScope::Target:
			ScopeActor = Context.Target;
			break;
		default:
			return false;
		}

		if (!ScopeActor)
			return false;

		if (const TMap<TSubclassOf<UActorComponent>, TWeakObjectPtr<UActorComponent>>* ActorCache = CachedDependencies.Find(ScopeActor))
		{
			for (const TSubclassOf<UActorComponent>& DepClass : DeclaredTypes)
			{
				if (!DepClass)
					continue;

				const TWeakObjectPtr<UActorComponent>* const Found = ActorCache->Find(DepClass);
				if (!Found || !Found->IsValid())
					return false;
			}
		}
		else
		{
			return false;
		}
	}

	return true;
}
```

- [ ] **Step 7: 修改 SingularisCombineBase.cpp — 恢复 GetDependency 委托**

将 Task 2 Step 10 的占位实现替换为真实委托：

改动前（占位）：
```cpp
UActorComponent* USingularisCombine::GetDependency(AActor* Actor, TSubclassOf<UActorComponent> ComponentClass) const
{
	// 占位：Task 3 完成后恢复委托至 CombineComponent->GetDependency
	return nullptr;
}
```

改动后：
```cpp
UActorComponent* USingularisCombine::GetDependency(AActor* Actor, TSubclassOf<UActorComponent> ComponentClass) const
{
	if (!Actor || !ComponentClass)
		return nullptr;

	// 1) 委托化合组件的缓存查询
	if (const USingularisCombineComponent* const CombineComponent = Cast<USingularisCombineComponent>(GetOuter()))
		return CombineComponent->GetDependency(Actor, ComponentClass);

	return nullptr;
}
```

- [ ] **Step 8: 编译验证**

运行编译命令。预期：编译成功。三个方法就绪，但尚未接入 EvaluatePipeline（Task 4 处理）。

---

## Task 4: 接入 EvaluatePipeline 与 TickComponent

**Files:**
- Modify: `Plugins/SingularisCombine/Source/SingularisCombine/Private/Components/SingularisCombineComponent.cpp`

**Interfaces:**
- Consumes: `ResolveDependencies(Context)` + `AreDependenciesSatisfied(Strategy, Context)`（Task 3 产生）
- Produces: 完整的依赖注入流程接入

- [ ] **Step 1: 修改 SingularisCombineComponent.cpp — EvaluatePipeline 移除旧 ResolveDependencies 调用**

删除 `EvaluatePipeline` 中 `CollectAllTags` 后的旧依赖预解析循环：

删除前：
```cpp
	// 1) 所有端：刷新全局黑板（GameplayTags + 原生 FName 标签）；仅在标签集合实际变更时广播
	CollectAllTags();

	// 2) 所有端：预解析策略声明的组件依赖，刷新策略内部缓存
	for (const FSingularisCombineEntry& Entry : CombinePipeline.Combines)
	{
		if (USingularisCombine* const Combine = Entry.Combine)
			Combine->ResolveDependencies();
	}

	const bool bGameplayTagsChanged = BlackboardTags != PreviousBlackboardTags;
```

删除后：
```cpp
	// 1) 所有端：刷新全局黑板（GameplayTags + 原生 FName 标签）；仅在标签集合实际变更时广播
	CollectAllTags();

	const bool bGameplayTagsChanged = BlackboardTags != PreviousBlackboardTags;
```

- [ ] **Step 2: 修改 SingularisCombineComponent.cpp — EvaluatePipeline 在 Context 构建后调用 ResolveDependencies**

在 `EvaluatePipeline` 的服务端权威部分，Context 构建后、策略评估循环前，调用 `ResolveDependencies(Context)`：

改动前：
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

改动后：
```cpp
	FSingularisCombineContext Context;
	Context.Instigator = Cast<AActor>(OwnerActor->GetInstigator());
	Context.Avatar = OwnerActor;
	Context.Target = OwnerActor;
	Context.CombineComponent = this;

	// 3) 读取待处理事件载荷（TriggerEvaluate 写入，周期轮询时为空）
	const FSingularisCombineTransientPayload Payload = PendingPayload;

	// 4) 预解析管线所有策略声明的组件依赖（按 Context 的三个 Actor 分别缓存）
	ResolveDependencies(Context);

	for (FSingularisCombineEntry& Entry : CombinePipeline.Combines)
```

- [ ] **Step 3: 修改 SingularisCombineComponent.cpp — 策略评估循环增加前置预检**

在策略评估循环中，每个策略调用 `CanReaction` 前增加 `AreDependenciesSatisfied` 前置检查：

改动前：
```cpp
	for (FSingularisCombineEntry& Entry : CombinePipeline.Combines)
	{
		USingularisCombine* const Combine = Entry.Combine;
		if (!Combine)
			continue;

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
```

改动后：
```cpp
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
```

- [ ] **Step 4: 修改 SingularisCombineComponent.cpp — TickComponent 调用 ResolveDependencies**

在 `TickComponent` 中，Context 构建后、SustainReaction 循环前，调用 `ResolveDependencies(Context)`：

改动前：
```cpp
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
```

改动后：
```cpp
	// 1) 构建化合上下文
	FSingularisCombineContext Context;
	Context.Avatar = OwnerActor;
	Context.Instigator = Cast<AActor>(OwnerActor->GetInstigator());
	Context.Target = nullptr;
	Context.CombineComponent = this;

	// 2) 服务端：评估间隔为 0 时每帧执行管线
	if (OwnerActor->HasAuthority() && AutoEvaluateInterval <= 0.0f)
		EvaluatePipeline();

	// 3) 所有端：预解析依赖（保证 SustainReaction 中 GetDependency 可用）
	ResolveDependencies(Context);

	// 4) 所有端：遍历激活的策略并调用 SustainReaction（驱动瞬态效果）
	for (const FSingularisCombineEntry& Entry : CombinePipeline.Combines)
```

- [ ] **Step 5: 编译验证**

运行编译命令。预期：编译成功。完整依赖注入流程就绪。

---

## Task 5: 更新文档

**Files:**
- Modify: `Plugins/SingularisCombine/Docs/2026-08-22-combine-mid-layer-adaptation-design.md`
- Modify: `Plugins/SingularisCombine/DESIGN.md`

- [ ] **Step 1: 更新 spec 文档**

更新 `Plugins/SingularisCombine/Docs/2026-08-22-combine-mid-layer-adaptation-design.md` 中的"查询机制：声明式依赖 + 辅助函数"章节，反映：
- `ComponentDependencies` 改为 `TMap<ESingularisCombineDependencyScope, TArray<TSubclassOf<UActorComponent>>>` 结构化声明
- 依赖缓存与解析移至组件层（`USingularisCombineComponent::CachedDependencies` + `ResolveDependencies(Context)`）
- `GetDependency` 签名改为 `(AActor*, Class)`
- 新增 `AreDependenciesSatisfied(Strategy, Context)` 作为 `CanReaction` 前置预检
- 空声明=无条件满足，全有才执行（强约束）
- 新增 `ESingularisCombineDependencyScope` 枚举

- [ ] **Step 2: 更新 DESIGN.md**

更新 `Plugins/SingularisCombine/DESIGN.md` 中"声明式依赖注入"章节，同步上述变更。

- [ ] **Step 3: 验证文档无残留旧描述**

检查文档中不再出现：
- `TArray<TSubclassOf<UActorComponent>> ComponentDependencies`（旧扁平结构）
- 策略层 `CachedDependencies`（已移至组件层）
- 策略层 `ResolveDependencies()`（已移至组件层）
- `GetDependency(TSubclassOf<UActorComponent>)`（旧单参数签名）

## 实现后手动验证（可选）

编译通过后，在编辑器中验证：

1. **结构化声明**：打开测试蓝图策略，在"组件依赖"中配置 `{ Avatar: [UEquipmentComponent], Instigator: [UPlayerControllerComp] }`，验证编辑器显示 TMap 编辑界面
2. **三 Actor 查询**：在策略 `Reaction` 中调用 `GetDependency(Context.Instigator, UPlayerControllerComp::StaticClass())`，验证返回正确引用
3. **强约束预检**：配置一个不存在的依赖类型，验证策略不进入 `CanReaction`（在 CanReaction 加 print 验证不被调用）
4. **空声明回归**：未配置依赖的策略行为不变，正常进入 CanReaction
