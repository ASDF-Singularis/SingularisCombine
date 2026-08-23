#include "Nodes/K2Node_SingularisGetDeclaredComponent.h"

#include <BlueprintActionDatabaseRegistrar.h>
#include <BlueprintNodeSpawner.h>
#include <EdGraph/EdGraph.h>
#include <EdGraphSchema_K2.h>
#include <K2Node_CallFunction.h>
#include <Kismet/BlueprintEditorUtils.h>
#include <Kismet2/CompilerResultsLog.h>

#include "Nodes/K2Node_SingularisDeclareDependency.h"
#include "Objects/SingularisCombineBase.h"
#include "Types/SingularisCombineDependencyScope.h"

#define LOCTEXT_NAMESPACE "K2Node_SingularisGetDeclaredComponent"

void UK2Node_SingularisGetDeclaredComponent::AllocateDefaultPins()
{
    Super::AllocateDefaultPins();

    // 隐藏 self 引脚：编译器自动连接到蓝图 self 上下文，ExpandNode 迁移至调用节点
    UEdGraphPin* SelfPin = CreatePin(
        EGPD_Input,
        UEdGraphSchema_K2::PC_Object,
        USingularisCombine::StaticClass(),
        UEdGraphSchema_K2::PN_Self);
    SelfPin->bHidden = true;

    UEdGraphPin* OutPin = CreatePin(
        EGPD_Output,
        UEdGraphSchema_K2::PC_Object,
        UActorComponent::StaticClass(),
        UEdGraphSchema_K2::PN_ReturnValue);

    // 编辑期类型推导：若已存在匹配声明，将输出引脚窄化为声明的组件类
    if (const UK2Node_SingularisDeclareDependency* Declaration = FindMatchingDeclaration())
        if (Declaration->ComponentClass)
            OutPin->PinType.PinSubCategoryObject = Declaration->ComponentClass;
}

const UK2Node_SingularisDeclareDependency* UK2Node_SingularisGetDeclaredComponent::FindMatchingDeclaration() const
{
    UBlueprint* Blueprint = GetBlueprint();
    if (!Blueprint)
        return nullptr;

    TArray<UEdGraph*> Graphs;
    FBlueprintEditorUtils::GetAllGraphs(Blueprint, Graphs);

    for (const UEdGraph* Graph : Graphs)
    {
        if (!Graph)
            continue;

        TArray<UK2Node_SingularisDeclareDependency*> Found;
        Graph->GetNodesOfClass<UK2Node_SingularisDeclareDependency>(Found);
        for (const UK2Node_SingularisDeclareDependency* Node : Found)
        {
            if (Node && Node->DependencyName == DependencyName)
                return Node;
        }
    }
    return nullptr;
}

void UK2Node_SingularisGetDeclaredComponent::ValidateNodeDuringCompilation(FCompilerResultsLog& MessageLog) const
{
    Super::ValidateNodeDuringCompilation(MessageLog);

    if (DependencyName.IsNone())
    {
        MessageLog.Error(TEXT("@0 的依赖名字未设置"), this);
        return;
    }

    const UK2Node_SingularisDeclareDependency* Declaration = FindMatchingDeclaration();
    if (!Declaration)
    {
        MessageLog.Error(TEXT("@0 在蓝图中未找到匹配的声明依赖节点"), this);
        return;
    }
    if (!Declaration->ComponentClass)
    {
        MessageLog.Error(TEXT("@0 匹配的声明依赖节点组件类未设置"), this);
    }
}

void UK2Node_SingularisGetDeclaredComponent::ExpandNode(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph)
{
    Super::ExpandNode(CompilerContext, SourceGraph);

    const UK2Node_SingularisDeclareDependency* Declaration = FindMatchingDeclaration();
    if (!Declaration || !Declaration->ComponentClass)
    {
        CompilerContext.MessageLog.Error(TEXT("@0 缺少匹配的声明依赖"), this);
        BreakAllNodeLinks();
        return;
    }

    UK2Node_CallFunction* CallGet = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
    CallGet->FunctionReference.SetSelfMemberFunction(
        USingularisCombine::StaticClass(),
        GET_FUNCTION_NAME_CHECKED(USingularisCombine, GetDeclaredComponent));
    CallGet->bIsPureFunc = true;
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
            static_cast<int64>(Declaration->Scope));
    }

    // ComponentClass 字面量
    if (UEdGraphPin* ClassArg = CallGet->FindPinChecked(TEXT("ComponentClass")))
        ClassArg->DefaultObject = Declaration->ComponentClass;

    // 输出引脚类型推导 + 链接迁移到中间调用节点
    UEdGraphPin* NodeOutPin = FindPinChecked(UEdGraphSchema_K2::PN_ReturnValue);
    UEdGraphPin* CallOutPin = CallGet->GetReturnValuePin();
    NodeOutPin->PinType.PinSubCategoryObject = Declaration->ComponentClass;
    CallOutPin->PinType.PinSubCategoryObject = Declaration->ComponentClass;
    CompilerContext.MovePinLinksToIntermediate(*NodeOutPin, *CallOutPin);

    BreakAllNodeLinks();
}

FText UK2Node_SingularisGetDeclaredComponent::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
    if (DependencyName.IsNone())
        return LOCTEXT("NodeTitle_None", "获取声明组件");
    return FText::Format(LOCTEXT("NodeTitle", "获取 {0}"), FText::FromName(DependencyName));
}

FText UK2Node_SingularisGetDeclaredComponent::GetTooltipText() const
{
    return LOCTEXT("NodeTooltip", "获取声明绑定的依赖组件实例，输出类型由声明推导");
}

FLinearColor UK2Node_SingularisGetDeclaredComponent::GetNodeTitleColor() const
{
    return FLinearColor(0.4f, 0.8f, 1.0f);
}

void UK2Node_SingularisGetDeclaredComponent::GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
    UClass* ActionClass = GetClass();
    if (!ActionRegistrar.IsOpenForRegistration(ActionClass))
        return;

    UBlueprintNodeSpawner* Spawner = UBlueprintNodeSpawner::Create(ActionClass);
    check(Spawner);
    ActionRegistrar.AddBlueprintAction(ActionClass, Spawner);
}

FText UK2Node_SingularisGetDeclaredComponent::GetMenuCategory() const
{
    return LOCTEXT("MenuCategory", "SingularisCombine|声明");
}

bool UK2Node_SingularisGetDeclaredComponent::IsCompatibleWithGraph(const UEdGraph* TargetGraph) const
{
    if (!Super::IsCompatibleWithGraph(TargetGraph))
        return false;

    UBlueprint* Blueprint = FBlueprintEditorUtils::FindBlueprintForGraph(TargetGraph);
    return Blueprint && Blueprint->ParentClass && Blueprint->ParentClass->IsChildOf(USingularisCombine::StaticClass());
}

#undef LOCTEXT_NAMESPACE
