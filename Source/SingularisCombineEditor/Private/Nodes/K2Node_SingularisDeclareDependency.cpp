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

	// Scope 输入引脚（枚举）：静态声明配置，未连接时直接在节点上选择
	UEdGraphPin* ScopePin = CreatePin(
		EGPD_Input,
		UEdGraphSchema_K2::PC_Byte,
		StaticEnum<ESingularisCombineDependencyScope>(),
		GetScopePinName()
	);
	ScopePin->DefaultValue = StaticEnum<ESingularisCombineDependencyScope>()->GetNameStringByValue(
		static_cast<int64>(ESingularisCombineDependencyScope::Avatar)
	);

	// ComponentClass 输入引脚（类）：静态声明配置，未连接时直接在节点上选择组件类
	UEdGraphPin* ClassPin = CreatePin(
		EGPD_Input,
		UEdGraphSchema_K2::PC_Class,
		UActorComponent::StaticClass(),
		GetComponentClassPinName()
	);

	// 隐藏 self 引脚：编译器自动连接蓝图 self 上下文，ExpandNode 迁移至调用节点
	UEdGraphPin* SelfPin = CreatePin(
		EGPD_Input,
		UEdGraphSchema_K2::PC_Object,
		USingularisCombine::StaticClass(),
		UEdGraphSchema_K2::PN_Self
	);
	SelfPin->bHidden = true;

	// 输出引脚：类型跟随组件类配置
	CreatePin(
		EGPD_Output,
		UEdGraphSchema_K2::PC_Object,
		UActorComponent::StaticClass(),
		UEdGraphSchema_K2::PN_ReturnValue
	);
	ConformOutputPinType();
}

void UK2Node_SingularisDeclareDependency::ExpandNode(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph)
{
	Super::ExpandNode(CompilerContext, SourceGraph);

	UEdGraphPin* ScopePin = FindPin(GetScopePinName());
	UEdGraphPin* ClassPin = FindPin(GetComponentClassPinName());
	UClass* ComponentType = ClassPin ? Cast<UClass>(ClassPin->DefaultObject) : nullptr;
	if (!ScopePin || !ClassPin)
	{
		CompilerContext.MessageLog.Error(TEXT("@0 缺少配置引脚"), this);
		BreakAllNodeLinks();
		return;
	}
	if (!ScopePin->LinkedTo.Num() && ScopePin->DefaultValue.IsEmpty())
	{
		CompilerContext.MessageLog.Error(TEXT("@0 的作用域未配置"), this);
		BreakAllNodeLinks();
		return;
	}
	if (!ClassPin->LinkedTo.Num() && !ComponentType)
	{
		CompilerContext.MessageLog.Error(TEXT("@0 的组件类未配置"), this);
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

	// Scope 参数：连接则迁移，否则写入字面量
	UEdGraphPin* ScopeArg = CallGet->FindPinChecked(TEXT("Scope"));
	if (ScopePin->LinkedTo.Num() > 0)
		CompilerContext.MovePinLinksToIntermediate(*ScopePin, *ScopeArg);
	else
		ScopeArg->DefaultValue = ScopePin->DefaultValue;

	// ComponentClass 参数：连接则迁移，否则写入字面量
	UEdGraphPin* ClassArg = CallGet->FindPinChecked(TEXT("ComponentClass"));
	if (ClassPin->LinkedTo.Num() > 0)
		CompilerContext.MovePinLinksToIntermediate(*ClassPin, *ClassArg);
	else
		ClassArg->DefaultObject = ClassPin->DefaultObject;

	// 输出引脚类型推导 + 链接迁移到中间调用节点
	UEdGraphPin* NodeOutPin = FindPinChecked(UEdGraphSchema_K2::PN_ReturnValue);
	UEdGraphPin* CallOutPin = CallGet->GetReturnValuePin();
	NodeOutPin->PinType.PinSubCategoryObject = ComponentType ? ComponentType : UActorComponent::StaticClass();
	CallOutPin->PinType.PinSubCategoryObject = ComponentType ? ComponentType : UActorComponent::StaticClass();
	CompilerContext.MovePinLinksToIntermediate(*NodeOutPin, *CallOutPin);

	BreakAllNodeLinks();
}

void UK2Node_SingularisDeclareDependency::PostReconstructNode()
{
	Super::PostReconstructNode();
	ConformOutputPinType();
}

void UK2Node_SingularisDeclareDependency::PinDefaultValueChanged(UEdGraphPin* Pin)
{
	Super::PinDefaultValueChanged(Pin);

	if (Pin && Pin->PinName == GetComponentClassPinName())
		ConformOutputPinType();
}

void UK2Node_SingularisDeclareDependency::ConformOutputPinType()
{
	UEdGraphPin* ClassPin = FindPin(GetComponentClassPinName());
	UEdGraphPin* OutPin = FindPin(UEdGraphSchema_K2::PN_ReturnValue);
	if (!ClassPin || !OutPin)
		return;

	UClass* ComponentType = Cast<UClass>(ClassPin->DefaultObject);
	OutPin->PinType.PinSubCategoryObject = ComponentType ? ComponentType : UActorComponent::StaticClass();
}

FText UK2Node_SingularisDeclareDependency::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	UEdGraphPin* ScopePin = FindPin(GetScopePinName());
	UEdGraphPin* ClassPin = FindPin(GetComponentClassPinName());

	const UClass* ComponentType = ClassPin ? Cast<UClass>(ClassPin->DefaultObject) : nullptr;
	if (!ScopePin || ScopePin->DefaultValue.IsEmpty() || !ComponentType)
		return LOCTEXT("NodeTitle_None", "声明依赖");

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

	UBlueprint* Blueprint = GetBlueprint();
	if (!Blueprint || !Blueprint->ParentClass || !Blueprint->ParentClass->IsChildOf(USingularisCombine::StaticClass()))
		MessageLog.Error(TEXT("@0 必须位于 USingularisCombine 派生蓝图中"), this);

	const UEdGraphPin* ScopePin = FindPin(GetScopePinName());
	const UEdGraphPin* ClassPin = FindPin(GetComponentClassPinName());
	if (!ScopePin || (!ScopePin->LinkedTo.Num() && ScopePin->DefaultValue.IsEmpty()))
		MessageLog.Error(TEXT("@0 的作用域未配置"), this);
	if (!ClassPin || (!ClassPin->LinkedTo.Num() && !ClassPin->DefaultObject))
		MessageLog.Error(TEXT("@0 的组件类未配置"), this);

	// 连接的配置值编译期不可知，不会被写入 CDO；声明应为静态配置
	if (ScopePin && ClassPin && (ScopePin->LinkedTo.Num() > 0 || ClassPin->LinkedTo.Num() > 0))
		MessageLog.Warning(TEXT("@0 的作用域或组件类被连接，该声明不会写入 CDO（声明应为静态配置）"), this);
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
