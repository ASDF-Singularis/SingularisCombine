# SingularisCombine 声明式依赖注入 Implementation Plan

> **历史文档注记（2026-08-23）：** 本实施计划描述了重构前期的原始方案——以 `DeclaredComponents` CDO 字段 + `GetDeclaredComponentClasses()` 虚函数 + `friend` 友元回填为核心。后续审查发现该方案存在 friend 反模式与策略类职责不纯问题，已重构为「声明、查询、缓存全部收口于化合组件」的最终形态。本计划保留作为历史参考，但**请勿按本计划执行**——最新实现以 `DESIGN.md` 与源码为准。

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将 `SingularisCombine` 的依赖注入重构为「声明即逻辑、实例即状态」的声明式模型，原生路径走全局静态注册表 + 类型安全宏访问器，蓝图路径走纯 K2Node + 编译期 CDO 回填，并以 `GetDeclaredComponentClasses()` 虚函数归一声明源。

**Architecture:** 数据层 `DeclaredComponents` 收口为私有，仅服务蓝图 CDO 回填与运行期内部读取；统一虚函数 `GetDeclaredComponentClasses()` 在基类做「注册表（沿继承链）→ 自身 CDO」合并并惰性缓存；原生声明宏在类体内展开为类型安全访问器 + 静态注册器；蓝图侧用两个 K2Node（声明/获取）+ 全局 `OnBlueprintCompiled` hook 批量回填 CDO。运行时模块保持纯净，编辑器专属代码全部位于已存在的 `SingularisCombineEditor` 模块。

**Tech Stack:** Unreal Engine 5 (latest API), C++17/20, K2Node / `IKismetCompilerInterface` 编译 hook, `FSingularisCombineDependencyRegistry` 函数局部静态单例 + `FCriticalSection`。

**Spec:** `Plugins/SingularisCombine/DESIGN.md` (section: 声明式依赖注入)

## Global Constraints

- UE5 latest API；不读 UE5 引擎源码。
- 无 LSP / 无编译验证：每个 verification 步骤为代码级 self-review 检查清单，不写 build / pytest 命令。
- 遵循 coding-standards skill：现代语法、卫语句、零信任校验、幂等、SSOT、显式优于隐式、简洁功能性注释、无元注释、无装饰性边框。
- 遵循 singularis-skeleton skill：`#pragma region` 结构、`Singularis[Module]|中文名|Section` 分类命名、`USingularis[Name]...` 命名。
- 模块名 `SingularisCombine`，API 宏 `SINGULARISCOMBINE_API`；编辑器模块名 `SingularisCombineEditor`，API 宏 `SINGULARISCOMBINEEDITOR_API`（K2Node 类内同模块，用 `UCLASS()` 即可）。
- 注释不得含「修复/更新」等元注释，仅陈述功能性事实。
- 编辑器模块 `SingularisCombineEditor` 已存在且已有 asset type actions 注册逻辑，本计划的编辑器任务必须保留该现有逻辑，仅追加 K2Node 与编译 hook。

## File Structure

**Runtime module `SingularisCombine`（保持纯净，无编辑器依赖）：**

- **Create** `Public/Types/SingularisCombineDependencyRegistry.h` — 全局依赖注册表函数局部静态单例（header-only，`RegisterDependency` / `FindDeclaredClasses` / `Clear`，`FCriticalSection` 线程安全）。
- **Create** `Public/SingularisCombineDeclarativeMacros.h` — `SINGULARIS_DECLARE_DEPENDENCY` 宏（header-only，展开为访问器 + 静态注册器）。
- **Modify** `Public/Objects/SingularisCombineBase.h` — `DeclaredComponents` 降为私有并去 `EditDefaultsOnly`；新增 `virtual GetDeclaredComponentClasses()`；`GetDeclaredComponent` 降为 `protected`（蓝图 Get K2Node 经反射调用、宏访问器经继承调用）；移除 C++ 模板版；`friend` 编辑器 hook；新增惰性缓存成员。
- **Modify** `Private/Objects/SingularisCombineBase.cpp` — 实现 `GetDeclaredComponentClasses()` 基类（注册表沿继承链 + 自身 CDO 合并 + `mutable` 缓存）。
- **Modify** `Private/Components/SingularisCombineComponent.cpp` — `ResolveDependencies` / `AreDependenciesSatisfied` 改调 `GetDeclaredComponentClasses()`，不再直接读 `DeclaredComponents`。
- `SingularisCombine.Build.cs` — **不改**（运行时无新增依赖）。

**Editor module `SingularisCombineEditor`（已存在，含 asset type actions + Factory）：**

- **Modify** `SingularisCombineEditor.Build.cs` — 在现有依赖基础上追加 `BlueprintGraph` / `KismetCompiler` / `Kismet`。
- **Modify** `Public/SingularisCombineEditor.h` — 新增 `FDelegateHandle BlueprintCompileHandle{}` 成员（保留现有 asset type actions 成员）。
- **Modify** `Private/SingularisCombineEditor.cpp` — 在现有 `StartupModule`/`ShutdownModule` 的 asset type actions 逻辑外，追加编译 hook 注册/注销。
- **Create** `Public/Nodes/K2Node_SingularisDeclareDependency.h` + `Private/Nodes/K2Node_SingularisDeclareDependency.cpp` — 声明 K2Node（纯元数据节点，无引脚，编译期校验宿主为 `USingularisCombine` 派生蓝图）。
- **Create** `Public/Nodes/K2Node_SingularisGetDeclaredComponent.h` + `Private/Nodes/K2Node_SingularisGetDeclaredComponent.cpp` — 获取 K2Node（编译期绑定同名声明，输出类型按声明推导，`ExpandNode` 展开为对 `GetDeclaredComponent` 的纯函数调用）。
- **Create** `Private/Nodes/SingularisCombineBlueprintCompileHooks.h` + `Private/Nodes/SingularisCombineBlueprintCompileHooks.cpp` — 全局 `OnBlueprintCompiled` hook，扫全部声明节点批量回填 CDO `DeclaredComponents`。

---

### Task 1: Global Dependency Registry

**Files:**
- Create: `Plugins/SingularisCombine/Source/SingularisCombine/Public/Types/SingularisCombineDependencyRegistry.h`

**Interfaces:**
- Consumes: `ESingularisCombineDependencyScope` (`Types/SingularisCombineDependencyScope.h`)、`FSingularisCombineDependencyList` (`Types/SingularisCombineDependencyList.h`)。
- Produces:
  - `FSingularisCombineDependencyRegistry& FSingularisCombineDependencyRegistry::Get()`（函数局部静态单例）
  - `void RegisterDependency(UClass* StrategyClass, ESingularisCombineDependencyScope Scope, TSubclassOf<UActorComponent> ComponentClass)`
  - `const TMap<ESingularisCombineDependencyScope, FSingularisCombineDependencyList>* FindDeclaredClasses(UClass* StrategyClass) const`
  - `void Clear()`

- [ ] **Step 1: 创建注册表头文件**

