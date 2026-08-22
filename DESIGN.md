# SingularisCombine 架构设计文档 (Architecture Document)

## 概述 (Overview)

`SingularisCombine`（引力奇点化合插件）是一套基于 Unreal Engine 5 的通用 **Systemic Design (系统驱动设计)** 与 **Rule Engine (规则引擎)** 框架。

本插件的终极目标是解决复杂游戏系统中（如魔法组合、环境化学反应、机甲模块组装）实体间的**状态感知与化合反应**问题。它严格遵循 **Zero Tight Coupling (零紧耦合)** 原则，将“业务逻辑表现”与“协同规则判定”彻底剥离。通过引入全局黑板、GameplayTags 语义抽象与策略模式，它能够驱动游戏世界产生无限的 **Emergence (涌现)** 玩法。

插件最初为上层“魔法组合”场景设计，偏向上层具体游戏行为。为适配游戏层（中层）大量使用化合将各独立插件（装备、物品栏、搬运、查询等）链接为完整玩法的场景，引入了三项中层适配能力：**声明式依赖注入**、**瞬态负荷 (Payload)** 与**事件驱动评估**。三者在不破坏上层化合用法的前提下，消除了中层使用时的查询样板、外部入参缺失与评估时机痛点。

## 一、 核心架构哲学 (Core Architectural Philosophy)

本插件的设计摒弃了传统的“硬编码判断”与“高度耦合通信”，建立在以下三大哲学基石之上：

### 1. 语义抽象与黑板模式 (Semantic Abstraction & Blackboard)

系统不再依赖具体的类名（Class）进行通信，而是采用 UE 原生的 `GameplayTags` 作为“世界语”。

- **黑板机制：** 化合组件会收集实体及子组件的所有 Tags，形成一个全局状态黑板。
- **Duck Typing (鸭子类型)：** 只要黑板上存在 `Element.Fire`，系统即判定当前处于“火”环境中，无论这个火是由魔法组件、场景岩浆还是玩家装备提供的。

### 2. 策略驱动与规则引擎 (Strategy-Driven Rule Engine)

化合逻辑不再堆砌在实体组件内部，而是抽象为一系列独立的 **策略对象 (Strategy / Rule)**。

- **开闭原则 (OCP)：** 新增化合反应（如“毒+雷=毒爆”）只需新建一条策略并插入管线，对现有系统实现绝对的“零修改”。
- **可插拔架构 (Pluggable)：** 策略列表可以在运行时被动态增减，化合组件对此完全无感且免疫崩溃。

### 3. 提线木偶与操纵者 (Puppet & Puppeteer Pattern)

- **业务组件 (Puppet)：** 游戏内的具体表现组件（如水球组件）应被降级为没有判断能力的提线木偶，仅对外暴露纯功能性 API（如 `MorphToShield`）。
- **化合策略 (Puppeteer)：** `SingularisCombine` 中的策略对象作为操纵者，负责监听黑板，并在满足条件时调用木偶的 API。

## 二、 核心模块解析 (Core Modules Breakdown)

插件由四个核心数据结构与类组成，它们共同构成了一个状态机求值管线。

### 1. `USingularisCombineComponent` (化合组件 / 执行器)

挂载在实体（Avatar）上的统筹枢纽。它不包含任何具体游戏规则，是一个纯粹的**上下文维护者**和**策略执行器**。

| **核心职责** | **实现机制**                                                 |
| ------------ | ------------------------------------------------------------ |
| **状态感知** | 遍历 Actor 及挂载组件，通过 `IGameplayTagAssetInterface` 提取并汇总 `BlackboardTags`。 |
| **事件广播** | 状态更新时触发 `OnCombineBlackboardUpdatedEvent`，允许外部系统（如 UI）被动监听环境变化。 |
| **管线求值** | 提供 `EvaluatePipeline()` 核心方法，按顺序逐一询问并执行 `CombinePipeline` 中的策略列表。所有策略独立判定，互不中断。 |
| **依赖预解析** | 评估前调用组件层的 `ResolveDependencies(Context)`，按作用域聚合管线所有策略的 `DeclaredComponents`，将每个作用域映射到 Context 中对应 Actor 并查找缓存到 `CachedDependencies`；调用时机覆盖 `EvaluatePipeline` 与 `TickComponent`，保证 `SustainReaction` 中查询亦可用。 |
| **依赖前置预检** | `AreDependenciesSatisfied(Strategy, Context)` 在 `CanReaction` 之前强约束检查：空声明=无条件满足；任意声明依赖缺失=不进入 `CanReaction`，直接走回滚路径。保证 `CanReaction` / `Reaction` 执行时声明依赖必定存在。 |
| **事件驱动评估** | 提供 `TriggerEvaluate(Payload)` API，立即触发一次评估并将事件载荷贯穿整条管线，不等周期轮询。 |

