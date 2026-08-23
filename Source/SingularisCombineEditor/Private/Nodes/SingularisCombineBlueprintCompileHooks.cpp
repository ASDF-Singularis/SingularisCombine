#include "Nodes/SingularisCombineBlueprintCompileHooks.h"

#include <EdGraph/EdGraph.h>
#include <EdGraph/EdGraphPin.h>
#include <Engine/Blueprint.h>
#include <UObject/UObjectGlobals.h>

#include "Nodes/K2Node_SingularisDeclareDependency.h"
#include "Objects/SingularisCombineBase.h"
#include "Types/SingularisCombineDependencyList.h"
#include "Types/SingularisCombineDependencyScope.h"

void FSingularisCombineBlueprintCompileHook::Register(FDelegateHandle& OutHandle)
{
	OutHandle = FCoreUObjectDelegates::OnObjectPostCDOCompiled.AddStatic(
		&FSingularisCombineBlueprintCompileHook::HandleCDOCompiled
	);
}

void FSingularisCombineBlueprintCompileHook::Unregister(FDelegateHandle& Handle)
{
	if (Handle.IsValid())
	{
		FCoreUObjectDelegates::OnObjectPostCDOCompiled.Remove(Handle);
		Handle.Reset();
	}
}

void FSingularisCombineBlueprintCompileHook::HandleCDOCompiled(
	UObject* CDO,
	const FObjectPostCDOCompiledContext& Context
)
{
	// 1) 仅全量编译后的产物 CDO 参与回填，跳过骨架类编译
	if (Context.bIsSkeletonOnly)
		return;

	// 2) 仅处理 USingularisCombine 派生蓝图
	UClass* GeneratedClass = CDO ? CDO->GetClass() : nullptr;
	if (!GeneratedClass || !GeneratedClass->IsChildOf(USingularisCombine::StaticClass()))
		return;

	UBlueprint* Blueprint = UBlueprint::GetBlueprintFromClass(GeneratedClass);
	if (!Blueprint)
		return;

	// 3) 扫描所有图中的 DeclareDependency 节点
	TArray<UEdGraph*> AllGraphs;
	Blueprint->GetAllGraphs(AllGraphs);

	TMap<ESingularisCombineDependencyScope, FSingularisCombineDependencyList> Backfilled;
	for (const UEdGraph* Graph : AllGraphs)
	{
		if (!Graph)
			continue;

		TArray<UK2Node_SingularisDeclareDependency*> Found;
		Graph->GetNodesOfClass<UK2Node_SingularisDeclareDependency>(Found);
		for (const UK2Node_SingularisDeclareDependency* Node : Found)
		{
			if (!Node)
				continue;

			// 仅收录未连接的静态声明（连接引脚的动态值编译期不可知，不入 CDO）
			const UEdGraphPin* ScopePin = Node->FindPin(UK2Node_SingularisDeclareDependency::GetScopePinName());
			const UEdGraphPin* ClassPin = Node->FindPin(
				UK2Node_SingularisDeclareDependency::GetComponentClassPinName()
			);
			if (!ScopePin || !ClassPin || ScopePin->LinkedTo.Num() > 0 || ClassPin->LinkedTo.Num() > 0)
				continue;

			UClass* ComponentType = Cast<UClass>(ClassPin->DefaultObject);
			if (!ComponentType)
				continue;

			const int64 ScopeValue = StaticEnum<ESingularisCombineDependencyScope>()->GetValueByName(
				FName(ScopePin->DefaultValue)
			);
			if (ScopeValue == INDEX_NONE)
				continue;

			Backfilled.FindOrAdd(static_cast<ESingularisCombineDependencyScope>(ScopeValue)).Classes.AddUnique(
				ComponentType
			);
		}
	}

	// 4) 写入产物 CDO 的私有 DeclaredComponents（friend 授权）
	USingularisCombine* StrategyCDO = Cast<USingularisCombine>(CDO);
	if (!StrategyCDO)
		return;

	StrategyCDO->DeclaredComponents = MoveTemp(Backfilled);
}