```cpp
#pragma once

#include <CoreMinimal.h>
#include <HAL/CriticalSection.h>
#include <Templates/SubclassOf.h>
#include <UObject/ObjectMacros.h>

#include "Types/SingularisCombineDependencyList.h"
#include "Types/SingularisCombineDependencyScope.h"

class UClass;
class UActorComponent;

/**
 * 引力奇点化合依赖注册表
 * 进程级函数局部静态单例，按策略类登记其原生声明依赖。
 * 模块静态初始化期由声明宏的静态注册器写入；评估期由 GetDeclaredComponentClasses() 只读查询。
 * UClass 指针在引擎生命周期内稳定，作为 Map 键安全。
 */
class FSingularisCombineDependencyRegistry
{
public:
    static FSingularisCombineDependencyRegistry& Get()
    {
        static FSingularisCombineDependencyRegistry Instance;
        return Instance;
    }

    void RegisterDependency(
        UClass* StrategyClass,
        ESingularisCombineDependencyScope Scope,
        TSubclassOf<UActorComponent> ComponentClass)
    {
        if (!StrategyClass || !ComponentClass)
            return;

        FScopeLock Lock(&CriticalSection);
        TMap<ESingularisCombineDependencyScope, FSingularisCombineDependencyList>& ScopeMap = Registry.FindOrAdd(
            StrategyClass);
        ScopeMap.FindOrAdd(Scope).Classes.AddUnique(ComponentClass);
    }

    const TMap<ESingularisCombineDependencyScope, FSingularisCombineDependencyList>* FindDeclaredClasses(
        UClass* StrategyClass) const
    {
        if (!StrategyClass)
            return nullptr;

        FScopeLock Lock(&CriticalSection);
        return Registry.Find(StrategyClass);
    }

    void Clear()
    {
        FScopeLock Lock(&CriticalSection);
        Registry.Empty();
    }

private:
    FSingularisCombineDependencyRegistry() = default;

    mutable FCriticalSection CriticalSection;
    TMap<UClass*, TMap<ESingularisCombineDependencyScope, FSingularisCombineDependencyList>> Registry;
};
```

- [ ] **Step 2: Self-review**

检查清单：
- [ ] 仅 header，无 `.cpp`；所有方法 `inline`（类内定义）。
- [ ] `Get()` 为函数局部静态（C++11 magic statics，线程安全）。
- [ ] 所有公共方法用 `FScopeLock` 保护 `Registry`；`FindDeclaredClasses` 为 `const` 但锁 `mutable CriticalSection`。
- [ ] 入参零信任校验：`StrategyClass` / `ComponentClass` 空检查。
- [ ] `RegisterDependency` 幂等：`AddUnique` 保证重复登记不污染。
- [ ] 无 `UPROPERTY` / `UOBJECT` —— 纯 C++ 单例。
- [ ] 无元注释、无装饰边框。

- [ ] **Step 3: Commit**

```bash
git add Plugins/SingularisCombine/Source/SingularisCombine/Public/Types/SingularisCombineDependencyRegistry.h
git commit -m "feat: 新增依赖注册表单例"
```

---

### Task 2: `USingularisCombine` 数据层收口

**Files:**
- Modify: `Plugins/SingularisCombine/Source/SingularisCombine/Public/Objects/SingularisCombineBase.h`
- Modify: `Plugins/SingularisCombine/Source/SingularisCombine/Private/Objects/SingularisCombineBase.cpp`

**Interfaces:**
- Consumes: Task 1 `FSingularisCombineDependencyRegistry::Get().FindDeclaredClasses(UClass*)`。
- Produces:
  - `virtual const TMap<ESingularisCombineDependencyScope, FSingularisCombineDependencyList>& GetDeclaredComponentClasses() const`（Task 3、6、7 依赖此签名）
  - `protected UActorComponent* GetDeclaredComponent(ESingularisCombineDependencyScope, TSubclassOf<UActorComponent>) const`（Task 4 宏访问器、Task 7 Get K2Node 经反射依赖）
  - 私有 `DeclaredComponents` + `friend class FSingularisCombineBlueprintCompileHook;`（Task 8 依赖）

- [ ] **Step 1: 修改 `SingularisCombineBase.h` — 删除原 `Parameter` 区的 `DeclaredComponents`、原 `State` 区的 `GetDeclaredComponent` 与模板版，新增虚函数与收口字段**

定位原 `Parameter` 区（约 L48-59）中的 `DeclaredComponents` UPROPERTY 块并删除。在 `State` 区（`IsActive()` 之后）新增虚函数：

```cpp
    /**
     * 归一声明依赖集合
     * 沿继承链查询全局注册表（原生声明宏写入），并合并自身 CDO 声明（蓝图回填）。
     * 结果惰性缓存于实例，运行期不可变；供 ResolveDependencies 聚合、AreDependenciesSatisfied 门控共用。
     */
    virtual const TMap<ESingularisCombineDependencyScope, FSingularisCombineDependencyList>&
    GetDeclaredComponentClasses() const;
```

定位原 `State` 区中的 `GetDeclaredComponent` UFUNCTION（含 `BlueprintPure`）与紧跟的 `template <typename T>` 模板版，整体删除。在 `protected:` 区新增仅 UFUNCTION 版本（去模板）：

```cpp
protected:
#pragma region State

    /**
     * 获取声明式配置的组件实例（预缓存）
     * 仅宏生成的类型安全访问器与蓝图获取节点经反射调用。
     * 必须在化合组件 ResolveDependencies(Context) 之后调用。
     * @param Scope           依赖作用域（Instigator / Avatar / Target）
     * @param ComponentClass  声明在依赖集合中的组件类型
     * @return 预缓存的组件引用，未找到或未声明时返回 nullptr
     */
    UFUNCTION(
        BlueprintPure,
        Category = "SingularisCombine|引力奇点化合|State",
        meta = (DisplayName = "获取声明组件", DeterminesOutputType = "ComponentClass")
    )
    UActorComponent* GetDeclaredComponent(
        ESingularisCombineDependencyScope Scope,
        TSubclassOf<UActorComponent> ComponentClass
    ) const;

#pragma endregion
```

在 `private:` `Internal Variable` 区，新增收口字段与缓存（保留原有 `DependencyProvider`）：

```cpp
private:
#pragma region Internal Variable

    /** 友元：编辑器编译 hook 回填 CDO 声明 */
    friend class FSingularisCombineBlueprintCompileHook;

    /**
     * 声明式组件（收口）
     * 仅服务蓝图 CDO 回填路径与运行期内部读取；外部不可编辑、不可直接读写。
     * 原生路径下注册表为唯一真相源，本字段闲置。
     */
    UPROPERTY()
    TMap<ESingularisCombineDependencyScope, FSingularisCombineDependencyList> DeclaredComponents{};

    /** 声明集合惰性缓存（GetDeclaredComponentClasses 首次调用后填充，运行期不变） */
    mutable TMap<ESingularisCombineDependencyScope, FSingularisCombineDependencyList> CachedDeclaredClasses{};

    /** 缓存是否已构建 */
    mutable bool bDeclaredClassesCached = false;

    /** 化合组件注入的依赖查询提供者（弱引用，随组件生命周期自动失效） */
    TWeakInterfacePtr<ISingularisCombineDependencyProvider> DependencyProvider{};

#pragma endregion
```

保留 `SetDependencyProvider` 在 `#pragma region Dependency Injection`（public）不变。

- [ ] **Step 2: 修改 `SingularisCombineBase.cpp` — 实现 `GetDeclaredComponentClasses()`**

在 `.cpp` 顶部 include 区新增：

```cpp
#include "Types/SingularisCombineDependencyRegistry.h"
```

原 `UActorComponent* USingularisCombine::GetDeclaredComponent(...)` 实现保持不变（已是 `DependencyProvider` 委托实现，签名匹配新 `protected` 声明）。在其后新增：

