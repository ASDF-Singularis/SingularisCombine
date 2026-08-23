#pragma once

#include <CoreMinimal.h>
#include <K2Node.h>

#include "K2Node_SingularisDeclareDependency.generated.h"

class UActorComponent;

/**
 * 声明依赖 K2Node（声明与获取一体）
 * 声明即使用：作用域与组件类为节点输入引脚（未连接时直接在节点上选择），输出引脚直接输出组件引用，
 * 输出类型由组件类配置实时推导，无跨节点绑定环节。
 * 编译期 hook 扫描本节点按 (Scope, Class) 去重写入 CDO；ExpandNode 展开为对
 * USingularisCombine::GetDeclaredComponent(Scope, Class) 的纯函数调用。
 */
UCLASS()
class UK2Node_SingularisDeclareDependency : public UK2Node
{
	GENERATED_BODY()

public:
	/** 作用域配置引脚名（hook 与展开逻辑共用，避免魔法字符串漂移） */
	static FName GetScopePinName() { return FName(TEXT("Scope")); }

	/** 组件类配置引脚名（hook 与展开逻辑共用，避免魔法字符串漂移） */
	static FName GetComponentClassPinName() { return FName(TEXT("ComponentClass")); }

	/** 创建配置引脚（Scope / ComponentClass）、隐藏 self 引脚与输出引脚 */
	virtual void AllocateDefaultPins() override;

	/**
	 * 编译展开：替换为对 GetDeclaredComponent(Scope, Class) 的中间调用节点并迁移链接
	 * @param CompilerContext  当前编译上下文，用于生成中间节点与迁移引脚
	 * @param SourceGraph      本节点所在图，中间节点挂载于此
	 */
	virtual void ExpandNode(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph) override;

	/** 节点重建后按组件类配置恢复输出引脚类型 */
	virtual void PostReconstructNode() override;

	/** 组件类引脚默认值变更时实时刷新输出引脚类型 */
	virtual void PinDefaultValueChanged(UEdGraphPin* Pin) override;

	/** 节点标题：声明依赖 {作用域}: {组件类}，配置未就绪时回退为「声明依赖」 */
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;

	/** 节点悬浮提示 */
	virtual FText GetTooltipText() const override;

	/** 标题着色（紫色，区分于普通调用节点） */
	virtual FLinearColor GetNodeTitleColor() const override;

	/** 纯节点：无执行引脚、无副作用，输出未连接时允许编译器修剪 */
	virtual bool IsNodePure() const override { return true; }

	/**
	 * 编译期校验：宿主蓝图类型、作用域与组件类配置完整性、连接警告
	 * @param MessageLog 编译结果日志，错误与警告写入此对象
	 */
	virtual void ValidateNodeDuringCompilation(FCompilerResultsLog& MessageLog) const override;

	/** 注册到蓝图右键菜单 */
	virtual void GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const override;

	/** 菜单分类路径（SingularisCombine|声明） */
	virtual FText GetMenuCategory() const override;

	/**
	 * 图兼容性：仅允许放置于 USingularisCombine 派生蓝图的图中
	 * @param TargetGraph 待放置的目标图
	 * @return 是否允许放置
	 */
	virtual bool IsCompatibleWithGraph(const UEdGraph* TargetGraph) const override;

private:
	/** 依据 ComponentClass 引脚当前值刷新输出引脚类型（未配置时回退 UActorComponent） */
	void ConformOutputPinType();
};