### 2. `USingularisCombine` (化合策略基类)

定义一种化合反应触发条件与执行逻辑的抽象基类。通过 `UCLASS(EditInlineNew)` 允许策划在编辑器内直接实例化与配置。它具备极其严谨的生命周期。

| **生命周期阶段**       | **接口**            | **核心职责与最佳实践**                                       |
| ---------------------- | ------------------- | ------------------------------------------------------------ |
| **阶段 1：条件判定**   | `CanReaction()`     | **Stateless(无状态) 判定**。读取 Context、Payload 与 BlackboardTags，返回是否满足化合条件（如 `HasTag(Water) && HasTag(Fire)`）。绝不在此修改任何数据。 |
| **阶段 2：执行操纵**   | `Reaction()`        | 当条件成立且处于未激活状态时触发。获取 `Context.Avatar` 上的具体表现组件，执行真正的化合业务（变身、生成特效、施加 Buff）。接收 Payload 以感知本次触发的事件载荷。 |
| **阶段 3：回滚剥离**   | `ReactionRevert()`  | **Graceful Invalidation (优雅失效)**。当环境突变导致 `CanReaction` 变为 false 或管线中断时触发。负责清理 `Reaction` 中产生的临时状态、恢复基础表现。接收 Payload 供回滚决策参考。 |
| **阶段 4：持续反应**   | `SustainReaction()` | **每帧调用**。当策略处于激活状态时逐帧触发，接收 Payload 供读取状态决策，但仅用于编写**不修改木偶状态的瞬态效果**（粒子特效、持续音效、屏幕震动、力场等）。严禁在此修改状态——状态的变更由 Reaction/ReactionRevert 统一管理。 |

四个 SPI 方法均接收 `(Context, Payload, BlackboardGameplayTags, BlackboardNativeTags)` 形参（`SustainReaction` 额外接收 `DeltaTime`）。其中 `Context` 承载稳定的场景身份引用，`Payload` 承载瞬态的事件载荷——两者分离，职责清晰。

#### 声明式依赖注入 (Declarative Dependency Injection)

策略基类提供声明式依赖注入机制，消除中层使用时反复 `GetComponentByClass` + `IsValid` 的查询样板。采用“策略声明、组件缓存、提供者注入”的分层设计：策略通过化合组件注入的依赖查询提供者（`ISingularisCombineDependencyProvider`）读取依赖，不直接引用具体组件类（依赖倒置）。

**策略层（`USingularisCombine`）**：

- **`DeclaredComponents`：** 编辑器配置字段，类型为 `TMap<ESingularisCombineDependencyScope, FSingularisCombineDependencyList>`，按作用域（Instigator / Avatar / Target）结构化声明所需的组件类型。UHT 不支持嵌套容器作为 UPROPERTY，因此用 `FSingularisCombineDependencyList` USTRUCT 包装 `TArray<TSubclassOf<UActorComponent>>` 作为 Map 值。
- **`GetDeclaredComponent(Scope, Class)`：** BlueprintPure 函数，读取预缓存声明组件。Scope 形参为作用域枚举（Instigator / Avatar / Target），内部委托注入的 `ISingularisCombineDependencyProvider`，零查找开销。蓝图侧以 `DeterminesOutputType = "ComponentClass"` 自动推导输出类型免 Cast；C++ 侧提供模板便捷版 `GetDeclaredComponent<T>(Scope)` 自动转换返回类型。
- **`SetDependencyProvider(Provider)`：** 由化合组件在注册/替换管线时调用，将自身注入为策略的依赖查询入口，解除策略对具体组件类的直接引用。

**组件层（`USingularisCombineComponent`）**：