```cpp
const TMap<ESingularisCombineDependencyScope, FSingularisCombineDependencyList>&
USingularisCombine::GetDeclaredComponentClasses() const
{
    if (bDeclaredClassesCached)
        return CachedDeclaredClasses;

    // 1) 沿继承链聚合所有祖先的原生注册表声明（含自身）：子策略继承父策略的全部声明
    for (UClass* Class = GetClass(); Class; Class = Class->GetSuperClass())
    {
        if (const TMap<ESingularisCombineDependencyScope, FSingularisCombineDependencyList>* const Found =
            FSingularisCombineDependencyRegistry::Get().FindDeclaredClasses(Class))
        {
            for (const auto& Pair : *Found)
                for (const TSubclassOf<UActorComponent>& DepClass : Pair.Value.Classes)
                    CachedDeclaredClasses.FindOrAdd(Pair.Key).Classes.AddUnique(DepClass);
        }
    }

    // 2) 合并自身 CDO 声明（蓝图回填路径）
    for (const auto& Pair : DeclaredComponents)
        for (const TSubclassOf<UActorComponent>& DepClass : Pair.Value.Classes)
            CachedDeclaredClasses.FindOrAdd(Pair.Key).Classes.AddUnique(DepClass);

    bDeclaredClassesCached = true;
    return CachedDeclaredClasses;
}
```

- [ ] **Step 3: Self-review**

检查清单：
- [ ] `DeclaredComponents` 为 `UPROPERTY()` 无 `EditDefaultsOnly`/`BlueprintReadOnly`，可序列化但不暴露编辑器/蓝图。
- [ ] `GetDeclaredComponent` 为 `protected`，保留 `BlueprintPure`（蓝图 Get K2Node 经反射调用 self 的受保护 UFUNCTION 合法）。
- [ ] C++ 模板版 `GetDeclaredComponent<T>` 已删除（由宏访问器取代）。
- [ ] `GetDeclaredComponentClasses()` 为 `virtual`，返回 `const TMap&`；首次构建后走缓存（`bDeclaredClassesCached`），幂等。
- [ ] 注册表查询沿继承链完整聚合（不 break）：子策略继承父策略的全部声明；`AddUnique` 保证同作用域同类型不重复。
- [ ] `friend class FSingularisCombineBlueprintCompileHook;` 隐式前向声明，运行时构建不引用编辑器符号。
- [ ] `.cpp` 已 include 注册表头。
- [ ] 原有 `SetDependencyProvider`、网络相关覆写（`GetWorld`/`GetLifetimeReplicatedProps`/`IsSupportedForNetworking`/`GetFunctionCallspace`/`CallRemoteFunction`）及四个 `_Implementation` SPI 实现均未被改动。

- [ ] **Step 4: Commit**

```bash
git add Plugins/SingularisCombine/Source/SingularisCombine/Public/Objects/SingularisCombineBase.h \
        Plugins/SingularisCombine/Source/SingularisCombine/Private/Objects/SingularisCombineBase.cpp
git commit -m "refactor: 收口 DeclaredComponents 并新增 GetDeclaredComponentClasses"
```

---

### Task 3: `USingularisCombineComponent` 切换至 `GetDeclaredComponentClasses()`

**Files:**
- Modify: `Plugins/SingularisCombine/Source/SingularisCombine/Private/Components/SingularisCombineComponent.cpp`

**Interfaces:**
- Consumes: Task 2 `USingularisCombine::GetDeclaredComponentClasses()`。
- Produces: `ResolveDependencies` / `AreDependenciesSatisfied` 不再直接读 `DeclaredComponents`。

- [ ] **Step 1: 修改 `ResolveDependencies` 的依赖聚合循环**

将原 `for (const auto& Pair : Combine->DeclaredComponents)` 整段替换为：

```cpp
    for (const auto& Pair : Combine->GetDeclaredComponentClasses())
    {
        TSet<TSubclassOf<UActorComponent>>& TypeSet = AggregatedDeps.FindOrAdd(Pair.Key);
        for (const TSubclassOf<UActorComponent>& DepClass : Pair.Value.Classes)
        {
            if (DepClass)
                TypeSet.Add(DepClass);
        }
    }
```

- [ ] **Step 2: 修改 `AreDependenciesSatisfied` 的门控读取**

将函数体开头的「空声明」判断与逐作用域循环整体替换为：

```cpp
bool USingularisCombineComponent::AreDependenciesSatisfied(
    const USingularisCombine* Strategy,
    const FSingularisCombineContext& Context
) const
{
    if (!Strategy)
        return true;

    const TMap<ESingularisCombineDependencyScope, FSingularisCombineDependencyList>& Declared =
        Strategy->GetDeclaredComponentClasses();

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
```

- [ ] **Step 3: Self-review**

检查清单：
- [ ] 文件内不再出现 `Combine->DeclaredComponents` 或 `Strategy->DeclaredComponents`（`DeclaredComponents` 已私有，本应编译失败即视为残留）。
- [ ] 门控集合与查询集合同源（均经 `GetDeclaredComponentClasses()`），符合「声明集合 == 门控集合 == 可查询集合」。
- [ ] 空声明返回 `true`；任一缺失返回 `false`，原语义保留。
- [ ] `ResolveDependencies` 中 `AggregatedDeps` 仍跨策略并集，未改其他逻辑（构造并集 → 映射 Actor → 缓存槽位写入）。
- [ ] `GetDeclaredComponent`（组件侧，从 `CachedDependencies` 读取的实现）未被改动。

- [ ] **Step 4: Commit**

```bash
git add Plugins/SingularisCombine/Source/SingularisCombine/Private/Components/SingularisCombineComponent.cpp
git commit -m "refactor: 化合组件依赖读取切换为 GetDeclaredComponentClasses"
```

---

### Task 4: `SINGULARIS_DECLARE_DEPENDENCY` 宏

**Files:**
- Create: `Plugins/SingularisCombine/Source/SingularisCombine/Public/SingularisCombineDeclarativeMacros.h`

**Interfaces:**
- Consumes: Task 1 注册表 `RegisterDependency`、Task 2 `protected GetDeclaredComponent(Scope, Class)`。
- Produces: 宏 `SINGULARIS_DECLARE_DEPENDENCY(Scope, UClassType, Name)`，展开为类型安全访问器 `Get[Name]()` 与静态注册器。

**宏展开决策（已定）：** 宏不生成 `GetDeclaredComponentClasses()` override；基类虚函数在 Task 2 已实现「注册表沿继承链 → 自身 CDO」合并。宏仅负责 (1) 类型安全访问器、(2) 静态注册器登记。展开示例：

`SINGULARIS_DECLARE_DEPENDENCY(Avatar, UMyComponent, MyComp)` 在 `USingularisCombine` 派生类 public 区展开为：

```cpp
UMyComponent* GetMyComp() const
{
    return Cast<UMyComponent>(GetDeclaredComponent(ESingularisCombineDependencyScope::Avatar, UMyComponent::StaticClass()));
}
struct FMyCompRegistrar
{
    FMyCompRegistrar()
    {
        FSingularisCombineDependencyRegistry::Get().RegisterDependency(
            StaticClass(), ESingularisCombineDependencyScope::Avatar, UMyComponent::StaticClass());
    }
};
static inline FMyCompRegistrar MyCompRegistrar{};
```

- [ ] **Step 1: 创建宏头文件**

