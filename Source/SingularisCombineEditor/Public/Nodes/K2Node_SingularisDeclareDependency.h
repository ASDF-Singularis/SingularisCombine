#pragma once

#include <CoreMinimal.h>
#include <K2Node.h>

#include "Types/SingularisCombineDependencyScope.h"
#include "K2Node_SingularisDeclareDependency.generated.h"

class UActorComponent;

/**
 * 声明依赖 K2Node（声明与获取一体）
 * 配置即节点：Scope / ComponentClass 为输入引脚，未连接时直接在节点上选择；输出引脚类型由组件类配置实时推导。
 * 编译期 hook 扫描节点写入 CDO；ExpandNode 展开为对 USingularisCombine::GetDeclaredComponent(Scope, Class) 的纯函数调用。
 */
UCLASS()
class UK2Node_SingularisDeclareDependency : public UK2Node
{
	GENERATED_BODY()

public:
	static FName GetScopePinName() { return FName(TEXT("Scope")); }
	static FName GetComponentClassPinName() { return FName(TEXT("ComponentClass")); }

	virtual void AllocateDefaultPins() override;
	virtual void ExpandNode(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph) override;
	virtual void PostReconstructNode() override;
	virtual void PinDefaultValueChanged(UEdGraphPin* Pin) override;
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FText GetTooltipText() const override;
	virtual FLinearColor GetNodeTitleColor() const override;
	virtual bool IsNodePure() const override { return true; }
	virtual void ValidateNodeDuringCompilation(FCompilerResultsLog& MessageLog) const override;
	virtual void GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const override;
	virtual FText GetMenuCategory() const override;
	virtual bool IsCompatibleWithGraph(const UEdGraph* TargetGraph) const override;

private:
	/** 依据 ComponentClass 引脚当前值刷新输出引脚类型 */
	void ConformOutputPinType();
};
