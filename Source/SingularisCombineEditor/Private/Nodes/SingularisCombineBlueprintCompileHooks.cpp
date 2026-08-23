#include "Nodes/SingularisCombineBlueprintCompileHooks.h"

#include <EdGraph/EdGraph.h>
#include <EdGraph/EdGraphPin.h>
#include <Engine/Blueprint.h>
#include <UObject/UObjectGlobals.h>

#include "SingularisCombine.h"
#include "Nodes/K2Node_SingularisDeclareDependency.h"
#include "Objects/SingularisCombineBase.h"
#include "Types/SingularisCombineDependencyList.h"
#include "Types/SingularisCombineDependencyRegistry.h"
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

/**
 * CDO 编译完成回调：扫描蓝图全部图的声明节点，将声明写入全局依赖注册表
 * 重复声明（相同 (Scope, Class)）经 AddUnique 去重，编译产物只保留一份；
 * 仅收录未连接的静态声明，连接的动态值编译期不可知，不入注册表。
 * 注册表为唯一真相源，整体替换语义保证幂等：每次编译全量重建后赋值，避免反复编译累积重复声明。
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

			// 仅收录未连接的静态声明（连接引脚的动态值编译期不可知，不入注册表）
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

	// 4) 整体替换该策略类在注册表中的声明集合（唯一真相源，幂等）
	const auto TargetClass = Blueprint->GeneratedClass.Get();
	UE_LOG(
		LogSingularisCombine,
		Display,
		TEXT("HandleCDOCompiled：写入注册表（写入指针=%p, GetPathName=%s, 作用域=%d）"),
		TargetClass,
		*TargetClass->GetPathName(),
		Backfilled.Num()
	);

	FSingularisCombineDependencyRegistry::Get().ReplaceDeclaredClasses(
		TargetClass,
		MoveTemp(Backfilled)
	);
}
