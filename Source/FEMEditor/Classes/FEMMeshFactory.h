//---------------------------------------------------------------------------------------
//
// Copyright (c) 2019 Advanced Micro Devices, Inc. All rights reserved.
//
//---------------------------------------------------------------------------------------

#pragma once

#include "UObject/ObjectMacros.h"
#include "EditorFramework/AssetImportData.h"
#include "Factories/Factory.h"
#include "FEMMeshFactory.generated.h"

UCLASS(config = EditorPerProjectUserSettings, MinimalAPI)
class UFEMMeshImportData : public UAssetImportData
{
public:

	GENERATED_UCLASS_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = "FEM", meta = (InlineEditConditionToggle))
	bool bProceduralGenerate;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = "FEM", meta = (EditCondition = "bProceduralGenerate"))
	bool Randomize;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = "FEM", meta = (EditCondition = "bProceduralGenerate", ClampMin = "1", ClampMax = "4095", UIMin = "1", UIMax = "16"))
	int NumCubesX;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = "FEM", meta = (EditCondition = "bProceduralGenerate", ClampMin = "1", ClampMax = "4095", UIMin = "1", UIMax = "16"))
	int NumCubesY;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = "FEM", meta = (EditCondition = "bProceduralGenerate", ClampMin = "1", ClampMax = "4095", UIMin = "1", UIMax = "16"))
	int NumCubesZ;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = "FEM", meta = (EditCondition = "bProceduralGenerate", ClampMin = "0.0001", UIMin = "0.01", UIMax = "100.0"))
	float CubeX;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = "FEM", meta = (EditCondition = "bProceduralGenerate", ClampMin = "0.0001", UIMin = "0.01", UIMax = "100.0"))
	float CubeY;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = "FEM", meta = (EditCondition = "bProceduralGenerate", ClampMin = "0.0001", UIMin = "0.01", UIMax = "100.0"))
	float CubeZ;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = "FEM", meta = (EditCondition = "bProceduralGenerate", ClampMin = "0.0001", UIMin = "0.01", UIMax = "100.0"))
	float Scale;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = "FEM", meta = (EditCondition = "bProceduralGenerate"))
	bool IsWoodPanel;

	void CopyFrom(const UFEMMeshImportData* Other);
	void SaveOptions();

	void LoadOptions();

};



UCLASS()
class UFEMMeshFactory : public UFactory
{
	GENERATED_UCLASS_BODY()

	virtual UObject* FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn, FName CallingContext) override;

	class UFEMMesh* CreateMesh(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, const UFEMMeshImportData* Data);

	uint32 GetMenuCategories() const override;
	bool ShouldShowInNewMenu() const override;
};
