#include "Nodes/K2Node_SingularisDeclareDependency.h"

#include <BlueprintActionDatabaseRegistrar.h>
#include <BlueprintNodeSpawner.h>
#include <EdGraphSchema_K2.h>
#include <K2Node_CallFunction.h>
#include <KismetCompiler.h>
#include <EdGraph/EdGraph.h>
#include <Kismet2/BlueprintEditorUtils.h>
#include <Kismet2/CompilerResultsLog.h>

#include "Objects/SingularisCombineBase.h"

#define LOCTEXT_NAMESPACE "K2Node_SingularisDeclareDependency"

void UK2Node_SingularisDeclareDependency::AllocateDefaultPins()
{
	Super::AllocateDefaultPins();

	// 隐藏 self 引脚：编译器自动连接到蓝图 self 上下文，ExpandNode 迁移至调用节点
	UEdGraphPin* SelfPin = CreatePin(
		EGPD_Input,
		UEdGraphSchema_K2::PC_Object,
		USingularisCombine::StaticClass(),
		UEdGraphSchema_K2::PN_Self
	);
	SelfPin->bHidden = true;

	// 输出引脚：类型由节点自持的组件类窄化
	UEdGraphPin* OutPin = CreatePin(
		EGPD_Output,
		UEdGraphSchema_K2::PC_Object,
		UActorComponent::StaticClass(),
		UEdGraphSchema_K2::PN_ReturnValue
	);
	if (ComponentClass)
		OutPin->PinType.PinSubCategoryObject = ComponentClass;
}

void UK2Node_SingularisDeclareDependency::ExpandNode(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph)
{
	Super::ExpandNode(CompilerContext, SourceGraph);

	if (!ComponentClass)
	{
		CompilerContext.MessageLog.Error(TEXT("@0 的组件类未设置"), this);
		BreakAllNodeLinks();
		return;
	}

	UK2Node_CallFunction* CallGet = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
	// GetDeclaredComponent 为基类 protected 函数，只能按名经反射解析（GET_FUNCTION_NAME_CHECKED 受访问限制不可用）；
	// SetFromFunction 依据 BlueprintPure 元数据自动置为纯节点，并解析为 self 上下文调用
	CallGet->SetFromFunction(
		USingularisCombine::StaticClass()->FindFunctionByName(FName(TEXT("GetDeclaredComponent")))
	);
	CallGet->AllocateDefaultPins();

	// Target = self（策略实例）：迁移本节点 self 引脚连接到调用节点
	UEdGraphPin* NodeSelfPin = FindPin(UEdGraphSchema_K2::PN_Self);
	UEdGraphPin* CallSelfPin = CallGet->FindPinChecked(UEdGraphSchema_K2::PN_Self);
	if (NodeSelfPin)
		CompilerContext.MovePinLinksToIntermediate(*NodeSelfPin, *CallSelfPin);

	// Scope 字面量
	if (UEdGraphPin* ScopeArg = CallGet->FindPinChecked(TEXT("Scope")))
	{
		ScopeArg->DefaultValue = StaticEnum<ESingularisCombineDependencyScope>()->GetNameStringByValue(
			static_cast<int64>(Scope)
		);
	}

	// ComponentClass 字面量
	if (UEdGraphPin* ClassArg = CallGet->FindPinChecked(TEXT("ComponentClass")))
		ClassArg->DefaultObject = ComponentClass;

	// 输出引脚类型推导 + 链接迁移到中间调用节点
	UEdGraphPin* NodeOutPin = FindPinChecked(UEdGraphSchema_K2::PN_ReturnValue);
	UEdGraphPin* CallOutPin = CallGet->GetReturnValuePin();
	NodeOutPin->PinType.PinSubCategoryObject = ComponentClass;
	CallOutPin->PinType.PinSubCategoryObject = ComponentClass;
	CompilerContext.MovePinLinksToIntermediate(*NodeOutPin, *CallOutPin);

	BreakAllNodeLinks();
}

FText UK2Node_SingularisDeclareDependency::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	if (!ComponentClass)
		return LOCTEXT("NodeTitle_None", "声明依赖");
	return FText::Format(
		LOCTEXT("NodeTitle", "声明依赖 {0}: {1}"),
		StaticEnum<ESingularisCombineDependencyScope>()->GetDisplayNameTextByValue(static_cast<int64>(Scope)),
		ComponentClass->GetDisplayNameText()
	);
}

FText UK2Node_SingularisDeclareDependency::GetTooltipText() const
{
	return LOCTEXT("NodeTooltip", "声明策略对该作用域组件的依赖并输出组件引用，编译期写入 CDO");
}

FLinearColor UK2Node_SingularisDeclareDependency::GetNodeTitleColor() const
{
	return FLinearColor(0.8f, 0.4f, 1.0f);
}

void UK2Node_SingularisDeclareDependency::ValidateNodeDuringCompilation(FCompilerResultsLog& MessageLog) const
{
	Super::ValidateNodeDuringCompilation(MessageLog);

	UBlueprint* Blueprint = GetBlueprint();
	if (!Blueprint || !Blueprint->ParentClass || !Blueprint->ParentClass->IsChildOf(USingularisCombine::StaticClass()))
		MessageLog.Error(TEXT("@0 必须位于 USingularisCombine 派生蓝图中"), this);
	if (!ComponentClass)
		MessageLog.Error(TEXT("@0 的组件类未设置"), this);
}

void UK2Node_SingularisDeclareDependency::GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
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
	if (!Super::IsCompatibleWithGraph(TargetGraph))
		return false;

	UBlueprint* Blueprint = FBlueprintEditorUtils::FindBlueprintForGraph(TargetGraph);
	return Blueprint && Blueprint->ParentClass && Blueprint->ParentClass->IsChildOf(USingularisCombine::StaticClass());
}

#undef LOCTEXT_NAMESPACE
