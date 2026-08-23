#include "Nodes/SingularisCombineBlueprintCompileHooks.h"

#include <EdGraph/EdGraph.h>
#include <Engine/Blueprint.h>
#include <Kismet/BlueprintEditorUtils.h>
#include <KismetCompiler/Public/KismetCompilerInterface.h>

#include "Nodes/K2Node_SingularisDeclareDependency.h"
#include "Objects/SingularisCombineBase.h"
#include "Types/SingularisCombineDependencyList.h"
#include "Types/SingularisCombineDependencyScope.h"

void FSingularisCombineBlueprintCompileHook::Register(FDelegateHandle& OutHandle)
{
	FBlueprintCompiledEventHandler Handler;
	Handler.OnBlueprintCompiled.AddStatic(&FSingularisCombineBlueprintCompileHook::HandleBlueprintCompiled);
	OutHandle = IKismetCompilerInterface::Get().RegisterCompiler(Handler);
}

void FSingularisCombineBlueprintCompileHook::Unregister(FDelegateHandle& Handle)
{
	if (Handle.IsValid())
	{
		IKismetCompilerInterface::Get().UnregisterCompiler(Handle);
		Handle.Reset();
	}
}

void FSingularisCombineBlueprintCompileHook::HandleBlueprintCompiled(UBlueprint* Blueprint)
{
	if (!Blueprint || !Blueprint->GeneratedClass)
		return;

	// 1) 仅处理 USingularisCombine 派生蓝图
	if (!Blueprint->GeneratedClass->IsChildOf(USingularisCombine::StaticClass()))
		return;

	// 2) 扫描所有图中的 DeclareDependency 节点
	TArray<UEdGraph*> AllGraphs;
	FBlueprintEditorUtils::GetAllGraphs(Blueprint, AllGraphs);

	TMap<ESingularisCombineDependencyScope, FSingularisCombineDependencyList> Backfilled;
	for (const UEdGraph* Graph : AllGraphs)
	{
		if (!Graph)
			continue;

		TArray<UK2Node_SingularisDeclareDependency*> Found;
		Graph->GetNodesOfClass<UK2Node_SingularisDeclareDependency>(Found);
		for (const UK2Node_SingularisDeclareDependency* Node : Found)
		{
			if (!Node || !Node->ComponentClass || Node->DependencyName.IsNone())
				continue;
			Backfilled.FindOrAdd(Node->Scope).Classes.AddUnique(Node->ComponentClass);
		}
	}

	// 3) 写入产物 CDO 的私有 DeclaredComponents（friend 授权）
	USingularisCombine* StrategyCDO = Cast<USingularisCombine>(Blueprint->GeneratedClass->ClassDefaultObject);
	if (!StrategyCDO)
		return;

	StrategyCDO->DeclaredComponents = MoveTemp(Backfilled);
}
