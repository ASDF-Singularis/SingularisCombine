#pragma once

#include <CoreMinimal.h>
#include <K2Node.h>
#include <K2Node_CallFunction.h>

#include "K2Node_SingularisDeclareDependency.generated.h"

class UActorComponent;

/**
 * 声明依赖 K2Node（声明与获取一体，继承 UK2Node_CallFunction 的最终调用节点形态）
 * 声明即使用：直接绑定 USingularisCombine::GetDeclaredComponent 函数，参数引脚（Scope / ComponentClass）
 * 未连接时直接在节点上编辑，输出类型由 DeterminesOutputType 引擎原生推导，无跨节点绑定与展开环节。
 * 编译期 hook 扫描本节点按 (Scope, Class) 去重写入 CDO。
 */
UCLASS()
class UK2Node_SingularisDeclareDependency : public UK2Node_CallFunction
{
	GENERATED_BODY()

public:
	/** 作用域配置引脚名（hook 与校验逻辑共用，避免魔法字符串漂移） */
	static FName GetScopePinName() { return FName(TEXT("Scope")); }

	/** 组件类配置引脚名（hook 与校验逻辑共用，避免魔法字符串漂移） */
	static FName GetComponentClassPinName() { return FName(TEXT("ComponentClass")); }

	/** 对象构造期确保函数引用已绑定（新建路径） */
	virtual void PostInitProperties() override;

	/** 资产加载期确保函数引用已绑定（加载路径） */
	virtual void PostLoad() override;

	/** 基类按函数签名生成引脚后隐藏 self 引脚：编译器自动连接蓝图 self 上下文 */
	virtual void AllocateDefaultPins() override;

	/** 节点重建后将函数引用修正为 self 上下文（此刻蓝图类必然可用） */
	virtual void PostReconstructNode() override;

	/** 节点标题：声明依赖 {作用域}: {组件类}，配置未就绪时回退为「声明依赖」 */
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;

	/** 节点悬浮提示 */
	virtual FText GetTooltipText() const override;

	/** 标题着色（紫色，区分于普通调用节点） */
	virtual FLinearColor GetNodeTitleColor() const override;

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
	/** 函数引用未绑定时按名绑定基类 GetDeclaredComponent（FName 字面量，规避 protected 取地址限制） */
	void EnsureFunctionBound();
};
