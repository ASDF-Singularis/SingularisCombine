#pragma once

#include <Templates/Casts.h>
#include <Templates/UnrealTypeTraits.h>

#include "Components/SingularisCombineComponent.h"
#include "Types/SingularisCombineDependencyScope.h"

/**
 * 声明式依赖代理
 * 作为 SINGULARIS_DECLARE_DEPENDENCY 展开体的运行时载体，为策略类提供指针语义的依赖访问：
 *   Pocket->OnItemAddedEvent.AddUniqueDynamic(this, &ThisClass::OnItemAdded);   // operator->
 *   if (IsValid(Pocket)) { ... }                                                // 隐式裸指针转换
 * 解析为惰性求值（每次访问实时反查），内部经静态工厂 GetFromStrategy 反查所属化合组件，
 * 再调用其 GetDeclaredComponent 查询缓存，编译期类型校验，不可绕过。
 * 仅持有策略自身指针，不持有化合组件引用（依赖倒置）。
 *
 * @tparam Scope       依赖作用域（ESingularisCombineDependencyScope 枚举项）
 * @tparam UClassType  组件类型（须为 UActorComponent 派生类）
 */
template <ESingularisCombineDependencyScope Scope, typename UClassType>
class TSingularisCombineDependency
{
	static_assert(
		TIsDerivedFrom<UClassType, UActorComponent>::IsDerived,
		"SINGULARIS_DECLARE_DEPENDENCY 的组件类型必须是 UActorComponent 的派生类"
	);

public:
	explicit TSingularisCombineDependency(const USingularisCombine* InStrategy)
		: Strategy(InStrategy) {}

	/** 指针语义：Pocket->GetSelectedIndex() */
	UClassType* operator->() const { return Get(); }

	/** 解引用语义：(*Pocket).GetSelectedIndex() */
	UClassType& operator*() const { return *Get(); }

	/** 裸指针语义：if (Pocket) / IsValid(Pocket) / 传参，与旧版 Get[Name]() 返回的裸指针行为一致 */
	explicit operator UClassType*() const { return Get(); }

	/** 解析依赖；宿主化合组件缺失或依赖未缓存时返回 nullptr */
	UClassType* Get() const
	{
		USingularisCombineComponent* const OwnerComponent = USingularisCombineComponent::GetFromStrategy(Strategy);
		return Cast<UClassType>(
			OwnerComponent
				? OwnerComponent->GetDeclaredComponent(Scope, UClassType::StaticClass())
				: nullptr
		);
	}

private:
	/** 所属策略实例（构造期指向自身），仅用于反查宿主化合组件 */
	const USingularisCombine* Strategy = nullptr;
};

/**
 * 在 USingularisCombine 派生类体内声明类型安全的依赖访问器与静态注册器
 * 必须放置于类体的 public 区域。
 *
 * 展开：
 *   1) 依赖代理成员 Name，类型 TSingularisCombineDependency<Scope, UClassType>，
 *      提供 Name->、*Name 与隐式裸指针转换；内部经静态工厂 GetFromStrategy 反查所属化合组件，
 *      再调用其 GetDeclaredComponent 查询缓存，编译期类型校验，不可绕过。策略类不持有对组件的直接引用（依赖倒置）。
 *   2) inline 访问器 Get[Name]() const，返回 [UClassType]*，等价于 Name.Get()，保留原有调用形态；
 *   3) 静态注册器实例，模块加载期向全局注册表登记
 *      (StaticClass(), Scope, UClassType::StaticClass())。
 *
 * 注意：Name 会占用类体的成员名，勿与自身成员重名。
 *
 * @param Scope        ESingularisCombineDependencyScope 枚举项（如 Avatar）
 * @param UClassType   组件类型（如 UMovementComponent）
 * @param Name         访问器名（如 Move → 生成成员 Move 与访问器 GetMove）
 */
#define SINGULARIS_DECLARE_DEPENDENCY(Scope, UClassType, Name) \
    TSingularisCombineDependency<ESingularisCombineDependencyScope::Scope, UClassType> Name{this}; \
    UClassType* Get##Name() const \
    { \
        return Name.Get(); \
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
