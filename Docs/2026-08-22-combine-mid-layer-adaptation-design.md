# 化合中层适配设计文档

> **架构演进注记（2026-08-23）：** 本文档初次成稿于 2026-08-22，描述了「声明式依赖注入」的原始设计——采用 `DeclaredComponents` CDO 字段 + `ISingularisCombineDependencyProvider` 接口注入 + `friend class FSingularisCombineBlueprintCompileHook` 友元回填三段式。后续审查发现该方案导致 `USingularisCombine` 职责不纯（既持声明数据、又持依赖查询入口），且 friend 反模式破坏封装。
>
> 重构后的最终形态：声明、查询、缓存全部收口于化合组件；`DeclaredComponents` 字段删除，蓝图路径的声明也统一写入全局注册表（`FSingularisCombineDependencyRegistry::ReplaceDeclaredClasses`）；`ISingularisCombineDependencyProvider` 接口与 `BindDependencyProvider`/`SetDependencyProvider` 一并删除；策略类零负担，仅保留化合 SPI。本文档下方正文的「设计意图」仍然有效，但「字段与 API 的具体实现细节」以 `DESIGN.md` 与源码为准。

## 概述

`SingularisCombine` 插件的化合组件最初为"魔法组合"场景设计，偏向上层具体游戏行为。实际在游戏层（中层）大量使用化合将各独立插件（装备、物品栏、搬运、查询等）链接为完整玩法时，发现若干不契合点。本设计在不破坏上层化合用法的前提下，针对中层使用痛点做针对性扩展。

## 痛点分析

| 编号 | 痛点 | 根因 |
|------|------|------|
| 1 | 查询样板：每个策略反复 `GetComponentByClass` + `IsValid` | 策略访问 Avatar 上具体组件时，SPI 仅提供 `Context.Avatar`，需自行查询 |
| 2 | 外部入参缺失：评估无法接收事件参数 | 化合为"被动标签驱动"模型，无事件载荷通道 |
| 3 | 评估时机：仅有周期轮询，无法即时响应 | 缺少主动触发评估的 API |

痛点 2 与 3 同源——缺少"事件驱动评估"通道。痛点 1 相对独立，是"策略↔组件"的访问问题。

## 方案选择

采用混合方案：

- 查询：声明式依赖注入
- 事件驱动：`TriggerEvaluate` API + 独立 Payload 形参
- Context 与 Payload 分离：参考 CHANT 项目的 `AbilityContext`（身份引用）/ `TransientPayload`（瞬态数据）分离设计

不新增中层基类，改动集中在现有文件。上层化合用法完全不变。

## 架构概览

改动文件清单：

| 文件 | 改动 |
|------|------|
| `FSingularisCombineContext`（上下文） | 保持现有身份引用字段不变，不承载事件载荷 |
| `FSingularisCombineTransientPayload`（瞬态负荷，新增） | 新建 USTRUCT，承载事件标识 + 结构化数据 |
| `ESingularisCombineDependencyScope`（枚举，新增） | 标识依赖归属的 Context Actor（Instigator / Avatar / Target） |
| `FSingularisCombineDependencyList`（USTRUCT，新增） | 包装 `TArray<TSubclassOf<UActorComponent>>` 作为注册表 TMap 值类型 |
| `FSingularisCombineDependencyRegistry`（注册表，重构后为唯一真相源） | 进程级单例，原生宏路径与蓝图编译 hook 路径统一写入；新增 `ReplaceDeclaredClasses` 整体替换语义保证编译幂等 |
| `USingularisCombine`（策略基类） | SPI 形参新增 Payload；不持有任何声明数据与依赖查询入口（重构后零负担） |
| `USingularisCombineComponent`（化合组件） | 增加 `TriggerEvaluate(Payload)` API；承担声明依赖的单一职责：登记、查询、缓存全部收口于本组件；新增 `GetDeclaredComponentClasses(StrategyClass)` / `GetDeclaredComponent(Scope, Class)` / `GetFromStrategy(Strategy)` 静态工厂；维护 `CachedDependencies` 缓存 + `ResolveDependencies(Context)` + `AreDependenciesSatisfied(Strategy, Context)`；Context 构建保持纯粹身份 |
| `SingularisCombine.Build.cs` | 无需改动（`FInstancedStruct` 在 UE 5.8 中已内置 `CoreUObject` 模块） |

## Context 与 Payload 分离