- **实现 `ISingularisCombineDependencyProvider`：** 组件实现依赖查询接口，通过 `BindDependencyProvider` 注入管线内所有策略，覆盖 `BeginPlay` / `SetPipeline` / `AddCombineEntry` 路径。
- **`CachedDependencies`：** 运行时缓存，类型为 `TMap<ESingularisCombineDependencyScope, TMap<TSubclassOf<UActorComponent>, TWeakObjectPtr<UActorComponent>>>`，按作用域分组。纯 C++ 成员（非 UPROPERTY）。
- **`ResolveDependencies(Context)`：** 评估前刷新缓存。聚合管线所有策略的声明并集，将每个作用域映射到 Context 中的 Actor（`GetContextActor`），按声明类型查找并写入对应作用域槽位。
- **`AreDependenciesSatisfied(Strategy, Context)`：** `CanReaction` 的前置强约束。空声明返回 true（无条件满足）；任一声明依赖缺失返回 false（不进入 `CanReaction`，直接回滚）。保证 `CanReaction` / `Reaction` 执行时声明依赖必定存在。

蓝图体验从 4-5 节点（Get Outer → Cast → Get Owner → Get Component by Class → Is Valid）简化为 1 节点（`GetDeclaredComponent(作用域, Class)`，输出类型自动推导）。

### 3. `FSingularisCombineTransientPayload` (瞬态负荷)

承载事件驱动评估的瞬态载荷，与 `Context`（稳定身份）分离。参考 CHANT 项目的 `Ability::execute(ctx, payload)` 设计模式。

| **字段** | **类型** | **语义** |
| -------- | -------- | -------- |
| **`EventTag`** | `FGameplayTag` | 事件标识，可空。周期轮询触发时为空（语义为“无具体事件，仅标签变更”），`TriggerEvaluate` 触发时携带事件标识。 |
| **`EventData`** | `FInstancedStruct` | 结构化事件数据，强类型任意 struct，可空。UE 5.8 中 `FInstancedStruct` 内置于 `CoreUObject` 模块，蓝图原生支持（`Make/Get/Set Instanced Struct` 节点）。 |

`Payload` 仅在触发端本次评估有效，不参与网络复制——符合 `Reaction` 服务端权威的现有模型。命名沿用 CHANT 项目的 `TransientPayload` 语义。

### 4. `FSingularisCombinePipeline` (化合管线数据)

用于包装一组有序的化合策略。

- **执行顺序：** 数组内的策略严格按照自上而下的顺序求值。
- **独立判定：** 所有策略独立判定，互不中断。每个策略根据自身 `CanReaction` 结果独立进入激活或回滚状态，不存在跨策略的挂起或穿透。

## 三、 数据流向与运行机制 (Data Flow)

当动态环境发生改变（如组件被动态增删）或外部事件触发时，系统进入重估循环。评估有三个触发来源：

- **周期轮询定时器**（`AutoEvaluateInterval`）：兜底检测组件移除与标签变更。
- **组件构造回调**（`OnOwnerChildComponentConstructed`）：检测到 OwnerActor 上新组件构造时，合并同帧多次构造为单次评估。
- **事件驱动**（`TriggerEvaluate(Payload)`）：立即触发一次评估，将 Payload 贯穿整条管线。

`EvaluatePipeline()` 的执行顺序：

1. **黑板重置 (Blackboard Reset)：** 所有端清空历史标签，重新收集同 Actor 下的所有 Tags。
2. **标签变更检测与广播：** 若 GameplayTags 或原生 FName 标签集合实际变更，触发 `OnCombineBlackboardUpdatedEvent`（所有端）。
3. **服务端权威闸门：** 非服务端提前 return，后续步骤仅服务端执行。
4. **Context 与 Payload 构建：** 构建 `Context`（Instigator / Avatar / Target / CombineComponent）与 `Payload` 局部快照（读取 `PendingPayload`，周期轮询时为空）。
5. **依赖预解析 (Dependency Resolution)：** 调用组件层 `ResolveDependencies(Context)`，按作用域聚合管线所有策略的 `DeclaredComponents`，对三个 Actor 分别查找并写入 `CachedDependencies`。
6. **状态推演 (State Transition)：** 按序遍历策略：
   - **依赖前置预检**：逐策略调用 `AreDependenciesSatisfied(Strategy, Context)`。空声明=无条件满足；任一声明依赖缺失=不进入 `CanReaction`，若策略原处于激活态则直接走 `ReactionRevert`。
   - 若 `CanReaction == true` 且当前 `bIsActive == false`：调用 `Reaction()`，标记激活。
   - 若 `CanReaction == false` 且当前 `bIsActive == true`：调用 `ReactionRevert()`，标记失活。

