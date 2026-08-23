#include "Nodes/K2Node_SingularisDeclareDependency.h"

#include <BlueprintActionDatabaseRegistrar.h>
#include <BlueprintNodeSpawner.h>
#include <EdGraphSchema_K2.h>
#include <K2Node_CallFunction.h>
#include <KismetCompiler.h>
#include <EdGraph/EdGraph.h>
#include <Kismet2/BlueprintEditorUtils.h>
#include <Kismet2/CompilerResultsLog.h>

#include "Components/SingularisCombineComponent.h"
#include "Objects/SingularisCombineBase.h"

#define LOCTEXT_NAMESPACE "K2Node_SingularisDeclareDependency"

void UK2Node_SingularisDeclareDependency::AllocateDefaultPins()
{
	Super::AllocateDefaultPins();

	// 1) Scope 配置引脚（枚举）：未连接时直接在节点上选择，默认 Avatar
	UEdGraphPin* ScopePin = CreatePin(
		EGPD_Input,
		UEdGraphSchema_K2::PC_Byte,
		StaticEnum<ESingularisCombineDependencyScope>(),
		GetScopePinName()
	);
	ScopePin->DefaultValue = StaticEnum<ESingularisCombineDependencyScope>()->GetNameStringByValue(
		static_cast<int64>(ESingularisCombineDependencyScope::Avatar)
	);

	// 2) ComponentClass 配置引脚（类）：未连接时直接在节点上选择组件类
	UEdGraphPin* ClassPin = CreatePin(
		EGPD_Input,
		UEdGraphSchema_K2::PC_Class,
		UActorComponent::StaticClass(),
		GetComponentClassPinName()
	);
	ClassPin->DefaultValue = UActorComponent::StaticClass()->GetName();

	// 3) 隐藏 self 引脚：编译器自动连接蓝图 self 上下文，ExpandNode 迁移至提供者调用节点
	UEdGraphPin* SelfPin = CreatePin(
		EGPD_Input,
		UEdGraphSchema_K2::PC_Object,
		USingularisCombine::StaticClass(),
		UEdGraphSchema_K2::PN_Self
	);
	SelfPin->bHidden = true;

	// 4) 输出引脚：类型跟随组件类配置，初始以基类兜底
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

	// 1) 校验配置引脚存在且已配置（未连接的引脚必须携带默认值）
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

	// 2) 中间节点 1：策略 → 依赖提供者（GetDependencyProvider，internal 反射调用）
	UK2Node_CallFunction* CallProvider = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
	CallProvider->SetFromFunction(
		USingularisCombine::StaticClass()->FindFunctionByName(FName(TEXT("GetDependencyProvider")))
	);
	CallProvider->AllocateDefaultPins();

	// 3) 迁移 self 上下文：本节点隐藏 self 引脚 → 提供者调用节点 self 引脚
	UEdGraphPin* NodeSelfPin = FindPin(UEdGraphSchema_K2::PN_Self);
	UEdGraphPin* ProviderSelfPin = CallProvider->FindPinChecked(UEdGraphSchema_K2::PN_Self);
	if (NodeSelfPin)
		CompilerContext.MovePinLinksToIntermediate(*NodeSelfPin, *ProviderSelfPin);

	// 4) 中间节点 2：组件 → 查询声明组件（GetDeclaredComponent，internal 反射调用）
	UK2Node_CallFunction* CallGet = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
	CallGet->SetFromFunction(
		USingularisCombineComponent::StaticClass()->FindFunctionByName(FName(TEXT("GetDeclaredComponent")))
	);
	CallGet->AllocateDefaultPins();

	// 5) 连接提供者输出 → 组件查询 self（类型均为 USingularisCombineComponent）
	UEdGraphPin* ProviderOutPin = CallProvider->GetReturnValuePin();
	UEdGraphPin* CallGetSelfPin = CallGet->FindPinChecked(UEdGraphSchema_K2::PN_Self);
	ProviderOutPin->MakeLinkTo(CallGetSelfPin);

	// 6) 写入参数字面量：连接则迁移动态值，未连接则拷贝配置默认值
	UEdGraphPin* ScopeArg = CallGet->FindPinChecked(TEXT("Scope"));
	if (ScopePin->LinkedTo.Num() > 0)
		CompilerContext.MovePinLinksToIntermediate(*ScopePin, *ScopeArg);
	else
		ScopeArg->DefaultValue = ScopePin->DefaultValue;

	UEdGraphPin* ClassArg = CallGet->FindPinChecked(TEXT("ComponentClass"));
	if (ClassPin->LinkedTo.Num() > 0)
		CompilerContext.MovePinLinksToIntermediate(*ClassPin, *ClassArg);
	else
		ClassArg->DefaultObject = ClassPin->DefaultObject;

	// 7) 输出类型推导 + 链接迁移：以配置的组件类窄化调用节点返回类型，保持与本节点输出一致
	UEdGraphPin* NodeOutPin = FindPinChecked(UEdGraphSchema_K2::PN_ReturnValue);
	UEdGraphPin* CallOutPin = CallGet->GetReturnValuePin();
	NodeOutPin->PinType.PinSubCategoryObject = ComponentType ? ComponentType : UActorComponent::StaticClass();
	CallOutPin->PinType.PinSubCategoryObject = ComponentType ? ComponentType : UActorComponent::StaticClass();
	CompilerContext.MovePinLinksToIntermediate(*NodeOutPin, *CallOutPin);

	// 8) 断离本节点全部链接，编译期由中间节点接管
	BreakAllNodeLinks();
}

void UK2Node_SingularisDeclareDependency::PostReconstructNode()
{
	Super::PostReconstructNode();

	// 图结构变更重建引脚后，按组件类配置恢复输出类型
	ConformOutputPinType();
}

void UK2Node_SingularisDeclareDependency::PinDefaultValueChanged(UEdGraphPin* Pin)
{
	Super::PinDefaultValueChanged(Pin);

	// 组件类引脚在节点上被修改时，实时同步输出引脚类型
	if (Pin && Pin->PinName == GetComponentClassPinName())
		ConformOutputPinType();
}

FText UK2Node_SingularisDeclareDependency::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	UEdGraphPin* ScopePin = FindPin(GetScopePinName());
	UEdGraphPin* ClassPin = FindPin(GetComponentClassPinName());

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
	UBlueprint* Blueprint = GetBlueprint();
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
	UBlueprint* Blueprint = FBlueprintEditorUtils::FindBlueprintForGraph(TargetGraph);
	return Blueprint && Blueprint->ParentClass && Blueprint->ParentClass->IsChildOf(USingularisCombine::StaticClass());
}

void UK2Node_SingularisDeclareDependency::ConformOutputPinType() const
{
	UEdGraphPin* ClassPin = FindPin(GetComponentClassPinName());
	UEdGraphPin* OutPin = FindPin(UEdGraphSchema_K2::PN_ReturnValue);
	if (!ClassPin || !OutPin)
		return;

	// 未配置组件类时以基类兜底，保证输出引脚始终可连
	UClass* ComponentType = Cast<UClass>(ClassPin->DefaultObject);
	OutPin->PinType.PinSubCategoryObject = ComponentType ? ComponentType : UActorComponent::StaticClass();
}

#undef LOCTEXT_NAMESPACE