参考 CHANT 项目 `ability.rs` 的设计：`Ability::execute(ctx, payload)` 将身份引用（`AbilityContext`）与瞬态数据（`TransientPayload`）作为两个独立形参传递。化合采纳同一思想：

- **Context 承载稳定的场景身份**：`Instigator` / `Avatar` / `Target` / `CombineComponent`，贯穿整个评估周期，不携带事件数据
- **Payload 承载瞬态的事件载荷**：每次 `TriggerEvaluate` 触发时构造，仅本次评估有效，可空
- **BlackboardTags / NativeTags 承载黑板状态**：周期刷新的环境标签

三者职责清晰分离，避免 Context 同时承载稳定身份与瞬态数据违反单一职责。

## 查询机制：声明式依赖 + 辅助函数

声明式依赖注入采用“策略声明、注册表汇总、组件缓存”的分层设计：

- **声明层**：策略仅需在类体调用 `SINGULARIS_DECLARE_DEPENDENCY(Scope, UClass, Name)` 宏（C++ 路径）或在蓝图内放置 `DeclareDependency` 节点（蓝图路径）声明依赖
- **注册表层**：`FSingularisCombineDependencyRegistry` 进程级单例作为唯一真相源，原生宏静态注册器与蓝图编译 hook 同源写入
- **组件层（`USingularisCombineComponent`）**：负责按 Context 收集 Actor 依赖、维护缓存、做前置满足性检查；策略类不持有任何声明数据与依赖查询入口（依赖倒置，单向依赖）

策略保持轻量（无运行时缓存状态），声明登记、缓存与解析全部归化合组件所有，与 `EvaluatePipeline` / `TickComponent` 的生命周期一致。

### 结构化声明：作用域 + 类型列表

依赖声明由两部分组成（均位于 `Public/Types/` 下独立头文件）：

```cpp
// 作用域枚举：标识依赖归属的 Context Actor
UENUM(BlueprintType)
enum class ESingularisCombineDependencyScope : uint8
{
    Instigator,  // Context.Instigator
    Avatar,      // Context.Avatar（通常为挂载化合组件的 Actor）
    Target,      // Context.Target
};

// 单作用域下的依赖类型列表（USTRUCT 包装，作为注册表 TMap 值类型）
USTRUCT(BlueprintType)
struct FSingularisCombineDependencyList
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TArray<TSubclassOf<UActorComponent>> Classes{};
};
```

> **UHT 限制说明（历史背景）：** UE 反射系统不支持 `TMap<K, TArray<TSubclassOf<UObject>>>` 作为 UPROPERTY。原始设计使用 `FSingularisCombineDependencyList` USTRUCT 包装 `TArray` 作为 Map 值，是 UE 反射地道的等价物。重构后该 USTRUCT 作为注册表内部存储类型保留，策略类不再持有此字段实例。

### 注册表写入（唯一真相源）

```cpp
// FSingularisCombineDependencyRegistry: 进程级单例（位于 Public/Types/）
// 原生宏静态注册器于模块加载期调用，蓝图编译 hook 于 CDO 编译完成后调用

// 原生宏路径（追加单条声明，幂等去重）
void RegisterDependency(UClass* StrategyClass, ESingularisCombineDependencyScope Scope, TSubclassOf<UActorComponent> ComponentClass);

// 蓝图编译 hook 路径（整体替换，保证反复编译幂等）
void ReplaceDeclaredClasses(UClass* StrategyClass, TMap<ESingularisCombineDependencyScope, FSingularisCombineDependencyList> InDeclared);

// 评估期只读查询
const TMap<ESingularisCombineDependencyScope, FSingularisCombineDependencyList>* FindDeclaredClasses(UClass* StrategyClass) const;
```

示例：策略声明 `{ Avatar: [UEquipmentComponent], Instigator: [UPlayerControllerComp] }` 表示 Avatar 上必须有 `UEquipmentComponent` 且 Instigator 上必须有 `UPlayerControllerComp`，两者均存在时才允许进入 `CanReaction`。

### 组件层依赖缓存

缓存与解析函数均位于 `USingularisCombineComponent`：

