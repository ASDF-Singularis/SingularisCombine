#include "Nodes/K2Node_SingularisDeclareDependency.h"

#include <BlueprintActionDatabaseRegistrar.h>
#include <BlueprintNodeSpawner.h>
#include <EdGraph/EdGraph.h>
#include <Kismet/BlueprintEditorUtils.h>
#include <Kismet2/CompilerResultsLog.h>

#include "Objects/SingularisCombineBase.h"

#define LOCTEXT_NAMESPACE "K2Node_SingularisDeclareDependency"

void UK2Node_SingularisDeclareDependency::AllocateDefaultPins()
{
    Super::AllocateDefaultPins();
    // 纯声明节点：无执行/数据引脚
}

FText UK2Node_SingularisDeclareDependency::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
    if (DependencyName.IsNone())
        return LOCTEXT("NodeTitle_None", "声明依赖");
    return FText::Format(LOCTEXT("NodeTitle", "声明依赖 {0}"), FText::FromName(DependencyName));
}

FText UK2Node_SingularisDeclareDependency::GetTooltipText() const
{
    return LOCTEXT("NodeTooltip", "声明策略对某作用域组件的依赖，编译期写入 CDO");
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
    {
        MessageLog.Error(TEXT("@0 必须位于 USingularisCombine 派生蓝图中"), this);
    }
    if (!ComponentClass)
    {
        MessageLog.Error(TEXT("@0 的组件类未设置"), this);
    }
    if (DependencyName.IsNone())
    {
        MessageLog.Error(TEXT("@0 的依赖名字未设置"), this);
    }

    // 4) 检查同名声明冲突
    if (Blueprint && !DependencyName.IsNone())
    {
        TArray<UEdGraph*> Graphs;
        FBlueprintEditorUtils::GetAllGraphs(Blueprint, Graphs);

        int32 MatchCount = 0;
        for (const UEdGraph* Graph : Graphs)
        {
            if (!Graph)
                continue;

            TArray<UK2Node_SingularisDeclareDependency*> Siblings;
            Graph->GetNodesOfClass<UK2Node_SingularisDeclareDependency>(Siblings);
            for (const UK2Node_SingularisDeclareDependency* Sibling : Siblings)
            {
                if (Sibling && Sibling != this && Sibling->DependencyName == DependencyName)
                    ++MatchCount;
            }
        }

        if (MatchCount > 0)
            MessageLog.Error(TEXT("@0 的依赖名字与其他声明节点冲突"), this);
    }
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
