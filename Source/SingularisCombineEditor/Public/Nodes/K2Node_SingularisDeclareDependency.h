#pragma once

#include <CoreMinimal.h>
#include <K2Node.h>

#include "Types/SingularisCombineDependencyScope.h"
#include "K2Node_SingularisDeclareDependency.generated.h"

class UActorComponent;

/**
 * 声明依赖 K2Node（声明与获取一体）
 * 自持作用域 + 组件类配置，输出引脚类型由组件类自身推导，声明即使用点，无跨节点匹配环节。
 * 编译期 hook 扫描节点写入 CDO；ExpandNode 展开为对 USingularisCombine::GetDeclaredComponent(Scope, Class) 的纯函数调用。
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
};