```cpp
// USingularisCombineComponent: 私有缓存（位于 Internal Variable region）
// 注：非 UPROPERTY，纯 C++ 运行时成员（评估期内部使用）
TMap<ESingularisCombineDependencyScope, TMap<TSubclassOf<UActorComponent>, TWeakObjectPtr<UActorComponent>>> CachedDependencies{};

// 声明查询（接收策略类，沿继承链聚合注册表声明）
TMap<ESingularisCombineDependencyScope, FSingularisCombineDependencyList> GetDeclaredComponentClasses(UClass* StrategyClass) const;

// 解析入口：每次评估前刷新缓存
void ResolveDependencies(const FSingularisCombineContext& Context);

// 前置预检：声明依赖是否全部命中缓存
bool AreDependenciesSatisfied(const USingularisCombine* Strategy, const FSingularisCombineContext& Context) const;

// 缓存查询（BlueprintPure + DeterminesOutputType，供蓝图与宏访问器调用）
UFUNCTION(BlueprintPure, meta = (DeterminesOutputType = "ComponentClass"))
UActorComponent* GetDeclaredComponent(ESingularisCombineDependencyScope Scope, TSubclassOf<UActorComponent> ComponentClass) const;

// 静态工厂：从策略实例反查所属化合组件（供声明节点 ExpandNode 与宏访问器使用）
UFUNCTION(BlueprintPure)
static USingularisCombineComponent* GetFromStrategy(const USingularisCombine* Strategy);
```

`ResolveDependencies(Context)` 流程：

1. `Reset()` 缓存
2. 聚合管线所有策略的声明并集，按作用域分组为 `TMap<Scope, TSet<Class>>`（经 `GetDeclaredComponentClasses(Strategy->GetClass())` 查询）
3. 逐作用域调用 `GetContextActor(Context, Scope)` 映射到 Context 中的 Actor（Instigator / Avatar / Target）
4. 对每个作用域的 Actor 调用 `GetComponentByClass(Class)` 查找，并写入该作用域对应的缓存槽位（`FindOrAdd` 方式）
5. 缓存槽位与声明结构一一对应：查询 `GetDeclaredComponent(Scope, Class)` 直接按作用域定位，同一 Actor 出现在多个作用域时各作用域槽位独立缓存

调用时机：

- `EvaluatePipeline` 在 Context 构建后、策略循环前调用一次
- `TickComponent` 在 Context 构建后调用一次，保证 `SustainReaction` 中 `GetDeclaredComponent` 可用
- 组件动态增删后下一周期 `ResolveDependencies` 自动校正；`OnOwnerChildComponentConstructed` 回调触发即时重估，保证时效性

### 前置预检（CanReaction 前置条件）

`AreDependenciesSatisfied(Strategy, Context)` 是 `CanReaction` 的强约束前置：

- **空声明 → 返回 true**：视为无条件满足，正常进入 `CanReaction`
- **任一声明依赖缺失 → 返回 false**：`CanReaction` 不被调用；若策略原处于激活态，则直接走 `ReactionRevert` 回滚路径

```cpp
// EvaluatePipeline 中的策略评估主循环（简化）
ResolveDependencies(Context);

for (FSingularisCombineEntry& Entry : CombinePipeline.Combines)
{
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
    // 依赖已就绪：CanReaction / Reaction / ReactionRevert 内可假定声明依赖存在
    if (Combine->CanReaction(Context, Payload, BlackboardTags, NativeBlackboardTags))
        ...
}
```

**契约**：当 `CanReaction` / `Reaction` / `ReactionRevert` 被执行时，已声明依赖保证存在（缓存命中且 `WeakPtr` 有效）。策略无需再次判空。

### 查询函数

化合组件暴露 BlueprintPure 函数：

```cpp
// USingularisCombineComponent: 缓存查询入口（BlueprintPure + DeterminesOutputType）
UFUNCTION(BlueprintPure, Category = "SingularisCombine|引力奇点化合组件|State", meta = (DisplayName = "获取声明组件", DeterminesOutputType = "ComponentClass"))
UActorComponent* GetDeclaredComponent(ESingularisCombineDependencyScope Scope, TSubclassOf<UActorComponent> ComponentClass) const;

// 静态工厂：从策略实例反查所属化合组件
UFUNCTION(BlueprintPure, Category = "SingularisCombine|引力奇点化合组件|API", meta = (DisplayName = "GetFromStrategy"))
static USingularisCombineComponent* GetFromStrategy(const USingularisCombine* Strategy);
```