```cpp
#pragma once

#include <Templates/Casts.h>

#include "Types/SingularisCombineDependencyRegistry.h"
#include "Types/SingularisCombineDependencyScope.h"

/**
 * 在 USingularisCombine 派生类体内声明类型安全的依赖访问器与静态注册器
 * 必须放置于类体的 public 区域。
 *
 * 展开：
 *   1) inline 访问器 Get[Name]() const，返回 [UClassType]*；
 *      内部委托 GetDeclaredComponent(Scope, UClassType::StaticClass())，编译期类型校验，不可绕过。
 *   2) 静态注册器实例，模块加载期向全局注册表登记
 *      (StaticClass(), Scope, UClassType::StaticClass())。
 *
 * @param Scope        ESingularisCombineDependencyScope 枚举项（如 Avatar）
 * @param UClassType   组件类型（如 UMovementComponent）
 * @param Name         访问器后缀（如 Move → 生成 GetMove）
 */
#define SINGULARIS_DECLARE_DEPENDENCY(Scope, UClassType, Name) \
    UClassType* Get##Name() const \
    { \
        return Cast<UClassType>(GetDeclaredComponent(ESingularisCombineDependencyScope::Scope, UClassType::StaticClass())); \
    } \
    struct F##Name##Registrar \
    { \
        F##Name##Registrar() \
        { \
            FSingularisCombineDependencyRegistry::Get().RegisterDependency( \
                StaticClass(), ESingularisCombineDependencyScope::Scope, UClassType::StaticClass()); \
        } \
    }; \
    static inline F##Name##Registrar Name##Registrar{};
```

- [ ] **Step 2: Self-review**

检查清单：
- [ ] 宏内不注入 `public:`/`private:`（避免扰乱类访问结构），文档要求置于 `public` 区域。
- [ ] `Get##Name()` 调用 `GetDeclaredComponent`（Task 2 已 `protected`），派生类可访问。
- [ ] 静态注册器用 `static inline`（C++17），header-only，无需 `.cpp` 定义。
- [ ] 嵌套 `struct F##Name##Registrar` 的构造调用 `StaticClass()`：非限定名查找会解析到外层 `USingularisCombine` 派生类的 `StaticClass()` 静态成员。
- [ ] `UClassType::StaticClass()` 在模块静态初始化期可调用（UE5 标准模式，与 GameplayTags 宏一致）；注册表 `Get()` 函数局部静态保证此时尚未构造则惰性构造。
- [ ] 无 `.generated.h`（非 UCLASS/USTRUCT，纯宏）。

- [ ] **Step 3: Commit**

```bash
git add Plugins/SingularisCombine/Source/SingularisCombine/Public/SingularisCombineDeclarativeMacros.h
git commit -m "feat: 新增 SINGULARIS_DECLARE_DEPENDENCY 宏"
```

---

### Task 5: 修改 `SingularisCombineEditor` 模块（追加依赖与 hook 句柄）

**Files:**
- Modify: `Plugins/SingularisCombine/Source/SingularisCombineEditor/SingularisCombineEditor.Build.cs`
- Modify: `Plugins/SingularisCombine/Source/SingularisCombineEditor/Public/SingularisCombineEditor.h`
- Modify: `Plugins/SingularisCombine/Source/SingularisCombineEditor/Private/SingularisCombineEditor.cpp`（hook 注册在 Task 8 注入）

**Interfaces:**
- Consumes: 无（仅模块适配）。
- Produces: `FSingularisCombineEditorModule` 新增 `FDelegateHandle BlueprintCompileHandle{}` 成员（Task 8 依赖）、Build.cs 追加 K2Node 所需模块依赖（Task 6/7/8 依赖）。

- [ ] **Step 1: 修改 `SingularisCombineEditor.Build.cs` — 追加 K2Node/编译 hook 模块依赖**

在现有 `PrivateDependencyModuleNames.AddRange` 数组中追加 `"BlueprintGraph"`、`"KismetCompiler"`、`"Kismet"`（保留现有 `Core`/`CoreUObject`/`Engine`/`SingularisCombine`/`UMG`/`UMGEditor`/`UnrealEd`/`AssetTools`/`ContentBrowser`）。最终 `AddRange` 内容：

```csharp
PrivateDependencyModuleNames.AddRange(
    [
        "Core",
        "CoreUObject",
        "Engine",

        "SingularisCombine",

        "UMG",
        "UMGEditor",
        "UnrealEd",
        "AssetTools",
        "ContentBrowser",

        "BlueprintGraph",
        "KismetCompiler",
        "Kismet"
    ]
);
```

- [ ] **Step 2: 修改 `SingularisCombineEditor.h` — 新增 hook 句柄成员**

在现有 `private:` 区（`CreatedAssetTypeActions` 之后）新增：

```cpp
    /** 蓝图编译 hook 注册句柄 */
    FDelegateHandle BlueprintCompileHandle{};
```

保留现有 `CreatedAssetTypeActions` 与 `RegisterAssetTypeAction`。最终头文件 private 区：

```cpp
private:
    TArray<TSharedPtr<IAssetTypeActions>> CreatedAssetTypeActions{};

    /** 蓝图编译 hook 注册句柄 */
    FDelegateHandle BlueprintCompileHandle{};

    void RegisterAssetTypeAction(IAssetTools& AssetTools, const TSharedRef<IAssetTypeActions>& Action);
```

- [ ] **Step 3: Self-review（.cpp 暂不改，Task 8 注入 hook）**

检查清单：
- [ ] `Build.cs` 新增三个模块依赖；现有依赖全部保留。
- [ ] `SingularisCombineEditor.h` 新增 `FDelegateHandle BlueprintCompileHandle{}`；现有成员全部保留。
- [ ] `.cpp` 在本任务不修改（Task 8 在 `StartupModule`/`ShutdownModule` 中注入 hook 调用，同时保留 asset type actions 逻辑）。
- [ ] `.uplugin` 已声明 `SingularisCombineEditor`（Type=Editor，LoadingPhase=Default），无需改。

- [ ] **Step 4: Commit**

```bash
git add Plugins/SingularisCombine/Source/SingularisCombineEditor/SingularisCombineEditor.Build.cs \
        Plugins/SingularisCombine/Source/SingularisCombineEditor/Public/SingularisCombineEditor.h
git commit -m "feat: 编辑器模块追加 K2Node 依赖与编译 hook 句柄"
```

---

### Task 6: `K2Node_SingularisDeclareDependency`

**Files:**
- Create: `Plugins/SingularisCombine/Source/SingularisCombineEditor/Public/Nodes/K2Node_SingularisDeclareDependency.h`
- Create: `Plugins/SingularisCombine/Source/SingularisCombineEditor/Private/Nodes/K2Node_SingularisDeclareDependency.cpp`

**Interfaces:**
- Consumes: `USingularisCombine`（运行时模块，校验宿主蓝图父类）。
- Produces:
  - `UK2Node_SingularisDeclareDependency` UCLASS，公共字段 `Scope` / `ComponentClass` / `DependencyName`（Task 7、Task 8 依赖读取这三个字段）。

- [ ] **Step 1: 创建 K2Node 头文件**