`TickComponent` 每帧遍历所有激活策略，调用 `SustainReaction()` 驱动瞬态效果（粒子、音效等），独立于 `EvaluatePipeline`。注：`TickComponent` 中 `Context.Target = nullptr`，故 `SustainReaction` 路径无法解析 Target 作用域依赖；声明 Target 作用域依赖仅对 `EvaluatePipeline`（服务端，`Context.Target = OwnerActor`）路径生效。

`TriggerEvaluate` 写入 `PendingPayload` 后同步调用 `EvaluatePipeline`，评估完成后清空 `PendingPayload`。`EvaluatePipeline` 读取 `PendingPayload` 为局部 `const` 快照，保证同帧内策略间读取一致且免疫嵌套触发。

## 四、 最佳实践规范 (Best Practices)

### 1. 保证领域组件的幂等性 (Idempotence)

在被 `Reaction` 或 `Revert` 操纵时，底层的业务实体（如魔法组件）必须具备退回 **Base State (基准状态)** 的能力。无论被调用多少次，传入相同参数产生的结果必须绝对一致。现有“激活才 Reaction、未激活才 Revert”的幂等机制已保证不会因 Payload 重复触发副作用。

### 2. 在 Reaction 中进行防御性编程 (Defensive Programming)

`CanReaction` 验证的是"逻辑真理"（Tag 存在），而 `Reaction` 面临的是"物理现实"（组件可能刚被销毁）。声明式依赖注入的 `AreDependenciesSatisfied` 前置预检保证在声明依赖存在时才进入 `CanReaction` / `Reaction`，策略可假定声明依赖已就绪。但未声明的动态访问仍**必须**判空，应对物理实体与抽象标签之间由于时序可能产生的短暂不一致。

### 3. 利用 Early Out (提前退出) 保护性能

所有的复杂判定（如位置计算、射线检测）不应放在 `CanReaction` 中。`CanReaction` 应仅作极速的 Tag 比对，从而在最早阶段过滤掉无效策略，实现性能保护。

### 4. Context 与 Payload 分离 (Context vs Payload Separation)

`FSingularisCombineContext` 承载**稳定的场景身份引用**（Instigator / Avatar / Target / CombineComponent），贯穿整个评估周期。`FSingularisCombineTransientPayload` 承载**瞬态的事件载荷**（EventTag / EventData），仅本次评估有效。两者职责严格分离，避免 Context 同时承载稳定身份与瞬态数据违反单一职责。

## 五、 核心架构关键字字典 (Keywords Glossary)

- **Glue (胶水层)：** 连接去中心化模块的代码层。在本项目中，游戏业务层负责挂载组件并触发 `EvaluatePipeline()` 或 `TriggerEvaluate()`，充当使插件生效的胶水。
- **Emergence (涌现论)：** 复杂系统理论核心。底层简单规则（Tag 与策略）互动，在宏观产生未曾硬编码的复杂玩法生态。
- **Defensive Programming (防御性编程)：** 在 `Reaction` 阶段对物理执行器进行判空，应对物理实体与抽象标签之间由于时序可能产生的短暂不一致。
- **State Invalidation (状态失效)：** 当系统的部分组件被动态销毁时，宣告旧黑板作废，强制系统重新计算化合管线，是应对动态环境的核心机制。
- **Decision vs. Execution (决策与执行分离)：** 架构原则。将“是否应该做（看 Tag）”和“具体怎么做（操纵 Actor）”严格区分为 `CanReaction` 与 `Reaction`。
- **Declarative Dependency Injection (声明式依赖注入)：** 中层适配机制。策略在编辑器按作用域（Instigator / Avatar / Target）结构化声明所需组件类型，评估前由化合组件预解析并缓存；化合组件将自身实现 `ISingularisCombineDependencyProvider` 注入策略，策略通过 `GetDeclaredComponent(Scope, Class)` 零查找开销读取；`AreDependenciesSatisfied` 前置预检保证 `CanReaction` 执行时声明依赖必定存在，消除查询样板。
- **Transient Payload (瞬态负荷)：** 中层适配机制。`FSingularisCombineTransientPayload` 承载事件驱动评估的瞬态载荷（EventTag + EventData），与 Context（稳定身份）分离，仅在本次评估有效。
- **Event-Driven Evaluation (事件驱动评估)：** 中层适配机制。`TriggerEvaluate(Payload)` 立即触发评估并携带事件载荷，突破周期轮询的被动模型，支持中层主动事件响应。
