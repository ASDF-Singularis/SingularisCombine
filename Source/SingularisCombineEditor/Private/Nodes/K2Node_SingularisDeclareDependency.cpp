#include "Nodes/K2Node_SingularisDeclareDependency.h"

#include <BlueprintActionDatabaseRegistrar.h>
#include <BlueprintNodeSpawner.h>
#include <EdGraphSchema_K2.h>
#include <EdGraph/EdGraph.h>
#include <Kismet2/BlueprintEditorUtils.h>
#include <Kismet2/CompilerResultsLog.h>

#include "Objects/SingularisCombineBase.h"

#define LOCTEXT_NAMESPACE "K2Node_SingularisDeclareDependency"

void UK2Node_SingularisDeclareDependency::PostInitProperties()
{
	Super::PostInitProperties();

	// 新建路径：绑定早于基类 AllocateDefaultPins 生成引脚
	EnsureFunctionBound();
}

void UK2Node_SingularisDeclareDependency::PostLoad()
{
	Super::PostLoad();

	// 加载路径：序列化恢复后兜底绑定（正常资产已序列化函数引用，直接跳过）
	EnsureFunctionBound();
}

void UK2Node_SingularisDeclareDependency::AllocateDefaultPins()
{
	Super::AllocateDefaultPins();

	// 隐藏 self 引脚：编译器自动连接蓝图 self 上下文，用户无需手动指定策略实例
	if (UEdGraphPin* SelfPin = FindPin(UEdGraphSchema_K2::PN_Self))
		SelfPin->bHidden = true;
}

void UK2Node_SingularisDeclareDependency::PostReconstructNode()
{
	Super::PostReconstructNode();

	// 蓝图类此刻必然可用：将 external 引用修正为 self 上下文（幂等：已为 self 上下文时自动跳过）
	FunctionReference.RefreshGivenNewSelfScope<UFunction>(GetBlueprintClassFromNode());
}

FText UK2Node_SingularisDeclareDependency::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	const UEdGraphPin* ScopePin = FindPin(GetScopePinName());
	const UEdGraphPin* ClassPin = FindPin(GetComponentClassPinName());

	// 配置引脚缺失或未就绪时回退为通用标题
	const UClass* ComponentType = ClassPin ? Cast<UClass>(ClassPin->DefaultObject) : nullptr;
	if (!ScopePin || ScopePin->DefaultValue.IsEmpty() || !ComponentType)
		return LOCTEXT("NodeTitle_None", "声明依赖");

	// 作用域枚举名 → 枚举值 → 显示文本
	const UEnum* ScopeEnum = StaticEnum<ESingularisCombineDependencyScope>();
	const int64 ScopeValue = ScopeEnum->GetValueByName(FName(ScopePin->DefaultValue));
	if (ScopeValue == INDEX_NONE)
		return LOCTEXT("NodeTitle_None", "声明依赖");

	return FText::Format(
		LOCTEXT("NodeTitle", "声明依赖 {0}: {1}"),
		ScopeEnum->GetDisplayNameTextByValue(ScopeValue),
		ComponentType->GetDisplayNameText()
	);
}

FText UK2Node_SingularisDeclareDependency::GetTooltipText() const
{
	return LOCTEXT("NodeTooltip", "声明策略对该作用域组件的依赖并输出组件引用（节点上直接配置作用域与组件类），编译期写入 CDO");
}

FLinearColor UK2Node_SingularisDeclareDependency::GetNodeTitleColor() const
{
	return FLinearColor(0.8f, 0.4f, 1.0f);
}

void UK2Node_SingularisDeclareDependency::ValidateNodeDuringCompilation(FCompilerResultsLog& MessageLog) const
{
	Super::ValidateNodeDuringCompilation(MessageLog);

	// 1) 宿主蓝图必须为 USingularisCombine 派生
	const UBlueprint* Blueprint = GetBlueprint();
	if (!Blueprint || !Blueprint->ParentClass || !Blueprint->ParentClass->IsChildOf(USingularisCombine::StaticClass()))
		MessageLog.Error(TEXT("@0 必须位于 USingularisCombine 派生蓝图中"), this);

	// 2) 未连接引脚的默认值必须完整（连接的动态值由调用方提供）
	const UEdGraphPin* ScopePin = FindPin(GetScopePinName());
	const UEdGraphPin* ClassPin = FindPin(GetComponentClassPinName());
	if (!ScopePin || (!ScopePin->LinkedTo.Num() && ScopePin->DefaultValue.IsEmpty()))
		MessageLog.Error(TEXT("@0 的作用域未配置"), this);
	if (!ClassPin || (!ClassPin->LinkedTo.Num() && !ClassPin->DefaultObject))
		MessageLog.Error(TEXT("@0 的组件类未配置"), this);

	// 3) 连接配置警告：动态值编译期不可知，该声明不会被写入 CDO
	if (ScopePin && ClassPin && (ScopePin->LinkedTo.Num() > 0 || ClassPin->LinkedTo.Num() > 0))
		MessageLog.Warning(TEXT("@0 的作用域或组件类被连接，该声明不会写入 CDO（声明应为静态配置）"), this);
}

void UK2Node_SingularisDeclareDependency::GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
	// 动作注册器未开放本节点类时直接返回，避免重复注册
	UClass* ActionClass = GetClass();
	if (!ActionRegistrar.IsOpenForRegistration(ActionClass))
		return;

	UBlueprintNodeSpawner* Spawner = UBlueprintNodeSpawner::Create(ActionClass);
	check(Spawner);
	ActionRegistrar.AddBlueprintAction(ActionClass, Spawner);
}

FText UK2Node_SingularisDeclareDependency::GetMenuCategory() const
{
	return LOCTEXT("MenuCategory", "SingularisCombine|声明");
}

bool UK2Node_SingularisDeclareDependency::IsCompatibleWithGraph(const UEdGraph* TargetGraph) const
{
	// 基类校验不通过时直接拒绝
	if (!Super::IsCompatibleWithGraph(TargetGraph))
		return false;

	// 仅允许放入 USingularisCombine 派生蓝图，声明目标必须存在
	const UBlueprint* Blueprint = FBlueprintEditorUtils::FindBlueprintForGraph(TargetGraph);
	return Blueprint && Blueprint->ParentClass && Blueprint->ParentClass->IsChildOf(USingularisCombine::StaticClass());
}

void UK2Node_SingularisDeclareDependency::EnsureFunctionBound()
{
	if (!FunctionReference.GetMemberName().IsNone())
		return;

	// 降级原因：GetDeclaredComponent 为基类 protected 函数，GET_FUNCTION_NAME_CHECKED（取成员地址）受访问限制，
	// 只能以 FName 字面量按名绑定 external 成员；PostReconstructNode 再修正为 self 上下文
	FunctionReference.SetExternalMember(
		FName(TEXT("GetDeclaredComponent")),
		USingularisCombine::StaticClass()
	);
}

#undef LOCTEXT_NAMESPACE