```cpp
#pragma once

#include <CoreMinimal.h>
#include <K2Node.h>

#include "Types/SingularisCombineDependencyScope.h"
#include "K2Node_SingularisDeclareDependency.generated.h"

class UActorComponent;

/**
 * 声明依赖 K2Node（纯声明节点）
 * 无执行/数据引脚，仅作为编译期元数据载体。
 * 编辑器面板配置作用域 + 组件类 + 名字；编译期校验宿主蓝图为 USingularisCombine 派生。
 */
UCLASS()
class UK2Node_SingularisDeclareDependency : public UK2Node
{
    GENERATED_BODY()

public:
    UPROPERTY(EditDefaultsOnly, Category = "SingularisCombine|声明")
    ESingularisCombineDependencyScope Scope = ESingularisCombineDependencyScope::Avatar;

    UPROPERTY(EditDefaultsOnly, Category = "SingularisCombine|声明")
    TSubclassOf<UActorComponent> ComponentClass = nullptr;

    UPROPERTY(EditDefaultsOnly, Category = "SingularisCombine|声明")
    FName DependencyName = NAME_None;

    virtual void AllocateDefaultPins() override;
    virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
    virtual FText GetTooltipText() const override;
    virtual FLinearColor GetNodeTitleColor() const override;
    virtual bool ShouldShowNodeProperties() const override { return true; }
    virtual void ValidateNodeDuringCompilation(FCompilerResultsLog& MessageLog) const override;
    virtual void GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const override;
    virtual FText GetMenuCategory() const override;
    virtual bool IsCompatibleWithGraph(const UEdGraph* TargetGraph) const override;
};
```

- [ ] **Step 2: 创建 K2Node 实现**

```cpp
#include "Nodes/K2Node_SingularisDeclareDependency.h"

#include <BlueprintActionDatabaseRegistrar.h>
#include <BlueprintNodeSpawner.h>
#include <EdGraph/EdGraph.h>
#include <Kismet/BlueprintEditorUtils.h>
#include <Kismet2/CompilerResultsLog.h>

#include "Objects/SingularisCombineBase.h"

#define LOCTEXT_NAMESPACE "K2Node_SingularisDeclareDependency"

void UK2Node_SingularisDeclareDependency::AllocateDefaultPins()
{
    Super::AllocateDefaultPins();
    // 纯声明节点：无执行/数据引脚
}

FText UK2Node_SingularisDeclareDependency::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
    if (DependencyName.IsNone())
        return LOCTEXT("NodeTitle_None", "声明依赖");
    return FText::Format(LOCTEXT("NodeTitle", "声明依赖 {0}"), FText::FromName(DependencyName));
}

FText UK2Node_SingularisDeclareDependency::GetTooltipText() const
{
    return LOCTEXT("NodeTooltip", "声明策略对某作用域组件的依赖，编译期写入 CDO");
}

FLinearColor UK2Node_SingularisDeclareDependency::GetNodeTitleColor() const
{
    return FLinearColor(0.8f, 0.4f, 1.0f);
}

void UK2Node_SingularisDeclareDependency::ValidateNodeDuringCompilation(FCompilerResultsLog& MessageLog) const
{
    Super::ValidateNodeDuringCompilation(MessageLog);

    UBlueprint* Blueprint = GetBlueprint();
    if (!Blueprint || !Blueprint->ParentClass || !Blueprint->ParentClass->IsChildOf(USingularisCombine::StaticClass()))
    {
        MessageLog.Error(TEXT("@0 必须位于 USingularisCombine 派生蓝图中"), this);
    }
    if (!ComponentClass)
    {
        MessageLog.Error(TEXT("@0 的组件类未设置"), this);
    }
    if (DependencyName.IsNone())
    {
        MessageLog.Error(TEXT("@0 的依赖名字未设置"), this);
    }
}

void UK2Node_SingularisDeclareDependency::GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
    UClass* ActionClass = GetClass();
    if (!ActionRegistrar.IsOpenForRegistration(ActionClass))
        return;

    UBlueprintNodeSpawner* Spawner = UBlueprintNodeSpawner::Create(ActionClass);
    check(Spawner);
    ActionRegistrar.AddBlueprintAction(ActionClass, Spawner);
}

FText UK2Node_SingularisDeclareDependency::GetMenuCategory() const
{
    return LOCTEXT("MenuCategory", "SingularisCombine|声明");
}

bool UK2Node_SingularisDeclareDependency::IsCompatibleWithGraph(const UEdGraph* TargetGraph) const
{
    if (!Super::IsCompatibleWithGraph(TargetGraph))
        return false;

    UBlueprint* Blueprint = FBlueprintEditorUtils::FindBlueprintForGraph(TargetGraph);
    return Blueprint && Blueprint->ParentClass && Blueprint->ParentClass->IsChildOf(USingularisCombine::StaticClass());
}

#undef LOCTEXT_NAMESPACE
```

- [ ] **Step 3: Self-review**

检查清单：
- [ ] `AllocateDefaultPins` 不创建任何引脚（纯元数据节点）。
- [ ] `ValidateNodeDuringCompilation` 三重校验：宿主蓝图父类、`ComponentClass`、`DependencyName`。
- [ ] `IsCompatibleWithGraph` 限制该节点只能拖入 `USingularisCombine` 派生蓝图图。
- [ ] `GetMenuActions` / `GetMenuCategory` 走标准 spawner 模式。
- [ ] 头文件 `#include "K2Node_SingularisDeclareDependency.generated.h"` 存在（UHT 生成）。
- [ ] 文件路径为 `Nodes/`（匹配现有 `Factories/` 约定，不创建 `Blueprint/`）。
- [ ] 无元注释。

- [ ] **Step 4: Commit**

```bash
git add Plugins/SingularisCombine/Source/SingularisCombineEditor/Public/Nodes/K2Node_SingularisDeclareDependency.h \
        Plugins/SingularisCombine/Source/SingularisCombineEditor/Private/Nodes/K2Node_SingularisDeclareDependency.cpp
git commit -m "feat: 新增声明依赖 K2Node"
```

---

### Task 7: `K2Node_SingularisGetDeclaredComponent`

**Files:**
- Create: `Plugins/SingularisCombine/Source/SingularisCombineEditor/Public/Nodes/K2Node_SingularisGetDeclaredComponent.h`
- Create: `Plugins/SingularisCombine/Source/SingularisCombineEditor/Private/Nodes/K2Node_SingularisGetDeclaredComponent.cpp`

**Interfaces:**
- Consumes: Task 6 `UK2Node_SingularisDeclareDependency`（字段 `Scope` / `ComponentClass` / `DependencyName`）；Task 2 `USingularisCombine::GetDeclaredComponent`（`protected` UFUNCTION，经反射调用）。
- Produces: `UK2Node_SingularisGetDeclaredComponent` UCLASS，编译期绑定同名声明、输出类型按声明推导。

- [ ] **Step 1: 创建 K2Node 头文件**

```cpp
#pragma once

#include <CoreMinimal.h>
#include <K2Node.h>

#include "K2Node_SingularisGetDeclaredComponent.generated.h"

class UK2Node_SingularisDeclareDependency;

/**
 * 获取声明组件 K2Node
 * 编译期绑定同蓝图内同名 DeclareDependency 节点，输出引脚类型由声明的组件类推导。
 * ExpandNode 展开为对 USingularisCombine::GetDeclaredComponent(Scope, Class) 的纯函数调用。
 */
UCLASS()
class UK2Node_SingularisGetDeclaredComponent : public UK2Node
{
    GENERATED_BODY()

public:
    UPROPERTY(EditDefaultsOnly, Category = "SingularisCombine|声明")
    FName DependencyName = NAME_None;

    virtual void AllocateDefaultPins() override;
    virtual void ExpandNode(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph) override;
    virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
    virtual FText GetTooltipText() const override;
    virtual FLinearColor GetNodeTitleColor() const override;
    virtual bool ShouldShowNodeProperties() const override { return true; }
    virtual void ValidateNodeDuringCompilation(FCompilerResultsLog& MessageLog) const override;
    virtual void GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const override;
    virtual FText GetMenuCategory() const override;
    virtual bool IsCompatibleWithGraph(const UEdGraph* TargetGraph) const override;

private:
    const UK2Node_SingularisDeclareDependency* FindMatchingDeclaration() const;
};
```

