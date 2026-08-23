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
