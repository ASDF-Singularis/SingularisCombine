# 化合中层适配设计文档

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

- 查询：声明式依赖注入（主）+ 辅助函数（兜底）
- 事件驱动：`TriggerEvaluate` API + 独立 Payload 形参
- Context 与 Payload 分离：参考 CHANT 项目的 `AbilityContext`（身份引用）/ `TransientPayload`（瞬态数据）分离设计

不新增中层基类，改动集中在现有文件。上层化合用法完全不变。

## 架构概览

改动文件清单：

| 文件 | 改动 |
|------|------|
| `FSingularisCombineContext`（上下文） | 保持现有身份引用字段不变，不承载事件载荷 |
| `FSingularisCombineTransientPayload`（瞬态负荷，新增） | 新建 USTRUCT，承载事件标识 + 结构化数据 |
| `USingularisCombine`（策略基类） | SPI 形参新增 Payload；增加声明式依赖配置 + 依赖缓存 + 依赖解析 + 查询辅助函数 |
| `USingularisCombineComponent`（化合组件） | 增加 `TriggerEvaluate(Payload)` API；评估前调用策略依赖解析；Context 构建保持纯粹身份 |
| `SingularisCombine.Build.cs` | 无需改动（`FInstancedStruct` 在 UE 5.8 中已内置 `CoreUObject` 模块） |

## Context 与 Payload 分离

参考 CHANT 项目 `ability.rs` 的设计：`Ability::execute(ctx, payload)` 将身份引用（`AbilityContext`）与瞬态数据（`TransientPayload`）作为两个独立形参传递。化合采纳同一思想：

- **Context 承载稳定的场景身份**：`Instigator` / `Avatar` / `Target` / `CombineComponent`，贯穿整个评估周期，不携带事件数据
- **Payload 承载瞬态的事件载荷**：每次 `TriggerEvaluate` 触发时构造，仅本次评估有效，可空
- **BlackboardTags / NativeTags 承载黑板状态**：周期刷新的环境标签

三者职责清晰分离，避免 Context 同时承载稳定身份与瞬态数据违反单一职责。

## 查询机制：声明式依赖 + 辅助函数

### 声明式依赖（主通道）

`USingularisCombine` 新增编辑器配置字段与运行时缓存：

```cpp
// 编辑器配置：策略声明的依赖组件类型（位于 Parameter region）
UPROPERTY(
    EditDefaultsOnly,
    BlueprintReadOnly,
    Category = "SingularisCombine|引力奇点化合|参数",
    meta = (DisplayName = "组件依赖")
)
TArray<TSubclassOf<UActorComponent>> ComponentDependencies{};

// 运行时缓存：每次评估前由依赖解析刷新（位于 private Internal Variable region）
// 降级说明：UHT 拒绝 TMap<...,TWeakObjectPtr<...>> 的 BlueprintReadOnly 暴露，且拒绝无 Edit/BP specifier 的 Category。
// 因此简为 UPROPERTY(Transient)，Blueprint 访问通过 GetDependency 函数保留语义。
UPROPERTY(Transient)
TMap<TSubclassOf<UActorComponent>, TWeakObjectPtr<UActorComponent>> CachedDependencies{};
```

依赖解析流程：

- `USingularisCombineComponent::EvaluatePipeline` 在 `CollectAllTags` 之后、策略评估之前，逐策略调用 `ResolveDependencies()`
- `ResolveDependencies()` 沿 Outer 链（`GetOuter()` → `USingularisCombineComponent` → `GetOwner()`）获取 `OwnerActor`，遍历 `ComponentDependencies` 查找组件并填充 `CachedDependencies`
- 缓存每评估周期刷新一次；组件动态增删后下一周期自动校正
- 现有 `OnOwnerChildComponentConstructed` 回调触发即时重估，保证组件增删后缓存时效性

策略通过蓝图函数获取预缓存引用：

```cpp
UFUNCTION(
    BlueprintPure,
    Category = "SingularisCombine|引力奇点化合|State",
    meta = (DisplayName = "GetDependency")
)
UActorComponent* GetDependency(TSubclassOf<UActorComponent> ComponentClass) const;
```

### 辅助函数（兜底）

`USingularisCombine` 新增蓝图函数，用于未声明依赖的临时/动态查询：

```cpp
UFUNCTION(
    BlueprintPure,
    Category = "SingularisCombine|引力奇点化合|State",
    meta = (DisplayName = "GetAvatarComponent")
)
UActorComponent* GetAvatarComponent(TSubclassOf<UActorComponent> ComponentClass) const;
```

- 与 `GetDependency` 的区别：无需在编辑器预先声明，每次调用即时查找

### 蓝图体验对比

| 方式 | 节点数 | 说明 |
|------|--------|------|
| 现状 | 4-5 | Get Outer → Cast → Get Owner → Get Component by Class → Is Valid |
| 声明式 | 2 | 编辑器配置依赖类 → `GetDependency(Class)` + 一次 Cast |
| 兜底 | 2 | `GetAvatarComponent(Class)` + 一次 Cast |

蓝图函数返回 `UActorComponent*`，受蓝图不支持模板限制，需一次 Cast 到具体类型。

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

