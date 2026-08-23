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
