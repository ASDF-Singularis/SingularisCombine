#pragma once

#include <CoreMinimal.h>
#include <AssetTypeActions/AssetTypeActions_Blueprint.h>
#include <Factories/Factory.h>

#include "SingularisCombineFactory.generated.h"

/**
 * 引力奇点化合工厂类
 */
UCLASS()
class SINGULARISCOMBINEEDITOR_API USingularisCombineFactory : public UFactory
{
	GENERATED_BODY()

public:
	USingularisCombineFactory();

	virtual UObject* FactoryCreateNew(
		UClass* InClass,
		UObject* InParent,
		FName InName,
		EObjectFlags Flags,
		UObject* Context,
		FFeedbackContext* Warn
	) override;

	virtual bool ShouldShowInNewMenu() const override;
};

/**
 * 引力奇点化合资产类型操作 (定义编辑器右键菜单行为)
 */
class FAssetTypeActions_SingularisCombine : public FAssetTypeActions_Blueprint
{
public:
	explicit FAssetTypeActions_SingularisCombine(const EAssetTypeCategories::Type InAssetCategory)
		: AssetTypeCategory(InAssetCategory) {}

	virtual FText GetName() const override
	{
		return NSLOCTEXT(
			"SingularisCombineEditor",
			"AssetTypeActions_SingularisCombine",
			"Singularis Combine"
		);
	}

	virtual FColor GetTypeColor() const override { return FColor(63, 126, 255); }

	virtual UClass* GetSupportedClass() const override;

	virtual UFactory* GetFactoryForBlueprintType(UBlueprint* InBlueprint) const override
	{
		// 1) 动态实例化工厂对象以接管该资产蓝图的创建流程
		USingularisCombineFactory* Factory = NewObject<USingularisCombineFactory>();
		return Factory;
	}

	virtual uint32 GetCategories() override { return AssetTypeCategory; }

	virtual const TArray<FText>& GetSubMenus() const override
	{
		// 1) 将资产收纳至右键菜单的指定子目录中
		static const TArray SubMenus = {
			FText::FromString("SingularisCombine"),
		};

		return SubMenus;
	}

private:
	EAssetTypeCategories::Type AssetTypeCategory;
};
