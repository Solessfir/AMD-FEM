// Copyright Epic Games, Inc. All Rights Reserved.

#include "FEMEditor.h"
#include "AssetToolsModule.h"
#include "FEMActions.h"

void FFEMEditorModule::StartupModule()
{
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	RegisterAssetTypeAction(AssetTools, MakeShareable(new FFEMAssetActions));
	RegisterAssetTypeAction(AssetTools, MakeShareable(new FFEMTetMeshMaterialActions));
	RegisterAssetTypeAction(AssetTools, MakeShareable(new FFEMMeshActions));
}

void FFEMEditorModule::ShutdownModule()
{
	if (const FAssetToolsModule* AssetToolsModule = FModuleManager::GetModulePtr<FAssetToolsModule>("AssetTools"))
	{
		IAssetTools& AssetTools = AssetToolsModule->Get();
		for (const auto& Action : RegisteredAssetTypeActions)
		{
			AssetTools.UnregisterAssetTypeActions(Action);
		}
	}
}

void FFEMEditorModule::RegisterAssetTypeAction(IAssetTools& AssetTools, const TSharedRef<IAssetTypeActions>& Action)
{
	AssetTools.RegisterAssetTypeActions(Action);
	RegisteredAssetTypeActions.Add(Action);
}

IMPLEMENT_MODULE(FFEMEditorModule, FEMEditor)