- `GetDeclaredComponent(Scope, Class)`：必须在 `ResolveDependencies(Context)` 之后调用，从 `CachedDependencies` 查询。蓝图侧通过 `DeterminesOutputType = "ComponentClass"` 使节点输出引脚类型随 `ComponentClass` 输入自动推导，免 Cast 节点。
- `GetFromStrategy(Strategy)`：通过 `Strategy->GetOuter()` 反查所属化合组件。供声明节点 `ExpandNode` 生成两步调用链，也供 C++ 宏访问器内部使用。CDO 返回 `nullptr`。
- **不再有提供者注入**：重构后不再有 `ISingularisCombineDependencyProvider` 接口、`SetDependencyProvider`/`BindDependencyProvider` 函数与 `TWeakInterfacePtr` 持有。化合组件在 `BeginPlay`/`SetPipeline`/`AddCombineEntry` 无需任何注入动作，策略类不知晓组件存在。

### C++ 宏访问器

```cpp
// SINGULARIS_DECLARE_DEPENDENCY(Scope, UClassType, Name) 展开为：
UClassType* Get##Name() const
{
    USingularisCombineComponent* const OwnerComponent = USingularisCombineComponent::GetFromStrategy(this);
    return Cast<UClassType>(
        OwnerComponent ? OwnerComponent->GetDeclaredComponent(
            ESingularisCombineDependencyScope::Scope, UClassType::StaticClass())
                      : nullptr);
}
// + 静态注册器（模块加载期向 Registry 登记）
```

声明与获取绑定在同一处，编译期类型校验，不可绕过。

### 蓝图体验对比

| 方式 | 节点数 | 说明 |
|------|--------|------|
| 现状 | 4-5 | Get Outer → Cast → Get Owner → Get Component by Class → Is Valid |
| 声明式（蓝图） | 1 | 编辑器配置依赖类 → 输出组件引用；`DeterminesOutputType` 自动推导输出类型，免 Cast |
| 声明式（C++） | 1 | `Get[Name]()` 宏访问器自动转换返回类型 |

蓝图节点 `DeclareDependency` 表面上是单节点，编译期 `ExpandNode` 展开为两步调用链：

1. `USingularisCombineComponent::GetFromStrategy(self)` → 输出所属化合组件引用
2. `USingularisCombineComponent::GetDeclaredComponent(Scope, Class)` → 输出组件引用

输出引脚类型由节点上配置的组件类实时推导（`DeterminesOutputType`），用户无需手动连线两步调用。

## 瞬态负荷（Payload）

### 数据结构

新建 USTRUCT `FSingularisCombineTransientPayload`：

```cpp
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

### 类型选择依据

采用 `FInstancedStruct` 承载结构化事件数据：

- 强类型任意 struct，序列化友好，蓝图原生支持（`BlueprintInstancedStructLibrary` 提供 `Make/Get/Set Instanced Struct` 等蓝图节点）
- 蓝图操作类似 Cast：`Get Instanced Struct Value` 节点指定期望类型，Valid/Invalid 引脚处理类型匹配
- 是 CHANT `Dictionary<Variant, Variant>` 在 UE 里的地道等价物，比 `TMap<FName, FVariant>` 更强类型、序列化更友好
- UE 5.8 中 `FInstancedStruct` 已内置在 `CoreUObject` 模块中，无需新增模块依赖

### 触发来源区分

- 周期轮询 / 组件构造回调触发：构造空 `FSingularisCombineTransientPayload`（`EventTag` 为空、`EventData` 为空）
- `TriggerEvaluate` 触发：携带事件标识与附加数据，贯穿本次评估所有策略

策略可在 `CanReaction` / `Reaction` 中读取 `Payload.EventTag` 判断触发来源，决定是否响应特定事件。

## 事件驱动评估

### TriggerEvaluate API

`USingularisCombineComponent` 新增：

```cpp
UFUNCTION(
    BlueprintCallable,
    BlueprintAuthorityOnly,
    Category = "SingularisCombine|引力奇点化合组件|API",
    meta = (DisplayName = "TriggerEvaluate")
)
void TriggerEvaluate(const FSingularisCombineTransientPayload& Payload);
```

行为：

- 立即触发一次 `EvaluatePipeline`，不等轮询周期
- 将 `Payload` 贯穿本次评估所有策略的 SPI 调用
- `BlueprintAuthorityOnly`：与现有 `EvaluatePipeline` 的服务端权威一致

### 载荷传递

`USingularisCombineComponent` 新增 `Transient` 内部变量 `PendingPayload`，`TriggerEvaluate` 写入后调用 `EvaluatePipeline`；`EvaluatePipeline` 构建 SPI 调用时读取该变量，评估完成后清空。周期轮询与构造回调触发时该变量为空，构造的 Payload 自然为空。

## SPI 签名变更

所有 SPI 方法新增 `Payload` 形参（与 CHANT `execute(ctx, payload)` 设计一致）：

```cpp
// CanReaction：现状无 Context，变更为同时接收 Context + Payload
bool CanReaction(
    const FSingularisCombineContext& Context,
    const FSingularisCombineTransientPayload& Payload,
    const FGameplayTagContainer& BlackboardGameplayTags,
    const TArray<FName>& BlackboardNativeTags
) const;

