# SingularisCombine 架构设计文档 (Architecture Document)

## 概述 (Overview)

`SingularisCombine`（引力奇点化合插件）是一套基于 Unreal Engine 5 的通用 **Systemic Design (系统驱动设计)** 与 **Rule Engine (规则引擎)** 框架。

本插件的终极目标是解决复杂游戏系统中（如魔法组合、环境化学反应、机甲模块组装）实体间的**状态感知与化合反应**问题。它严格遵循 **Zero Tight Coupling (零紧耦合)** 原则，将“业务逻辑表现”与“协同规则判定”彻底剥离。通过引入全局黑板、GameplayTags 语义抽象与策略模式，它能够驱动游戏世界产生无限的 **Emergence (涌现)** 玩法。

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

插件由两个核心数据结构与类组成，它们共同构成了一个状态机求值管线。

### 1. `USingularisCombineComponent` (化合组件 / 执行器)

挂载在实体（Avatar）上的统筹枢纽。它不包含任何具体游戏规则，是一个纯粹的**上下文维护者**和**策略执行器**。

| **核心职责** | **实现机制**                                                 |
| ------------ | ------------------------------------------------------------ |
| **状态感知** | 遍历 Actor 及挂载组件，通过 `IGameplayTagAssetInterface` 提取并汇总 `BlackboardTags`。 |
| **事件广播** | 状态更新时触发 `OnCombineBlackboardUpdatedEvent`，允许外部系统（如 UI）被动监听环境变化。 |
| **管线求值** | 提供 `EvaluatePipeline()` 核心方法，按顺序逐一询问并执行 `CombinePipeline` 中的策略列表。 |

### 2. `USingularisCombine` (化合策略基类)

定义一种化合反应触发条件与执行逻辑的抽象基类。通过 `UCLASS(EditInlineNew)` 允许策划在编辑器内直接实例化与配置。它具备极其严谨的生命周期。

| **生命周期阶段**       | **接口**            | **核心职责与最佳实践**                                       |
| ---------------------- | ------------------- | ------------------------------------------------------------ |
| **阶段 1：条件判定**   | `CanReaction()`     | **Stateless(无状态) 判定**。仅读取 BlackboardTags，返回是否满足化合条件（如 `HasTag(Water) && HasTag(Fire)`）。绝不在此修改任何数据。 |
| **阶段 2：执行操纵**   | `Reaction()`        | 当条件成立且处于未激活状态时触发。获取 `Context.Avatar` 上的具体表现组件，执行真正的化合业务（变身、生成特效、施加 Buff）。接收 BlackboardTags 以感知当前黑板全貌。 |
| **阶段 3：回滚剥离**   | `ReactionRevert()`  | **Graceful Invalidation (优雅失效)**。当环境突变导致 `CanReaction` 变为 false 或管线中断时触发。负责清理 `Reaction` 中产生的临时状态、恢复基础表现。接收 BlackboardTags 供回滚决策参考（如判断哪些元素已退场）。 |
| **阶段 4：持续反应**   | `SustainReaction()` | **每帧调用**。当策略处于激活状态时逐帧触发，接收 BlackboardTags 供读取状态决策，但仅用于编写**不修改木偶状态的瞬态效果**（粒子特效、持续音效、屏幕震动、力场等）。严禁在此修改状态——状态的变更由 Reaction/ReactionRevert 统一管理。 |

### 3. `FSingularisCombinePipeline` (化合管线数据)

用于包装一组有序的化合策略，并提供极其关键的**管线中断 (Suspend)** 控制机制。

- **执行顺序：** 数组内的策略严格按照自上而下的顺序求值。
- **中断机制 (`bSuspend`)：** 若设为 `true`，一旦管线中某个策略成功触发了 `Reaction`，则后续所有策略的 `Reaction` 都将被阻止（挂起）。
- **状态机闭环安全：** 中断逻辑使用了极其安全的穿透机制。被挂起的策略即使不能触发 `Reaction`，如果其上一帧处于激活状态，系统依然会强制调用其 `ReactionRevert()`，确保绝不会产生残留的视觉或逻辑死锁。

## 三、 数据流向与运行机制 (Data Flow)

当动态环境发生改变（如组件被动态增删），游戏逻辑层（Glue）调用 `EvaluatePipeline()`，系统进入重估循环：

1. **黑板重置 (Blackboard Reset)：** 清空历史标签，重新收集同 Actor 下的所有 Tags。
2. **管线遍历 (Pipeline Iteration)：** 按序遍历所有的 `USingularisCombine` 策略实体。
3. **状态推演 (State Transition)：**
   - 若 `CanReaction == true` 且当前 `bIsActive == false`：调用 `Reaction()`，标记激活。
   - 若 `CanReaction == false` 且当前 `bIsActive == true`：调用 `ReactionRevert()`，标记失活。
4. **中断拦截 (Suspend Intercept)：** 若触发中断，后续策略跳过判定，若激活则强行 `ReactionRevert()`。
5. **持续反应 (Continuous Tick)：** 组件每帧遍历所有激活策略，调用 `SustainReaction()` 驱动瞬态效果（粒子、音效等）。

## 四、 最佳实践规范 (Best Practices)

### 1. 保证领域组件的幂等性 (Idempotence)

在被 `Reaction` 或 `Revert` 操纵时，底层的业务实体（如魔法组件）必须具备退回 **Base State (基准状态)** 的能力。无论被调用多少次，传入相同参数产生的结果必须绝对一致。

### 2. 在 Reaction 中进行防御性编程 (Defensive Programming)

`CanReaction` 验证的是“逻辑真理”（Tag 存在），而 `Reaction` 面临的是“物理现实”（组件可能刚被销毁）。在 `Reaction` 中调用目标 Actor 的组件时，**必须**进行空指针判定。

### 3. 利用 Early Out (提前退出) 保护性能

所有的复杂判定（如位置计算、射线检测）不应放在 `CanReaction` 中。`CanReaction` 应仅作极速的 Tag 比对，从而在最早阶段过滤掉无效策略，实现性能保护。

## 五、 核心架构关键字字典 (Keywords Glossary)

- **Glue (胶水层)：** 连接去中心化模块的代码层。在本项目中，游戏业务层负责挂载组件并触发 `EvaluatePipeline()`，充当使插件生效的胶水。
- **Emergence (涌现论)：** 复杂系统理论核心。底层简单规则（Tag 与策略）互动，在宏观产生未曾硬编码的复杂玩法生态。
- **Defensive Programming (防御性编程)：** 在 `Reaction` 阶段对物理执行器进行判空，应对物理实体与抽象标签之间由于时序可能产生的短暂不一致。
- **State Invalidation (状态失效)：** 当系统的部分组件被动态销毁时，宣告旧黑板作废，强制系统重新计算化合管线，是应对动态环境的核心机制。
- **Decision vs. Execution (决策与执行分离)：** 架构原则。将“是否应该做（看 Tag）”和“具体怎么做（操纵 Actor）”严格区分为 `CanReaction` 与 `Reaction`。