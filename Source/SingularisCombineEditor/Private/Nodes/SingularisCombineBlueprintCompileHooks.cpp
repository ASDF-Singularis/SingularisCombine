#include "Nodes/SingularisCombineBlueprintCompileHooks.h"

#include <EdGraph/EdGraph.h>
#include <EdGraph/EdGraphPin.h>
#include <Engine/Blueprint.h>
#include <UObject/UObjectGlobals.h>

#include "SingularisCombine.h"
#include "Nodes/K2Node_SingularisDeclareDependency.h"
#include "Objects/SingularisCombineBase.h"
#include "Types/SingularisCombineDependencyList.h"
#include "Types/SingularisCombineDependencyScope.h"

namespace
{
	/** 声明集合是否等价（作用域键相同且每作用域类型集合一致，忽略顺序） */
	bool AreDeclarationsEquivalent(
		const TMap<ESingularisCombineDependencyScope, FSingularisCombineDependencyList>& A,
		const TMap<ESingularisCombineDependencyScope, FSingularisCombineDependencyList>& B
	)
	{
		if (A.Num() != B.Num())
			return false;

		for (const auto& Pair : A)
		{
			const FSingularisCombineDependencyList* const Found = B.Find(Pair.Key);
			if (!Found || Found->Classes.Num() != Pair.Value.Classes.Num())
				return false;

			for (const TSubclassOf<UActorComponent>& DepClass : Pair.Value.Classes)
			{
				if (!Found->Classes.Contains(DepClass))
					return false;
			}
		}

		return true;
	}
}

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

/**
 * CDO 编译完成回调：扫描蓝图全部图的声明节点，将声明回填至策略类 CDO 的 DeclaredDependencies
 * 重复声明（相同 (Scope, Class)）经 AddUnique 去重，编译产物只保留一份；
 * 仅收录未连接的静态声明，连接的动态值编译期不可知，不入 CDO。
 * 值未变化时跳过，变化时回填并标记蓝图待保存（幂等，避免反复编译累积重复声明）。
 * @param CDO      刚编译完成的类默认对象
 * @param Context  编译上下文（含骨架编译标志）
 */
void FSingularisCombineBlueprintCompileHook::HandleCDOCompiled(
	// ReSharper disable once CppParameterMayBeConstPtrOrRef
	UObject* CDO,
	const FObjectPostCDOCompiledContext& Context
)
{
	// 1) 仅全量编译后的产物 CDO 参与回填，跳过骨架类编译
	if (Context.bIsSkeletonOnly)
		return;

	// 2) 仅处理 USingularisCombine 派生蓝图
	const UClass* GeneratedClass = CDO ? CDO->GetClass() : nullptr;
	if (!GeneratedClass || !GeneratedClass->IsChildOf(USingularisCombine::StaticClass()))
		return;

	const UBlueprint* Blueprint = UBlueprint::GetBlueprintFromClass(GeneratedClass);
	if (!Blueprint)
		return;

	UE_LOG(
		LogSingularisCombine,
		Display,
		TEXT("HandleCDOCompiled：开始扫图回填（蓝图=%s, 类=%s）"),
		*GetNameSafe(Blueprint),
		*GetNameSafe(GeneratedClass)
	);

	// 3) 扫描所有图中的声明节点，按 (Scope, Class) 去重收集
	TArray<UEdGraph*> AllGraphs;
	Blueprint->GetAllGraphs(AllGraphs);

	TMap<ESingularisCombineDependencyScope, FSingularisCombineDependencyList> Backfilled;
	auto NodeCount = 0;
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

			++NodeCount;

			// 仅收录未连接的静态声明（连接引脚的动态值编译期不可知，不入 CDO）
			const UEdGraphPin* ScopePin = Node->FindPin(UK2Node_SingularisDeclareDependency::GetScopePinName());
			const UEdGraphPin* ClassPin = Node->FindPin(
				UK2Node_SingularisDeclareDependency::GetComponentClassPinName()
			);
			if (!ScopePin || !ClassPin || ScopePin->LinkedTo.Num() > 0 || ClassPin->LinkedTo.Num() > 0)
			{
				UE_LOG(
					LogSingularisCombine,
					Warning,
					TEXT("HandleCDOCompiled：跳过已连接的声明节点（节点=%s）"),
					*GetNameSafe(Node)
				);
				continue;
			}

			// 组件类未配置或作用域枚举名无效时跳过该节点
			UClass* ComponentType = Cast<UClass>(ClassPin->DefaultObject);
			if (!ComponentType)
			{
				UE_LOG(
					LogSingularisCombine,
					Warning,
					TEXT("HandleCDOCompiled：声明节点未配置组件类（节点=%s）"),
					*GetNameSafe(Node)
				);
				continue;
			}

			const int64 ScopeValue = StaticEnum<ESingularisCombineDependencyScope>()->GetValueByName(
				FName(ScopePin->DefaultValue)
			);
			if (ScopeValue == INDEX_NONE)
			{
				UE_LOG(
					LogSingularisCombine,
					Warning,
					TEXT("HandleCDOCompiled：声明节点作用域枚举值无效（节点=%s, DefaultValue=%s）"),
					*GetNameSafe(Node),
					*ScopePin->DefaultValue
				);
				continue;
			}

			Backfilled.FindOrAdd(static_cast<ESingularisCombineDependencyScope>(ScopeValue)).Classes.AddUnique(
				ComponentType
			);
		}
	}

	UE_LOG(
		LogSingularisCombine,
		Display,
		TEXT("HandleCDOCompiled：扫描完成（节点=%d, 作用域=%d）"),
		NodeCount,
		Backfilled.Num()
	);

	// 4) 回填策略类 CDO：该 UPROPERTY 随蓝图资产序列化并参与烘焙，打包版本运行时无需编辑器即可读取。
	//    值未变化时跳过（避免编辑器加载误标脏）；变化时回填并标记蓝图待保存，使升级后的旧资产重新落盘。
	USingularisCombine* const StrategyCDO = Cast<USingularisCombine>(CDO);
	if (!StrategyCDO || AreDeclarationsEquivalent(StrategyCDO->DeclaredDependencies, Backfilled))
		return;

	StrategyCDO->DeclaredDependencies = MoveTemp(Backfilled);
	StrategyCDO->MarkPackageDirty();

	UE_LOG(
		LogSingularisCombine,
		Display,
		TEXT("HandleCDOCompiled：回填 CDO 声明（CDO=%s, 作用域=%d）"),
		*StrategyCDO->GetPathName(),
		StrategyCDO->DeclaredDependencies.Num()
	);
}
