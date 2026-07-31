#include "Factories/SingularisCombineFactory.h"

#include <Kismet2/KismetEditorUtilities.h>
#include <Objects/SingularisCombineBase.h>

USingularisCombineFactory::USingularisCombineFactory()
{
	bCreateNew = true;
	bEditAfterNew = true;
	SupportedClass = USingularisCombine::StaticClass();
}

UObject* USingularisCombineFactory::FactoryCreateNew(
	UClass* InClass,
	UObject* InParent,
	const FName InName,
	const EObjectFlags Flags,
	UObject* Context,
	FFeedbackContext* Warn
)
{
	// 1) 利用 KismetEditorUtilities 自动生成蓝图资产
	// 2) 强制将其基类指派为最新的引力奇点化合基础类 USingularisCombine
	return FKismetEditorUtilities::CreateBlueprint(
		USingularisCombine::StaticClass(),
		InParent,
		InName,
		BPTYPE_Normal,
		UBlueprint::StaticClass(),
		UBlueprintGeneratedClass::StaticClass(),
		NAME_None
	);
}

bool USingularisCombineFactory::ShouldShowInNewMenu() const
{
	return true;
}

UClass* FAssetTypeActions_SingularisCombine::GetSupportedClass() const
{
	return USingularisCombine::StaticClass();
}
