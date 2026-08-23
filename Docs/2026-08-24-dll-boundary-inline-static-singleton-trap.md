# DLL 边界的 inline static 单例陷阱（Cross-DLL inline static Singleton Trap）

> **性质：** 通用 C++ / C++ DLL 编程疑难与其规避原则，与具体项目无关。
> **一句话结论：** `inline` 函数中的 `static` 局部变量，在多个独立编译的 DLL 中会各自持有一份副本；
> 跨 DLL 共享进程级单例时必须用模块导出宏将访问函数改为外部链接（out-of-line），让所有 DLL 链接同一份实现。

---

## 1. 一种看起来完全正确的"单例"

最常见的单例写法（Meyers' Singleton），依赖 C++11 的线程安全静态局部初始化：

```cpp
// singleton.h
class Singleton
{
public:
    static Singleton& Get()
    {
        static Singleton Instance;   // 关键：函数局部静态
        return Instance;
    }
};
```

`static` 局部变量的语义：**首次调用时构造，之后每次返回同一对象**。这在单进程、单一代码映像（全部静态链接进一个 exe）下确实"全局唯一"。

## 2. 陷阱：多个 DLL 时，`static` 不再唯一

`static` 局部变量属于**编译函数的这份代码产物**。当 `Get()` 是 **inline** 函数且定义在头文件中时：

- 每个 include 该头文件的**独立编译单元（.cpp → DLL）**都会把 `Get()` 的完整实现拷进自己
- 每个 DLL 因此各拥有一份 `static Instance`

```
┌───────────────────────────────┐
│  Module.dll（运行时）          │
│  ┌────────────────────────┐   │
│  │ Get() 实现（副本1）     │   │
│  │ static Instance  A     │   │
│  └────────────────────────┘   │
└───────────────────────────────┘

┌───────────────────────────────┐
│  ModuleEditor.dll（编辑器）    │
│  ┌────────────────────────┐   │
│  │ Get() 实现（副本2）     │   │
│  │ static Instance  B     │   │  ← 完全不同的一份
│  └────────────────────────┘   │
└───────────────────────────────┘
```

- 在 Module.dll 里调用 `Get()` → 命中副本 1 的 `A`
- 在 ModuleEditor.dll 里调用 `Get()` → 命中副本 2 的 `B`

**`A` 与 `B` 是相互隔离的两个对象**，导致"这边写入、那边读不到"。

### 一句本质

> **inline `static` 局部变量单例 = 每个翻译单位（DLL）一份副本。**
> "全局唯一"的直觉只在单进程单一代码映像下成立；DLL 边界会制造多份。

## 3. 典型症状

- 代码编译、运行都不报错，逻辑"看起来"正确
- 在某个 DLL 写入单例状态，在另一个 DLL 读取时行为异常/为空
- 只在跨 DLL 场景复现；全部静态链接进一个 exe 时正常
- 通过 debugger 对比同一对象的内部字段计数（如 map 条目数），两侧不一致

## 4. 修复方法

把 `Get()` 从 inline 改为**在所属模块 .cpp 中外部定义，并用模块导出宏导出符号**：

```cpp
// singleton.h —— 只声明（不再 inline）
class MODULE_API Singleton      // MODULE_API 例如 ENGINE_API / MYPLUGIN_API
{
public:
    static Singleton& Get();
    // ...
};

// singleton.cpp —— 定义（仅在一个模块内编译一次，由该模块导出）
Singleton& Singleton::Get()
{
    static Singleton Instance;  // 现在只有这一份
    return Instance;
}
```

### 修复后的访问模型

```
┌───────────────────────────────┐
│  Module.dll（定义方）          │
│  ┌────────────────────────┐   │
│  │ Get() 实现（唯一一份）  │   │
│  │ static Instance        │   │
│  └───────────┬────────────┘   │
│              │ 导出符号        │
└──────────────┼────────────────┘
               │ dllimport（链接引用）
┌──────────────┼────────────────┐
│  ModuleEditor.dll              │
│              ↓                 │
│  Get() → 跳转到 Module 的实例   │
└───────────────────────────────┘
```

所有 DLL 通过 `dllimport` 链接到同一份 `Get()` 实现，从而访问**同一个** `Instance`。

## 5. 何时要警惕（易踩坑三条件）

1. inline **`static` 局部变量**实现的单例
2. 单例头文件被**多个独立编译的 DLL** include
3. 这些 DLL **通过该单例共享状态**

三类项目最常触碰：

- **插件系统**：每个插件模块常为独立 DLL（UE 插件尤甚）
- **编辑器/运行时分离**：Editor 模块与 Runtime 模块是两个 DLL，但共享数据
- **预编译库**：header-only 库把 inline 实现打进多个消费者 DLL

## 6. 规避原则

- 跨 DLL 共享的进程级单例，**访问函数必须用模块导出宏（`Xxx_API`）声明**，实现放单个 `.cpp`
- 头文件只保留函数声明，不内联 `static` 局部变量
- 若必须 header-only，则退而求其次接受"每个 DLL 一份"，并明确其语义（状态不跨 DLL 共享）
- 参考各主流引擎/框架做法：全局 `GConfig`、`GEngine` 等单例均为"头文件声明 API 访问函数，实现由所属模块导出"

### 快捷排查信号

- "某处写入、另一处读取为空"，且两头代码明明正确
- 同一对象内部状态（如容器元素计数）在不同 DLL 中不一致
- 仅在跨模块场景复现，静态链接时正常