// Reaction / ReactionRevert / SustainReaction：在现有 Context 形参后追加 Payload
void Reaction(
    const FSingularisCombineContext& Context,
    const FSingularisCombineTransientPayload& Payload,
    const FGameplayTagContainer& BlackboardGameplayTags,
    const TArray<FName>& BlackboardNativeTags
);

void ReactionRevert(
    const FSingularisCombineContext& Context,
    const FSingularisCombineTransientPayload& Payload,
    const FGameplayTagContainer& BlackboardGameplayTags,
    const TArray<FName>& BlackboardNativeTags
);

void SustainReaction(
    const FSingularisCombineContext& Context,
    const FSingularisCombineTransientPayload& Payload,
    const FGameplayTagContainer& BlackboardGameplayTags,
    const TArray<FName>& BlackboardNativeTags,
    float DeltaTime
);
```

变更影响：

- `CanReaction` 是 `BlueprintNativeEvent` 签名变更，现有蓝图策略需重新连线（新增 Context + Payload 输入引脚）
- `Reaction` / `ReactionRevert` / `SustainReaction` 已接收 Context，新增 Payload 引脚需重新连线
- `USingularisCombineComponent::EvaluatePipeline` 中调用所有 SPI 的位置需传入 Payload
- `TickComponent` 中调用 `SustainReaction` 时传入空 Payload（无事件语义）

## 数据流

### 触发来源

- 周期轮询定时器（`AutoEvaluateInterval`）
- 组件构造回调（`OnOwnerChildComponentConstructed` → 合并定时器）
- `TriggerEvaluate(Payload)`（事件驱动）

### EvaluatePipeline 执行顺序

1. `CollectAllTags()`：所有端刷新黑板（GameplayTags + 原生 FName 标签）
2. 标签变更检测，变更时广播 `OnCombineBlackboardUpdatedEvent`（所有端）
3. 服务端权威闸门：非服务端提前 return，后续步骤仅服务端执行
4. 构建 `Context`（Instigator / Avatar / Target / CombineComponent）与 `Payload` 局部快照（读取 `PendingPayload`，周期轮询时为空）
5. 调用 `ResolveDependencies(Context)`（化合组件层）预解析管线所有策略声明的组件依赖，按 Context 的三个 Actor 分别缓存到 `CachedDependencies`
6. 策略评估循环：逐策略调用 `AreDependenciesSatisfied(Strategy, Context)` 前置预检（不满足则回滚、不进入 `CanReaction`）→ 满足时评估 `CanReaction(Context, Payload, Tags, NativeTags)` → `Reaction` / `ReactionRevert`

> **注**：`PendingPayload` 的清空发生在 `TriggerEvaluate` 中 `EvaluatePipeline` 返回之后（`SingularisCombineComponent.cpp:231`），不属于 `EvaluatePipeline` 本身。`TickComponent` 中 `SustainReaction` 传入空 Payload（`SingularisCombineComponent.cpp:103-110`）是独立于 `EvaluatePipeline` 的逐帧流程。

### Context 构建

- `FSingularisCombineContext` 保持现有四字段（`Instigator` / `Avatar` / `Target` / `CombineComponent`）不变
- 不在 Context 中承载事件数据，事件载荷统一通过 `FSingularisCombineTransientPayload` 形参传递

## 错误处理与边界

1. 依赖查找失败：`ResolveDependencies(Context)` 找不到组件时对应作用域槽位留空；策略经宏访问器或声明节点调用 `GetDeclaredComponent(Scope, Class)` 返回 `nullptr`。注意策略评估主循环中已通过 `AreDependenciesSatisfied` 前置预检保证 `CanReaction` / `Reaction` 不在依赖缺失时执行，此处 `nullptr` 仅见于 `SustainReaction` 等绕过预检的路径，或策略声明变更后尚未触发重估的窗口。注：`TickComponent` 中 `Context.Target = nullptr`，故 `SustainReaction` 路径无法解析 Target 作用域依赖；声明 Target 作用域依赖仅对 `EvaluatePipeline`（服务端，`Context.Target = OwnerActor`）路径生效。
2. 依赖注入丢失：重构后已不存在提供者注入路径，`BindDependencyProvider`/`SetDependencyProvider` 删除。声明查询经 `GetFromStrategy` 静态工厂实时反查 `Outer`，无需生命周期管理；若策略实例未被挂载于化合组件上（`Outer` 非组件实例），`GetFromStrategy` 返回 `nullptr`，宏访问器返回 `nullptr`，蓝图节点展开后的两步调用在编译期可能产生 `nullptr` 访问，需在使用前判空。
3. 缓存失效：`TWeakObjectPtr` 在组件销毁后自动失效；下一评估周期 `ResolveDependencies(Context)` 重新填充；`AreDependenciesSatisfied` 的预检会捕获该失效并将策略回滚
4. Payload 幂等性：现有"激活才 Reaction、未激活才 Revert"的幂等机制已保证不会因 Payload 重复触发副作用；策略应综合 Payload.EventTag + 黑板判断
5. Payload 类型不匹配：`FInstancedStruct` 的蓝图 `Get Instanced Struct Value` 节点提供 Valid/Invalid 引脚，类型不匹配走 Invalid 分支，不报错；C++ 侧 `GetPtr<T>()` 返回 `nullptr`，策略需判空
6. 网络语义：`Payload` 仅在触发端本次评估有效，不复制；服务端 `TriggerEvaluate` 的 Payload 贯穿服务端策略评估，客户端周期轮询时 Payload 为空——符合"Reaction 服务端权威"的现有模型

## 测试策略

项目无 LSP，化合策略在蓝图实现，验证依赖：

1. 编译验证：C++ 改动通过项目编译，`Build.cs` 无需改动（`FInstancedStruct` 在 `CoreUObject` 中）
2. 依赖注入验证：测试蓝图策略放置 `DeclareDependency` 节点（配置作用域与组件类，如 `{ Avatar: [UEquipmentComponent], Instigator: [UPlayerControllerComp] }`），验证编译后注册表记录正确、`GetDeclaredComponent(作用域, Class)` 返回正确引用、组件增删后缓存自动校正、`AreDependenciesSatisfied` 在缺依赖时阻止 `CanReaction` 执行；验证 `GetFromStrategy(self)` 反查逻辑（策略实例被挂载于组件 vs 未挂载于组件）。
3. 事件触发验证：调用 `TriggerEvaluate`，验证策略读取到 Payload.EventTag 且即时评估（不等轮询周期）
4. Payload 传递验证：测试 `FInstancedStruct` 在蓝图中的 Make/Get 流程，验证策略正确读取结构化数据
5. 回归验证：现有上层化合策略（无依赖配置、不读 Payload）行为完全不变；SPI 签名变更后蓝图需重新连线

## 迁移影响

- `CanReaction` / `Reaction` / `ReactionRevert` / `SustainReaction` 签名变更：所有现有蓝图策略需更新输入引脚连线（新增 Payload 引脚，`CanReaction` 额外新增 Context 引脚）
- `FSingularisCombineContext`：保持不变，向后兼容
- 新增 `FSingularisCombineTransientPayload` USTRUCT：新建文件
- 新增 API 与字段：可选使用，未声明任何依赖的策略行为不变
- 重构后策略类零负担：`DeclaredComponents`/`GetDeclaredComponent`/`GetDeclaredComponentClasses`/`SetDependencyProvider`/`DependencyProvider`/`friend` 全部移出 `USingularisCombine`，声明与查询职责完全收口于化合组件。使用旧版本接口的代码需迁移：策略层 SPI 形参不变，但 `GetDeclaredComponent` 调用路径变为经 `GetFromStrategy` 反查后委托
- `Build.cs`：无需改动（`FInstancedStruct` 在 UE 5.8 中已内置 `CoreUObject` 模块）

## 文件改动清单

- `Plugins/SingularisCombine/Source/SingularisCombine/Public/Types/SingularisCombineTransientPayload.h`：定义 `FSingularisCombineTransientPayload`，include `CoreUObject` 的 `StructUtils/InstancedStruct.h`
- `Plugins/SingularisCombine/Source/SingularisCombine/Public/Types/SingularisCombineDependencyScope.h`：定义 `ESingularisCombineDependencyScope` 枚举（Instigator / Avatar / Target）
- `Plugins/SingularisCombine/Source/SingularisCombine/Public/Types/SingularisCombineDependencyList.h`：定义 `FSingularisCombineDependencyList` USTRUCT，包装 `TArray<TSubclassOf<UActorComponent>>` 作为注册表 TMap 值类型
- `Plugins/SingularisCombine/Source/SingularisCombine/Public/Types/SingularisCombineDependencyRegistry.h`：进程级单例，新增 `ReplaceDeclaredClasses` 整体替换语义保证编译幂等；原生宏路径与蓝图编译 hook 路径统一写入此处
- `Plugins/SingularisCombine/Source/SingularisCombine/Public/Objects/SingularisCombineBase.h`：SPI 签名新增 Payload 形参；策略类零负担，不持有任何声明数据与依赖查询入口
- `Plugins/SingularisCombine/Source/SingularisCombine/Private/Objects/SingularisCombineBase.cpp`：SPI 实现新增 Payload；删除原声明查询实现（已迁移至组件）
- `Plugins/SingularisCombine/Source/SingularisCombine/Public/Components/SingularisCombineComponent.h`：新增 `TriggerEvaluate` 声明、`PendingPayload` 内部变量；新增 `GetDeclaredComponentClasses(StrategyClass)` / `GetDeclaredComponent(Scope, Class)` BlueprintPure / `GetFromStrategy(Strategy)` 静态工厂；维护 `CachedDependencies`（按作用域分组的嵌套 TMap，纯 C++）、`ResolveDependencies(Context)`、`AreDependenciesSatisfied(Strategy, Context)`、`GetContextActor`
- `Plugins/SingularisCombine/Source/SingularisCombine/Private/Components/SingularisCombineComponent.cpp`：实现 `TriggerEvaluate`、`EvaluatePipeline` 增加 `ResolveDependencies(Context)` 与 `AreDependenciesSatisfied` 前置预检、Payload 传递；`TickComponent` 增加 `ResolveDependencies(Context)`；实现 `GetDeclaredComponentClasses` / `ResolveDependencies` / `AreDependenciesSatisfied` / 组件层 `GetDeclaredComponent` / `GetFromStrategy` / `GetContextActor`
- `Plugins/SingularisCombine/Source/SingularisCombine/Public/SingularisCombineDeclarativeMacros.h`：宏访问器展开为 `GetFromStrategy(this)->GetDeclaredComponent(Scope, Class)`，声明与查询分离，策略不直接持有依赖查询入口
- `Plugins/SingularisCombine/Source/SingularisCombineEditor/Private/Nodes/K2Node_SingularisDeclareDependency.cpp`：`ExpandNode` 展开为两步调用链（`GetFromStrategy(self)` → `GetDeclaredComponent(Scope, Class)`），用 `GET_FUNCTION_NAME_CHECKED` 标准反射取代原 `FindFunctionByName` 降级写法
- `Plugins/SingularisCombine/Source/SingularisCombineEditor/Private/Nodes/SingularisCombineBlueprintCompileHooks.cpp`：hook 改为调用 `FSingularisCombineDependencyRegistry::ReplaceDeclaredClasses` 写入全局注册表，不再依赖 `friend` 访问策略类私有字段

## 设计来源

Context 与 Payload 分离的设计思想参考 CHANT 项目（Godot 4.7 + Rust GDExtension）的 `Ability::execute(ctx, payload)` 模式：

- `AbilityContext`（`ability.rs`）：身份引用（instigator / avatar / target）
- `TransientPayload`（`payload.rs`）：瞬态数据容器（`Dictionary<Variant, Variant>`）
- `execute` 签名将两者作为独立形参传递

化合将此模式 UE 化适配：

- `FSingularisCombineContext` 对应 `AbilityContext`（身份引用）
- `FSingularisCombineTransientPayload` 对应 `TransientPayload`（瞬态数据），用 `FInstancedStruct` 替代 `Dictionary<Variant, Variant>`
- SPI 形参 `(Context, Payload, Tags, NativeTags)` 对应 `execute(ctx, payload)`
