#include "SingularisCombineEditor.h"

#include <AssetToolsModule.h>

#include "Factories/SingularisCombineFactory.h"
#include "Nodes/SingularisCombineBlueprintCompileHooks.h"

#define LOCTEXT_NAMESPACE "FSingularisCombineEditorModule"

void FSingularisCombineEditorModule::StartupModule()
{
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();

	const EAssetTypeCategories::Type SingularisPluginCategory = AssetTools.RegisterAdvancedAssetCategory(
		FName("Singularis"),
		LOCTEXT("SingularisCategory", "Singularis")
	);

	RegisterAssetTypeAction(
		AssetTools,
		MakeShareable(new FAssetTypeActions_SingularisCombine(SingularisPluginCategory))
	);

	FSingularisCombineBlueprintCompileHook::Register(BlueprintCompileHandle);
}

void FSingularisCombineEditorModule::ShutdownModule()
{
	FSingularisCombineBlueprintCompileHook::Unregister(BlueprintCompileHandle);

	if (FModuleManager::Get().IsModuleLoaded("AssetTools"))
	{
		IAssetTools& AssetTools = FModuleManager::GetModuleChecked<FAssetToolsModule>("AssetTools").Get();

		for (auto Action : CreatedAssetTypeActions)
			AssetTools.UnregisterAssetTypeActions(Action.ToSharedRef());
	}

	CreatedAssetTypeActions.Empty();
}

void FSingularisCombineEditorModule::RegisterAssetTypeAction(
	IAssetTools& AssetTools,
	const TSharedRef<IAssetTypeActions>& Action
)
{
	AssetTools.RegisterAssetTypeActions(Action);
	CreatedAssetTypeActions.Add(Action);
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FSingularisCombineEditorModule, SingularisCombineEditor)
