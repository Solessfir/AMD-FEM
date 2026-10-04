// Copyright 1998-2017 Epic Games, Inc. All Rights Reserved.

#include "FEMMeshFactory.h"
#include "FEMMesh.h"
#include "EditorStyleSet.h"
#include "Framework/Application/SlateApplication.h"
#include "IDetailsView.h"
#include "PropertyEditorModule.h"
#include "SlateOptMacros.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SWindow.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Interfaces/IMainFrameModule.h"
#include "AssetTypeCategories.h"
#include "Misc/ConfigCacheIni.h"
#include "UObject/StrongObjectPtr.h"

#define LOCTEXT_NAMESPACE "FEMMeshFactory"

UFEMMeshImportData::UFEMMeshImportData(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bProceduralGenerate = true;
	Randomize = false;
	NumCubesX = NumCubesY = NumCubesZ = 1;
	CubeX = CubeY = CubeZ = 1.0f;
	Scale = 1.0f;
	IsWoodPanel = false;
}

void UFEMMeshImportData::CopyFrom(const UFEMMeshImportData* Other)
{
	bProceduralGenerate = Other->bProceduralGenerate;
	Randomize = Other->Randomize;
	NumCubesX = Other->NumCubesX;
	NumCubesY = Other->NumCubesY;
	NumCubesZ = Other->NumCubesZ;

	CubeX = Other->CubeX;
	CubeY = Other->CubeY;
	CubeZ = Other->CubeZ;

	Scale = Other->Scale;

	IsWoodPanel = Other->IsWoodPanel;
}


void UFEMMeshImportData::LoadOptions()
{
	LoadConfig();
}

void UFEMMeshImportData::SaveOptions()
{
	SaveConfig(CPF_Config, nullptr, GConfig, false);
}


static ProceduralMeshOptions GetProceduralOptions(const UFEMMeshImportData& Data)
{
	ProceduralMeshOptions Options;
	Options.Randomize = Data.Randomize;
	Options.NumCubesX = Data.NumCubesX;
	Options.NumCubesY = Data.NumCubesY;
	Options.NumCubesZ = Data.NumCubesZ;
	Options.CubeX = Data.CubeX;
	Options.CubeY = Data.CubeY;
	Options.CubeZ = Data.CubeZ;
	Options.Scale = Data.Scale;
	Options.IsWoodPanel = Data.IsWoodPanel;
	return Options;
}

/** UI to pick options when importing  FEMMesh */
BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION
class SFEMMeshImportOptions : public SCompoundWidget
{
public:
	TStrongObjectPtr<UFEMMeshImportData> FEMMeshImportData;

	/** Whether we should go ahead with import */
	bool bImport = false;

	// Window That Owns Us
	TSharedPtr<SWindow> WidgetWindow;

	TSharedPtr<IDetailsView> DetailsView;

	SLATE_BEGIN_ARGS(SFEMMeshImportOptions)
		: _WidgetWindow()
		, _ReimportAssetData(nullptr)
	{}
		SLATE_ARGUMENT(TSharedPtr<SWindow>, WidgetWindow)
		SLATE_ARGUMENT(UFEMMeshImportData*, ReimportAssetData)
	SLATE_END_ARGS()


	SFEMMeshImportOptions()
	{
		DetailsView = nullptr;
		FEMMeshImportData.Reset(NewObject<UFEMMeshImportData>());
	}

