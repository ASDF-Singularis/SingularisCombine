#pragma once

#include <CoreMinimal.h>
#include <IAssetTypeActions.h>
#include <Modules/ModuleManager.h>

class IAssetTools;

class FSingularisCombineEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	TArray<TSharedPtr<IAssetTypeActions>> CreatedAssetTypeActions{};

	void RegisterAssetTypeAction(IAssetTools& AssetTools, const TSharedRef<IAssetTypeActions>& Action);
};