- [ ] **Step 2: 创建 K2Node 实现**

```cpp
#include "Nodes/K2Node_SingularisGetDeclaredComponent.h"

#include <BlueprintActionDatabaseRegistrar.h>
#include <BlueprintNodeSpawner.h>
#include <EdGraph/EdGraph.h>
#include <EdGraphSchema_K2.h>
#include <K2Node_CallFunction.h>
#include <Kismet/BlueprintEditorUtils.h>
#include <Kismet2/CompilerResultsLog.h>

#include "Nodes/K2Node_SingularisDeclareDependency.h"
#include "Objects/SingularisCombineBase.h"
#include "Types/SingularisCombineDependencyScope.h"

#define LOCTEXT_NAMESPACE "K2Node_SingularisGetDeclaredComponent"

void UK2Node_SingularisGetDeclaredComponent::AllocateDefaultPins()
{
    Super::AllocateDefaultPins();
    CreatePin(
        EGPD_Output,
        UEdGraphSchema_K2::PC_Object,
        UActorComponent::StaticClass(),
        UEdGraphSchema_K2::PN_ReturnValue);
}

const UK2Node_SingularisDeclareDependency* UK2Node_SingularisGetDeclaredComponent::FindMatchingDeclaration() const
{
    UBlueprint* Blueprint = GetBlueprint();
    if (!Blueprint)
        return nullptr;

    TArray<UEdGraph*> Graphs;
    FBlueprintEditorUtils::GetAllGraphs(Blueprint, Graphs);

    for (const UEdGraph* Graph : Graphs)
    {
        if (!Graph)
            continue;

        TArray<UK2Node_SingularisDeclareDependency*> Found;
        Graph->GetNodesOfClass<UK2Node_SingularisDeclareDependency>(Found);
        for (const UK2Node_SingularisDeclareDependency* Node : Found)
        {
            if (Node && Node->DependencyName == DependencyName)
                return Node;
        }
    }
    return nullptr;
}

void UK2Node_SingularisGetDeclaredComponent::ValidateNodeDuringCompilation(FCompilerResultsLog& MessageLog) const
{
    Super::ValidateNodeDuringCompilation(MessageLog);

    if (DependencyName.IsNone())
    {
        MessageLog.Error(TEXT("@0 的依赖名字未设置"), this);
        return;
    }

    const UK2Node_SingularisDeclareDependency* Declaration = FindMatchingDeclaration();
    if (!Declaration)
    {
        MessageLog.Error(TEXT("@0 在蓝图中未找到匹配的声明依赖节点"), this);
        return;
    }
    if (!Declaration->ComponentClass)
    {
        MessageLog.Error(TEXT("@0 匹配的声明依赖节点组件类未设置"), this);
    }
}

void UK2Node_SingularisGetDeclaredComponent::ExpandNode(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph)
{
    Super::ExpandNode(CompilerContext, SourceGraph);

    const UK2Node_SingularisDeclareDependency* Declaration = FindMatchingDeclaration();
    if (!Declaration || !Declaration->ComponentClass)
    {
        CompilerContext.MessageLog.Error(TEXT("@0 缺少匹配的声明依赖"), this);
        BreakAllNodeLinks();
        return;
    }

    UK2Node_CallFunction* CallGet = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
    CallGet->FunctionReference.SetSelfMemberFunction(
        USingularisCombine::StaticClass(),
        GET_FUNCTION_NAME_CHECKED(USingularisCombine, GetDeclaredComponent));
    CallGet->bIsPureFunc = true;
    CallGet->AllocateDefaultPins();

    // Target = self（策略实例）：迁移本节点 self 引脚连接到调用节点
    UEdGraphPin* NodeSelfPin = FindPin(UEdGraphSchema_K2::PN_Self);
    UEdGraphPin* CallSelfPin = CallGet->FindPinChecked(UEdGraphSchema_K2::PN_Self);
    if (NodeSelfPin && NodeSelfPin->LinkedTo.Num() > 0)
        CallSelfPin->MakeLinkTo(NodeSelfPin->LinkedTo[0]);

    // Scope 字面量
    if (UEdGraphPin* ScopeArg = CallGet->FindPinChecked(TEXT("Scope")))
    {
        ScopeArg->DefaultValue = StaticEnum<ESingularisCombineDependencyScope>()->GetNameStringByValue(
            static_cast<int64>(Declaration->Scope));
    }

    // ComponentClass 字面量
    if (UEdGraphPin* ClassArg = CallGet->FindPinChecked(TEXT("ComponentClass")))
        ClassArg->DefaultObject = Declaration->ComponentClass;

    // 输出引脚类型推导 + 链接迁移到中间调用节点
    UEdGraphPin* NodeOutPin = FindPinChecked(UEdGraphSchema_K2::PN_ReturnValue);
    UEdGraphPin* CallOutPin = CallGet->GetReturnValuePin();
    NodeOutPin->PinType.PinSubCategoryObject = Declaration->ComponentClass;
    CompilerContext.MovePinLinksToIntermediate(*NodeOutPin, *CallOutPin);

    BreakAllNodeLinks();
}

FText UK2Node_SingularisGetDeclaredComponent::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
    if (DependencyName.IsNone())
        return LOCTEXT("NodeTitle_None", "获取声明组件");
    return FText::Format(LOCTEXT("NodeTitle", "获取 {0}"), FText::FromName(DependencyName));
}

FText UK2Node_SingularisGetDeclaredComponent::GetTooltipText() const
{
    return LOCTEXT("NodeTooltip", "获取声明绑定的依赖组件实例，输出类型由声明推导");
}

FLinearColor UK2Node_SingularisGetDeclaredComponent::GetNodeTitleColor() const
{
    return FLinearColor(0.4f, 0.8f, 1.0f);
}

void UK2Node_SingularisGetDeclaredComponent::GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
    UClass* ActionClass = GetClass();
    if (!ActionRegistrar.IsOpenForRegistration(ActionClass))
        return;

    UBlueprintNodeSpawner* Spawner = UBlueprintNodeSpawner::Create(ActionClass);
    check(Spawner);
    ActionRegistrar.AddBlueprintAction(ActionClass, Spawner);
}

FText UK2Node_SingularisGetDeclaredComponent::GetMenuCategory() const
{
    return LOCTEXT("MenuCategory", "SingularisCombine|声明");
}

bool UK2Node_SingularisGetDeclaredComponent::IsCompatibleWithGraph(const UEdGraph* TargetGraph) const
{
    if (!Super::IsCompatibleWithGraph(TargetGraph))
        return false;

    UBlueprint* Blueprint = FBlueprintEditorUtils::FindBlueprintForGraph(TargetGraph);
    return Blueprint && Blueprint->ParentClass && Blueprint->ParentClass->IsChildOf(USingularisCombine::StaticClass());
}

#undef LOCTEXT_NAMESPACE
```

- [ ] **Step 3: Self-review**