1. `CollectAllTags()`：刷新黑板（GameplayTags + 原生 FName 标签）
2. 新增：遍历管线策略，调用 `ResolveDependencies()` 预解析组件依赖
3. 标签变更检测，变更时广播 `OnCombineBlackboardUpdatedEvent`
4. 服务端权威：构建空 Payload（或读取 `PendingPayload`）→ 逐策略评估 `CanReaction(Context, Payload, Tags, NativeTags)` → `Reaction` / `ReactionRevert`
5. 评估完成后清空 `PendingPayload`
6. `TickComponent` 中 `SustainReaction` 传入空 Payload

### Context 构建

- `FSingularisCombineContext` 保持现有四字段（`Instigator` / `Avatar` / `Target` / `CombineComponent`）不变
- 不在 Context 中承载事件数据，事件载荷统一通过 `FSingularisCombineTransientPayload` 形参传递

## 错误处理与边界

1. 依赖查找失败：`ResolveDependencies` 找不到组件时对应槽位留空；`GetDependency` 返回 `nullptr`，策略自行降级处理
2. 缓存失效：`TWeakObjectPtr` 在组件销毁后自动失效；下一评估周期 `ResolveDependencies` 重新填充
3. Payload 幂等性：现有"激活才 Reaction、未激活才 Revert"的幂等机制已保证不会因 Payload 重复触发副作用；策略应综合 Payload.EventTag + 黑板判断
4. Payload 类型不匹配：`FInstancedStruct` 的蓝图 `Get Instanced Struct Value` 节点提供 Valid/Invalid 引脚，类型不匹配走 Invalid 分支，不报错；C++ 侧 `GetPtr<T>()` 返回 `nullptr`，策略需判空
5. 网络语义：`Payload` 仅在触发端本次评估有效，不复制；服务端 `TriggerEvaluate` 的 Payload 贯穿服务端策略评估，客户端周期轮询时 Payload 为空——符合"Reaction 服务端权威"的现有模型

## 测试策略

项目无 LSP，化合策略在蓝图实现，验证依赖：

1. 编译验证：C++ 改动通过项目编译，`Build.cs` 无需改动（`FInstancedStruct` 在 `CoreUObject` 中）
2. 依赖注入验证：测试蓝图策略配置 `ComponentDependencies`，验证 `GetDependency` 返回正确引用、组件增删后缓存自动校正
3. 事件触发验证：调用 `TriggerEvaluate`，验证策略读取到 Payload.EventTag 且即时评估（不等轮询周期）
4. Payload 传递验证：测试 `FInstancedStruct` 在蓝图中的 Make/Get 流程，验证策略正确读取结构化数据
5. 回归验证：现有上层化合策略（无依赖配置、不读 Payload）行为完全不变；SPI 签名变更后蓝图需重新连线

## 迁移影响

- `CanReaction` / `Reaction` / `ReactionRevert` / `SustainReaction` 签名变更：所有现有蓝图策略需更新输入引脚连线（新增 Payload 引脚，`CanReaction` 额外新增 Context 引脚）
- `FSingularisCombineContext`：保持不变，向后兼容
- 新增 `FSingularisCombineTransientPayload` USTRUCT：新建文件
- 新增 API 与字段：可选使用，不配置 `ComponentDependencies` 的策略行为不变
- `Build.cs`：无需改动（`FInstancedStruct` 在 UE 5.8 中已内置 `CoreUObject` 模块）

## 文件改动清单

- `Plugins/SingularisCombine/Source/SingularisCombine/Public/Types/SingularisCombineTransientPayload.h`（新建）：定义 `FSingularisCombineTransientPayload`，include `CoreUObject` 的 `StructUtils/InstancedStruct.h`
- `Plugins/SingularisCombine/Source/SingularisCombine/Public/Objects/SingularisCombineBase.h`：SPI 签名新增 Payload 形参；新增依赖配置、缓存、解析函数、查询函数
- `Plugins/SingularisCombine/Source/SingularisCombine/Private/Objects/SingularisCombineBase.cpp`：SPI 实现新增 Payload；实现依赖解析与查询函数
- `Plugins/SingularisCombine/Source/SingularisCombine/Public/Components/SingularisCombineComponent.h`：新增 `TriggerEvaluate` 声明、`PendingPayload` 内部变量
- `Plugins/SingularisCombine/Source/SingularisCombine/Private/Components/SingularisCombineComponent.cpp`：实现 `TriggerEvaluate`、`EvaluatePipeline` 增加依赖预解析与 Payload 传递、SPI 调用传入 Payload

## 设计来源

Context 与 Payload 分离的设计思想参考 CHANT 项目（Godot 4.7 + Rust GDExtension）的 `Ability::execute(ctx, payload)` 模式：

- `AbilityContext`（`ability.rs`）：身份引用（instigator / avatar / target）
- `TransientPayload`（`payload.rs`）：瞬态数据容器（`Dictionary<Variant, Variant>`）
- `execute` 签名将两者作为独立形参传递

化合将此模式 UE 化适配：

- `FSingularisCombineContext` 对应 `AbilityContext`（身份引用）
- `FSingularisCombineTransientPayload` 对应 `TransientPayload`（瞬态数据），用 `FInstancedStruct` 替代 `Dictionary<Variant, Variant>`
- SPI 形参 `(Context, Payload, Tags, NativeTags)` 对应 `execute(ctx, payload)`