	void Construct(const FArguments& InArgs)
	{
		WidgetWindow = InArgs._WidgetWindow;
		UFEMMeshImportData* ReimportAssetData = InArgs._ReimportAssetData;

		if (ReimportAssetData != nullptr)
		{
			FEMMeshImportData->CopyFrom(ReimportAssetData);
		}
		else
		{
			FEMMeshImportData->LoadOptions();
		}
		TSharedPtr<SBox> InspectorBox;

		//Create A Widget
		this->ChildSlot
			[
				SNew(SBorder)
				.BorderImage(FEditorStyle::GetBrush(TEXT("Menu.Background")))
				.Content()
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(2)
					[
						SAssignNew(InspectorBox, SBox)
						.MaxDesiredHeight(650.0f)
						.WidthOverride(400.0)
					]
					// Ok/Cancel
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(5)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.HAlign(HAlign_Left)
						[
							//Left Button array
							SNew(SUniformGridPanel)
							.SlotPadding(3)
							+ SUniformGridPanel::Slot(0, 0)
							[
								SNew(SButton)
								.Text(LOCTEXT("FEMMeshOptionWindow_ResetToDefault", "Reset to Default"))
								.OnClicked(this, &SFEMMeshImportOptions::OnResetToDefault)
							]
						]
						+ SHorizontalBox::Slot()
						.FillWidth(1.0f)
						.HAlign(HAlign_Right)
						[
							//Right button array
							SNew(SUniformGridPanel)
							.SlotPadding(3)
							+ SUniformGridPanel::Slot(0, 0)
							[
								SNew(SButton)
								.Text(LOCTEXT("SpeedTreeOptionWindow_Import", "Import"))
								.IsEnabled(this, &SFEMMeshImportOptions::CanImport)
								.OnClicked(this, &SFEMMeshImportOptions::OnImport)
							]
							+ SUniformGridPanel::Slot(1, 0)
							[
								SNew(SButton)
								.Text(LOCTEXT("SpeedTreeOptionWindow_Cancel", "Cancel"))
								.OnClicked(this, &SFEMMeshImportOptions::OnCancel)
							]
						]
					]
				]
			];
		FPropertyEditorModule& PropertyEditorModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
		FDetailsViewArgs DetailsViewArgs;
		DetailsViewArgs.bAllowSearch = false;
		DetailsViewArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
		DetailsView = PropertyEditorModule.CreateDetailView(DetailsViewArgs);
		InspectorBox->SetContent(DetailsView->AsShared());
		DetailsView->SetObject(FEMMeshImportData.Get());
	}

	bool CanImport() const
	{
		return FEMMeshImportData->bProceduralGenerate && UFEMMesh::ValidateProceduralMeshOptions(GetProceduralOptions(*FEMMeshImportData));
	}

	/** If we should import */
	bool ShouldImport()
	{
		return bImport;
	}

	/** Called when 'OK' button is pressed */
	FReply OnImport()
	{
		if (CanImport())
		{
			bImport = true;
			WidgetWindow->RequestDestroyWindow();
		}
		return FReply::Handled();
	}

	FReply OnResetToDefault()
	{
		if (DetailsView.IsValid())
		{
			FEMMeshImportData->CopyFrom(GetDefault<UFEMMeshImportData>());
			DetailsView->SetObject(FEMMeshImportData.Get(), true);
		}
		return FReply::Handled();
	}

	/** Called when 'Cancel' button is pressed */
	FReply OnCancel()
	{
		bImport = false;
		WidgetWindow->RequestDestroyWindow();
		return FReply::Handled();
	}


};
END_SLATE_FUNCTION_BUILD_OPTIMIZATION



UFEMMeshFactory::UFEMMeshFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SupportedClass = UFEMMesh::StaticClass();

	bCreateNew = true;
	bEditorImport = true;
	bEditAfterNew = true;
}

UObject* UFEMMeshFactory::FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn, FName CallingContext)
{
	TSharedPtr<SWindow> ParentWindow;
	if (FModuleManager::Get().IsModuleLoaded("MainFrame"))
	{
		IMainFrameModule& MainFrame = FModuleManager::LoadModuleChecked<IMainFrameModule>("MainFrame");
		ParentWindow = MainFrame.GetParentWindow();
	}

	TSharedPtr<SFEMMeshImportOptions> Options;
	TSharedRef<SWindow> Window = SNew(SWindow)
		.Title(LOCTEXT("WindowTitle", "FEM Options"))
		.SizingRule(ESizingRule::Autosized);
	Window->SetContent(SAssignNew(Options, SFEMMeshImportOptions).WidgetWindow(Window));
	FSlateApplication::Get().AddModalWindow(Window, ParentWindow, false);
	if (!Options->ShouldImport())
	{
		return nullptr;
	}

	UFEMMesh* Mesh = CreateMesh(InClass, InParent, InName, Flags, Options->FEMMeshImportData.Get());
	if (Mesh)
	{
		Options->FEMMeshImportData->SaveOptions();
	}
	return Mesh;
}

UFEMMesh* UFEMMeshFactory::CreateMesh(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, const UFEMMeshImportData* Data)
{
	if (!Data || !Data->bProceduralGenerate)
	{
		return nullptr;
	}
	const ProceduralMeshOptions Options = GetProceduralOptions(*Data);
	if (!UFEMMesh::ValidateProceduralMeshOptions(Options))
	{
		return nullptr;
	}
	UFEMMesh* Mesh = NewObject<UFEMMesh>(InParent, InClass, InName, Flags);
	return Mesh->CreateProceduralMesh(Options) ? Mesh : nullptr;
}

uint32 UFEMMeshFactory::GetMenuCategories() const
{
	return EAssetTypeCategories::Misc;
}

bool UFEMMeshFactory::ShouldShowInNewMenu() const
{
	return true;
}
