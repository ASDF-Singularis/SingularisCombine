#pragma once

#include <Templates/Casts.h>

#include "Interfaces/SingularisCombineDependencyProvider.h"
#include "Types/SingularisCombineDependencyRegistry.h"
#include "Types/SingularisCombineDependencyScope.h"

/**
 * 在 USingularisCombine 派生类体内声明类型安全的依赖访问器与静态注册器
 * 必须放置于类体的 public 区域。
 *
 * 展开：
 *   1) inline 访问器 Get[Name]() const，返回 [UClassType]*；
 *      直调注入的依赖查询提供者（查询能力下沉于提供者，策略不承载查询逻辑），未注入时返回 nullptr。
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
        const ISingularisCombineDependencyProvider* const Provider = DependencyProvider.Get(); \
        return Provider ? Cast<UClassType>(Provider->GetDeclaredComponent(ESingularisCombineDependencyScope::Scope, UClassType::StaticClass())) : nullptr; \
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