检查清单：
- [ ] `FindMatchingDeclaration` 跨蓝图所有图按 `DependencyName` 精确匹配。
- [ ] `ValidateNodeDuringCompilation`：名字缺失 / 无匹配声明 / 声明组件类未设置 三重校验，编译期失败而非运行期静默 nullptr。
- [ ] `ExpandNode`：`bIsPureFunc = true`；`SetSelfMemberFunction(USingularisCombine::StaticClass(), GET_FUNCTION_NAME_CHECKED(USingularisCombine, GetDeclaredComponent))` 指向基类 `protected` UFUNCTION，经反射生成调用合法。
- [ ] 输出引脚 `PinType.PinSubCategoryObject = Declaration->ComponentClass` 实现类型推导。
- [ ] `MovePinLinksToIntermediate` 将外部对获取节点输出的连接迁移到真实调用节点输出。
- [ ] 末尾 `BreakAllNodeLinks()` 清理原节点（已展开）。
- [ ] `Scope` 字面量用 `StaticEnum<...>()->GetNameStringByValue`（蓝图枚举字面量以名字串存储）。
- [ ] 文件路径为 `Nodes/`（匹配 Task 6）。
- [ ] 无元注释。

- [ ] **Step 4: Commit**

```bash
git add Plugins/SingularisCombine/Source/SingularisCombineEditor/Public/Nodes/K2Node_SingularisGetDeclaredComponent.h \
        Plugins/SingularisCombine/Source/SingularisCombineEditor/Private/Nodes/K2Node_SingularisGetDeclaredComponent.cpp
git commit -m "feat: 新增获取声明组件 K2Node"
```

---

### Task 8: 全局 `OnBlueprintCompiled` hook 回填 CDO

**Files:**
- Create: `Plugins/SingularisCombine/Source/SingularisCombineEditor/Private/Nodes/SingularisCombineBlueprintCompileHooks.h`
- Create: `Plugins/SingularisCombine/Source/SingularisCombineEditor/Private/Nodes/SingularisCombineBlueprintCompileHooks.cpp`
- Modify: `Plugins/SingularisCombine/Source/SingularisCombineEditor/Private/SingularisCombineEditor.cpp`（注入 hook 注册到现有 Startup/Shutdown，保留 asset type actions 逻辑）

**Interfaces:**
- Consumes: Task 6 `UK2Node_SingularisDeclareDependency`（扫描节点读字段）；Task 2 `friend class FSingularisCombineBlueprintCompileHook;` + 私有 `DeclaredComponents`。
- Produces: `FSingularisCombineBlueprintCompileHook::Register(FDelegateHandle&)` / `Unregister(FDelegateHandle&)`。

- [ ] **Step 1: 创建 hook 头文件**

```cpp
#pragma once

#include <CoreMinimal.h>
#include <UObject/ObjectMacros.h>

class UBlueprint;
class FDelegateHandle;

/**
 * 蓝图编译期 CDO 回填 hook
 * 蓝图编译完成后扫描所有 DeclareDependency 节点，批量写入产物 CDO 的 DeclaredComponents。
 * 经 USingularisCombine 的 friend 授权访问私有 DeclaredComponents。
 */
class FSingularisCombineBlueprintCompileHook
{
public:
    static void Register(FDelegateHandle& OutHandle);
    static void Unregister(FDelegateHandle& Handle);
    static void HandleBlueprintCompiled(UBlueprint* Blueprint);
};
```

- [ ] **Step 2: 创建 hook 实现**

```cpp
#include "Nodes/SingularisCombineBlueprintCompileHooks.h"

#include <EdGraph/EdGraph.h>
#include <Engine/Blueprint.h>
#include <Kismet/BlueprintEditorUtils.h>
#include <KismetCompiler/Public/KismetCompilerInterface.h>

#include "Nodes/K2Node_SingularisDeclareDependency.h"
#include "Objects/SingularisCombineBase.h"
#include "Types/SingularisCombineDependencyList.h"
#include "Types/SingularisCombineDependencyScope.h"

void FSingularisCombineBlueprintCompileHook::Register(FDelegateHandle& OutHandle)
{
    FBlueprintCompiledEventHandler Handler;
    Handler.OnBlueprintCompiled.AddStatic(&FSingularisCombineBlueprintCompileHook::HandleBlueprintCompiled);
    OutHandle = IKismetCompilerInterface::Get().RegisterCompiler(Handler);
}

void FSingularisCombineBlueprintCompileHook::Unregister(FDelegateHandle& Handle)
{
    if (Handle.IsValid())
    {
        IKismetCompilerInterface::Get().UnregisterCompiler(Handle);
        Handle.Reset();
    }
}

void FSingularisCombineBlueprintCompileHook::HandleBlueprintCompiled(UBlueprint* Blueprint)
{
    if (!Blueprint || !Blueprint->GeneratedClass)
        return;

    // 1) 仅处理 USingularisCombine 派生蓝图
    if (!Blueprint->GeneratedClass->IsChildOf(USingularisCombine::StaticClass()))
        return;

    // 2) 扫描所有图中的 DeclareDependency 节点
    TArray<UEdGraph*> AllGraphs;
    FBlueprintEditorUtils::GetAllGraphs(Blueprint, AllGraphs);

    TMap<ESingularisCombineDependencyScope, FSingularisCombineDependencyList> Backfilled;
    for (const UEdGraph* Graph : AllGraphs)
    {
        if (!Graph)
            continue;

        TArray<UK2Node_SingularisDeclareDependency*> Found;
        Graph->GetNodesOfClass<UK2Node_SingularisDeclareDependency>(Found);
        for (const UK2Node_SingularisDeclareDependency* Node : Found)
        {
            if (!Node || !Node->ComponentClass || Node->DependencyName.IsNone())
                continue;
            Backfilled.FindOrAdd(Node->Scope).Classes.AddUnique(Node->ComponentClass);
        }
    }

    // 3) 写入产物 CDO 的私有 DeclaredComponents（friend 授权）
    USingularisCombine* StrategyCDO = Cast<USingularisCombine>(Blueprint->GeneratedClass->ClassDefaultObject);
    if (!StrategyCDO)
        return;

    StrategyCDO->DeclaredComponents = MoveTemp(Backfilled);
}
```

- [ ] **Step 3: 修改 `SingularisCombineEditor.cpp` — 在现有 Startup/Shutdown 中注入 hook，保留 asset type actions 逻辑**

在文件顶部 include 区新增：

```cpp
#include "Nodes/SingularisCombineBlueprintCompileHooks.h"
```

在 `StartupModule` 末尾（现有 asset type actions 注册之后）追加：

```cpp
    FSingularisCombineBlueprintCompileHook::Register(BlueprintCompileHandle);
```

在 `ShutdownModule` 开头（现有 asset type actions 注销之前）追加：

```cpp
    FSingularisCombineBlueprintCompileHook::Unregister(BlueprintCompileHandle);
```

最终 `StartupModule` 完整形态：

```cpp
void FSingularisCombineEditorModule::StartupModule()
{
    IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();

    const EAssetTypeCategories::Type SingularisPluginCategory = AssetTools.RegisterAdvancedAssetCategory(
        FName("Singularis"),
        LOCTEXT("SingularisCategory", "Singularis")
    );

    RegisterAssetTypeAction(
        AssetTools,
        MakeShareable(new FAssetTypeActions_SingularisCombine(SingularisPluginCategory))
    );

    FSingularisCombineBlueprintCompileHook::Register(BlueprintCompileHandle);
}
```

