//---------------------------------------------------------------------------------------
//
// Copyright (c) 2019 Advanced Micro Devices, Inc. All rights reserved.
//
//---------------------------------------------------------------------------------------
#include "FEMEditorPCH.h"
#include "FEMTetMeshParametersFactory.h"

#include "Editor.h"
#include "ActorFactories/ActorFactoryEmptyActor.h"
#include "AssetRegistryModule.h"
#include "BlueprintEditorUtils.h"
#include "ObjectTools.h"
#include "PackageTools.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "AssetSelection.h"

#include "ConstructorHelpers.h"
#include "FEMFXTetMeshParameters.h"

#include "Developer/AssetTools/Public/AssetTypeCategories.h"

UFEMTetMeshParametersFactory::UFEMTetMeshParametersFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SupportedClass = UFEMFXTetMeshParameters::StaticClass();

	bCreateNew = true;
	bEditorImport = true;
	bEditAfterNew = true;
}

uint32 UFEMTetMeshParametersFactory::GetMenuCategories() const
{
	return EAssetTypeCategories::Misc;
}

bool UFEMTetMeshParametersFactory::ShouldShowInNewMenu() const
{
	return true;
}

UObject* UFEMTetMeshParametersFactory::FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn, FName CallingContext)
{
	return NewObject<UFEMFXTetMeshParameters>(InParent, InClass, InName, Flags);
}