最终 `ShutdownModule` 完整形态：

```cpp
void FSingularisCombineEditorModule::ShutdownModule()
{
    FSingularisCombineBlueprintCompileHook::Unregister(BlueprintCompileHandle);

    if (FModuleManager::Get().IsModuleLoaded("AssetTools"))
    {
        IAssetTools& AssetTools = FModuleManager::GetModuleChecked<FAssetToolsModule>("AssetTools").Get();

        for (auto Action : CreatedAssetTypeActions)
        {
            AssetTools.UnregisterAssetTypeActions(Action.ToSharedRef());
        }
    }

    CreatedAssetTypeActions.Empty();
}
```

- [ ] **Step 4: Self-review**

检查清单：
- [ ] hook 只对 `USingularisCombine` 派生蓝图生效（`GeneratedClass->IsChildOf`）。
- [ ] 扫全部图（`FBlueprintEditorUtils::GetAllGraphs`），含函数图与事件图。
- [ ] 回填幂等：每次编译完全重建 `Backfilled` 再赋值，旧值被覆盖；`AddUnique` 去重。
- [ ] `StrategyCDO->DeclaredComponents = MoveTemp(Backfilled)` 经 Task 2 friend 授权，写入私有字段。
- [ ] `Register` / `Unregister` 配对，`ShutdownModule` 注销避免悬挂委托。
- [ ] 现有 asset type actions 注册/注销逻辑完全保留，hook 注入为追加。
- [ ] **API 待确认项（无 LSP / 无引擎源码可读）**：`FBlueprintCompiledEventHandler::OnBlueprintCompiled` 的确切形式与 `IKismetCompilerInterface::RegisterCompiler` / `UnregisterCompiler` 签名，依据 UE5 `KismetCompiler` 模块公开 API。若实际为虚函数形式（非多播委托成员），改为派生 `class FSingularisCombineBlueprintCompileHook : public FBlueprintCompiledEventHandler { virtual void OnBlueprintCompiled(UBlueprint*) override; }` 并以实例注册；若 `UnregisterCompiler` 签名不同，按引擎头文件调整。此为唯一需在集成期对照引擎头确认的 API 表面。
- [ ] 无元注释。

- [ ] **Step 5: Commit**

```bash
git add Plugins/SingularisCombine/Source/SingularisCombineEditor/Private/Nodes/SingularisCombineBlueprintCompileHooks.h \
        Plugins/SingularisCombine/Source/SingularisCombineEditor/Private/Nodes/SingularisCombineBlueprintCompileHooks.cpp \
        Plugins/SingularisCombine/Source/SingularisCombineEditor/Private/SingularisCombineEditor.cpp
git commit -m "feat: 新增蓝图编译 hook 回填 CDO 声明"
```

---

### Task 9: 集成 self-review

**Files:**
- 无修改，仅逐项核对 `Plugins/SingularisCombine/DESIGN.md`「声明式依赖注入」章节与本计划。

- [ ] **Step 1: DESIGN.md 逐条覆盖核对**

逐条核对清单：
- [ ] 「声明即逻辑、实例即状态」→ `DeclaredComponents` 私有、声明数据类级（注册表/CDO），实例只承载缓存状态（`CachedDependencies`、`CachedDeclaredClasses`）。
- [ ] 「声明集合 == 门控集合 == 可查询集合」→ `ResolveDependencies` 与 `AreDependenciesSatisfied` 均经 `GetDeclaredComponentClasses()` 同源（Task 3）。
- [ ] 「原生类静态注册表」→ Task 1 注册表 + Task 4 宏静态注册器，模块加载期登记。
- [ ] 「蓝图类 CDO 回填」→ Task 6 声明节点 + Task 8 编译 hook 回填 CDO。
- [ ] 「`DeclaredComponents` 收口为 private、去 EditDefaultsOnly」→ Task 2。
- [ ] 「`GetDeclaredComponentClasses()` 虚函数归一」→ Task 2 基类实现。
- [ ] 「类型安全访问器宏」→ Task 4，`Get[Name]()` 返回 `UClass*`，编译期类型校验。
- [ ] 「`GetDeclaredComponent(Scope, Class)` 降为 private/protected」→ Task 2 改为 `protected`（必须可被派生类宏访问器调用，纯 `private` 会阻断宏；此为对设计「private」的必要修正，已记录）。
- [ ] 「C++ 模板便捷版移除」→ Task 2 删除。
- [ ] 「`SetDependencyProvider(Provider)`」→ 未受影响，保留。
- [ ] 「组件层 `CachedDependencies` 纯 C++ 成员」→ 未改（非 UPROPERTY）。
- [ ] 「`ResolveDependencies` 聚合并集」→ Task 3 经 `GetDeclaredComponentClasses()` 聚合。
- [ ] 「`AreDependenciesSatisfied` 空声明 true、缺一 false」→ Task 3 保留语义。
- [ ] 「蓝图体验：声明节点（无输入引脚）+ 获取节点（输出类型推导、编译期绑定）」→ Task 6 / Task 7。

- [ ] **Step 2: 残留旧引用扫描**

用 `grep` 在 `Plugins/SingularisCombine/Source/` 全树搜索以下模式，确认无残留：
- [ ] `Combine->DeclaredComponents`（应为 0 命中）
- [ ] `Strategy->DeclaredComponents`（应为 0 命中）
- [ ] `EditDefaultsOnly` 与 `DeclaredComponents` 同块出现（应为 0 命中）
- [ ] `GetDeclaredComponent<` 模板调用（应为 0 命中，C++ 模板版已删）
- [ ] 在 `SingularisCombine.Build.cs` 中 `BlueprintGraph` / `KismetCompiler`（应为 0 命中，编辑器依赖在 `SingularisCombineEditor.Build.cs`）

- [ ] **Step 3: 类型一致性核对**

- [ ] `GetDeclaredComponentClasses()` 签名在 Task 2（声明+定义）、Task 3（调用）完全一致：返回 `const TMap<ESingularisCombineDependencyScope, FSingularisCombineDependencyList>&`，`const` 方法。
- [ ] `GetDeclaredComponent` 签名在 Task 2、Task 7（`GET_FUNCTION_NAME_CHECKED` 引用）一致。
- [ ] `FSingularisCombineDependencyRegistry::RegisterDependency` / `FindDeclaredClasses` 签名在 Task 1、Task 2、Task 4 一致。
- [ ] `UK2Node_SingularisDeclareDependency` 的 `Scope` / `ComponentClass` / `DependencyName` 字段在 Task 6、Task 7（`FindMatchingDeclaration` 读）、Task 8（hook 读）一致。
- [ ] `FSingularisCombineBlueprintCompileHook::Register(FDelegateHandle&)` / `Unregister(FDelegateHandle&)` 在 Task 8 头/实现/模块调用三处一致。
- [ ] `friend class FSingularisCombineBlueprintCompileHook;`（Task 2 运行时头）与 hook 类名（Task 8）拼写一致。
- [ ] `BlueprintCompileHandle` 成员名在 Task 5（头文件声明）、Task 8（模块 cpp 引用）一致。
- [ ] 文件路径 `Nodes/` 在 Task 6、7、8 一致（不混用 `Blueprint/`）。

- [ ] **Step 4: Commit（如有修正）**

如 Step 1-3 发现任何偏差并修正，提交：

```bash
git add -A
git commit -m "test: 声明式依赖注入集成核对"
```

若无修正，本任务无提交。
