#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AssetRegistryModule.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "FEMActor.h"
#include "FEMFactory.h"
#include "FEMFileImportFactory.h"
#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/FeedbackContext.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectHash.h"
#include "UObject/UObjectIterator.h"

namespace
{
	class FFEMCompatibilityFeedback : public FFeedbackContext
	{
	public:
		int32 NumErrors = 0;
		FString LastError;

		virtual void Serialize(const TCHAR* Message, ELogVerbosity::Type Verbosity, const FName& Category) override
		{
			NumErrors += Verbosity == ELogVerbosity::Error;
			if (Verbosity == ELogVerbosity::Error)
				LastError = Message;
		}
	};

	int32 CountLiveActors(UWorld* World)
	{
		int32 Count = 0;
		for (AActor* Actor : World->PersistentLevel->Actors)
			Count += IsValid(Actor);
		return Count;
	}

	void CleanImportPackages(const FString& Prefix)
	{
		for (TObjectIterator<UPackage> It; It; ++It)
		{
			if (!It->GetName().StartsWith(Prefix))
				continue;
			It->SetDirtyFlag(false);
			TArray<UObject*> Objects;
			GetObjectsWithOuter(*It, Objects, true);
			for (UObject* Object : Objects)
			{
				if (Object->IsAsset())
				{
					if (GEditor != nullptr)
					{
						if (UAssetEditorSubsystem* AssetEditors = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>())
							AssetEditors->CloseAllEditorsForAsset(Object);
					}
					FAssetRegistryModule::AssetDeleted(Object);
				}
				Object->ClearFlags(RF_Public | RF_Standalone);
				Object->SetFlags(RF_Transient);
			}
			It->SetDirtyFlag(false);
			It->SetFlags(RF_Transient);
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFEMImportCompatibilityTest, "FEM.Import.FBXFieldCompatibility", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFEMImportCompatibilityTest::RunTest(const FString& Parameters)
{
	UWorld* World = GEditor != nullptr ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("Editor world"), World))
		return false;

	const FString Prefix = FString(TEXT("/Temp/FEMCompatibility_")) + FGuid::NewGuid().ToString(EGuidFormats::Digits);
	const FString Filename = FPaths::CreateTempFilename(*FPaths::ProjectSavedDir(), TEXT("FEMCompatibility-"), TEXT(".fem"));
	const bool bWorldWasDirty = World->GetOutermost()->IsDirty();
	TSet<AActor*> OriginalActors;
	for (AActor* Actor : World->PersistentLevel->Actors)
		if (IsValid(Actor))
			OriginalActors.Add(Actor);
	ON_SCOPE_EXIT
	{
		const TArray<AActor*> Actors = World->PersistentLevel->Actors;
		for (AActor* Actor : Actors)
			if (IsValid(Actor) && !OriginalActors.Contains(Actor))
				World->EditorDestroyActor(Actor, true);
		CleanImportPackages(Prefix);
		World->GetOutermost()->SetDirtyFlag(bWorldWasDirty);
		IFileManager::Get().Delete(*Filename);
	};

	TStrongObjectPtr<UFEMFactory> Factory(NewObject<UFEMFactory>());
	Factory->bEditAfterNew = false;
	int32 CaseIndex = 0;
	const auto CheckImport = [&](const FString& Name, const FString& Contents, bool bExpectedSuccess) -> UBlueprint*
	{
		if (!TestTrue(Name + TEXT(" writes temporary file"), FFileHelper::SaveStringToFile(Contents, *Filename)))
			return nullptr;
		UPackage* Package = CreatePackage(*(Prefix + FString::FromInt(CaseIndex++)));
		FFEMCompatibilityFeedback Feedback;
		bool bCanceled = false;
		UObject* Imported = Factory->FactoryCreateFile(AFEMActor::StaticClass(), Package, TEXT("CompatibilityMesh"), RF_Transient, Filename, nullptr, &Feedback, bCanceled);
		TestEqual(Name + TEXT(" cancellation"), bCanceled, !bExpectedSuccess);
		TestEqual(Name + TEXT(" error count"), Feedback.NumErrors, bExpectedSuccess ? 0 : 1);
		if (bExpectedSuccess)
		{
			UBlueprint* Blueprint = Cast<UBlueprint>(Imported);
			if (TestNotNull(Name + TEXT(" creates Blueprint"), Blueprint) && TestNotNull(Name + TEXT(" generated class"), Blueprint->GeneratedClass.Get()))
			{
				AFEMActor* Defaults = Cast<AFEMActor>(Blueprint->GeneratedClass->GetDefaultObject());
				if (TestNotNull(Name + TEXT(" FEM actor defaults"), Defaults))
				{
					TestEqual(Name + TEXT(" imports one component"), Defaults->ComponentResources.Num(), 1);
					if (Defaults->ComponentResources.Num() == 1)
					{
						TestEqual(Name + TEXT(" imports tetrahedral vertices"), Defaults->ComponentResources[0].NumVerts, 4);
						TestEqual(Name + TEXT(" imports tetrahedra"), Defaults->ComponentResources[0].NumTets, 1);
						TestEqual(Name + TEXT(" imports selected FBX array"), Defaults->ComponentResources[0].FBXFiles.Num(), 0);
					}
				}
			}
		}
		else
		{
			TestNull(Name + TEXT(" rejects import"), Imported);
			TArray<UObject*> Objects;
			GetObjectsWithOuter(Package, Objects, true);
			TestEqual(Name + TEXT(" creates no partial objects"), Objects.Num(), 0);
			TestFalse(Name + TEXT(" preserves clean package"), Package->IsDirty());
		}
		TestEqual(Name + TEXT(" preserves live actors"), CountLiveActors(World), OriginalActors.Num());
		CleanImportPackages(Prefix);
		World->GetOutermost()->SetDirtyFlag(bWorldWasDirty);
		return Cast<UBlueprint>(Imported);
	};

	const FString Component = TEXT("{\"Name\":\"Mesh\",\"NumFBXFiles\":0,\"NumCornersPerShard\":0,\"CollisionGroup\":0,\"NumTags\":0,\"NumMaterials\":0,\"IsFracturable\":false,\"Tags\":[],\"Materials\":[],\"FbxFiles\":[],\"RenderMesh\":[],\"Node\":{\"IsBoundaryMarker\":false,\"NumAttributes\":0,\"NumDimensions\":3,\"NumPoints\":4,\"Data\":[1,0,0,0,2,1,0,0,3,0,1,0,4,0,0,1]},\"Ele\":{\"IsRegionAttribute\":false,\"NumNodesPerTets\":4,\"NumTetrahedra\":1,\"Data\":[{\"TetIndex\":1,\"Indices\":[1,2,3,4]}]}}");
	const auto Document = [](const FString& Mesh)
	{
		return FString::Printf(TEXT("{\"Version\":\"1.0.0\",\"FEMMeshComponents\":[%s],\"RigidBodies\":[],\"RBAngleConstraints\":[],\"GlueConstraints\":[],\"PlaneConstraints\":[]}"), *Mesh);
	};
	for (const TCHAR* Field : { TEXT("FbxFiles"), TEXT("fbxFiles") })
	{
		const FString Mesh = Component.Replace(TEXT("FbxFiles"), Field);
		const FString ArrayField = FString::Printf(TEXT("\"%s\":[]"), Field);
		CheckImport(Field, Document(Mesh), true);
		CheckImport(FString(Field) + TEXT(" is not an array"), Document(Mesh.Replace(*ArrayField, *(FString::Printf(TEXT("\"%s\":null"), Field)))), false);
		CheckImport(FString(Field) + TEXT(" count mismatch"), Document(Mesh.Replace(TEXT("\"NumFBXFiles\":0"), TEXT("\"NumFBXFiles\":1"))), false);
		CheckImport(FString(Field) + TEXT(" non-string reference"), Document(Mesh.Replace(TEXT("\"NumFBXFiles\":0"), TEXT("\"NumFBXFiles\":1")).Replace(*ArrayField, *(FString::Printf(TEXT("\"%s\":[42]"), Field)))), false);
		CheckImport(FString(Field) + TEXT(" empty reference"), Document(Mesh.Replace(TEXT("\"NumFBXFiles\":0"), TEXT("\"NumFBXFiles\":1")).Replace(*ArrayField, *(FString::Printf(TEXT("\"%s\":[\"\"]"), Field)))), false);

		const FString WithReferences = Document(Mesh.Replace(TEXT("\"NumFBXFiles\":0"), TEXT("\"NumFBXFiles\":2")).Replace(*ArrayField, *(FString::Printf(TEXT("\"%s\":[\"First.fbx\",\"Second.fbx\"]"), Field))));
		FEMFileImportInputs Inputs;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(WithReferences);
		if (TestTrue(FString(Field) + TEXT(" reference fixture parses"), FJsonSerializer::Deserialize(Reader, Inputs.JsonObject)))
		{
			TStrongObjectPtr<UFEMResource> Resource(NewObject<UFEMResource>(GetTransientPackage(), NAME_None, RF_Transient));
			Inputs.Resource = Resource.Get();
			TUniquePtr<FEMFileImportFactory> FileFactory = FEMFileImportFactory::GetFactory(TEXT("1.0.0"));
			FileFactory->ImportFEMFile(&Inputs);
			TestEqual(FString(Field) + TEXT(" parses one component"), Inputs.Resource->ComponentResources.Num(), 1);
			if (Inputs.Resource->ComponentResources.Num() == 1)
			{
				const TArray<FString>& References = Inputs.Resource->ComponentResources[0].FBXFiles;
				TestEqual(FString(Field) + TEXT(" preserves both references"), References.Num(), 2);
				if (References.Num() == 2)
				{
					TestEqual(FString(Field) + TEXT(" first reference"), References[0], FString(TEXT("First.fbx")));
					TestEqual(FString(Field) + TEXT(" second reference"), References[1], FString(TEXT("Second.fbx")));
				}
			}
		}
	}
	CheckImport(TEXT("Missing aliases"), Document(Component.Replace(TEXT("\"FbxFiles\":[],"), TEXT(""))), false);
	CheckImport(TEXT("Legacy numeric zero flags"), Document(Component.Replace(TEXT(":false"), TEXT(":0"))), true);
	CheckImport(TEXT("Legacy numeric fracture flag"), Document(Component.Replace(TEXT("\"IsFracturable\":false"), TEXT("\"IsFracturable\":1"))), true);
	for (const TCHAR* InvalidFlag : { TEXT("2"), TEXT("-1"), TEXT("\"true\"") })
		CheckImport(FString(TEXT("Reject invalid flag ")) + InvalidFlag, Document(Component.Replace(TEXT("\"IsFracturable\":false"), *(FString(TEXT("\"IsFracturable\":")) + InvalidFlag))), false);
	const FString OrdinarySection = TEXT("{\"NumberOfShards\":3,\"AssignedTetFaceBuffer\":[],\"BarycentricCoordsBuffer\":[1,0,0,0,0,1,0,0,0,0,1,0],\"BarycentricPosIds\":[0,1,2],\"TetAssignmentBuffer\":[0,0,0],\"ColorBuffer\":[],\"NormalBuffer\":[],\"PositionBuffer\":[0,0,0,1,0,0,0,1,0],\"TangentBuffer\":[],\"UVsBuffer\":[],\"ShardIds\":[0,1,2],\"Triangles\":[0,1,2],\"Centroids\":[]}");
	UBlueprint* Ordinary = CheckImport(TEXT("Ordinary render layout"), Document(Component.Replace(TEXT("\"RenderMesh\":[]"), *(FString(TEXT("\"RenderMesh\":[")) + OrdinarySection + TEXT("]")))), true);
	if (Ordinary && Ordinary->GeneratedClass)
	{
		const AFEMActor* Defaults = Cast<AFEMActor>(Ordinary->GeneratedClass->GetDefaultObject());
		if (Defaults && Defaults->ComponentResources.Num() == 1)
		{
			const auto& Sections = Defaults->ComponentResources[0].meshSections;
			if (TestEqual(TEXT("Ordinary layout retains one section"), Sections.Num(), 1))
			{
				TestEqual(TEXT("Ordinary layout retains compact positions"), Sections[0].VertexPosition.Num(), 9);
				TestEqual(TEXT("Ordinary layout retains three triangle indices"), Sections[0].Triangles.Num(), 3);
				TestEqual(TEXT("Ordinary layout retains three shard IDs"), Sections[0].ShardVertexIds.Num(), 3);
				if (Sections[0].VertexPosition.Num() == 9 && Sections[0].Triangles.Num() == 3 && Sections[0].ShardVertexIds.Num() == 3)
				{
					const float ExpectedPositions[] = { 0, 0, 0, 1, 0, 0, 0, 1, 0 };
					for (int32 Index = 0; Index < 9; ++Index)
						TestEqual(FString::Printf(TEXT("Ordinary position %d remains unchanged"), Index), Sections[0].VertexPosition[Index], ExpectedPositions[Index]);
					for (int32 Index = 0; Index < 3; ++Index)
					{
						TestEqual(FString::Printf(TEXT("Ordinary triangle %d remains unchanged"), Index), Sections[0].Triangles[Index], Index);
						TestEqual(FString::Printf(TEXT("Ordinary shard %d remains unchanged"), Index), Sections[0].ShardVertexIds[Index], Index);
					}
				}
			}
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFEMImportAMDExampleTest, "FEM.Import.AMDExample", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFEMImportAMDExampleTest::RunTest(const FString& Parameters)
{
	FString Filename = Parameters.TrimStartAndEnd().TrimQuotes();
	if (Filename.IsEmpty())
		FParse::Value(FCommandLine::Get(), TEXT("FEMImportFixture="), Filename);
	if (Filename.IsEmpty())
	{
		AddInfo(TEXT("Pass a local FEM_SimpleSquare.fem path with -FEMImportFixture= to exercise the upstream AMD asset."));
		return true;
	}
	UWorld* World = GEditor != nullptr ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("Editor world"), World))
		return false;
	FString Contents;
	TSharedPtr<FJsonObject> Document;
	if (!TestTrue(TEXT("Read AMD example"), FFileHelper::LoadFileToString(Contents, *Filename))
		|| !TestTrue(TEXT("Parse AMD example"), FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Contents), Document)))
		return false;
	const TArray<TSharedPtr<FJsonValue>>* Components = nullptr;
	if (!TestTrue(TEXT("AMD example component array"), Document->TryGetArrayField(TEXT("FEMMeshComponents"), Components)))
		return false;
	if (!TestEqual(TEXT("AMD fixture has one component"), Components->Num(), 1))
		return false;
	for (const TSharedPtr<FJsonValue>& Value : *Components)
	{
		const TSharedPtr<FJsonObject> Component = Value.IsValid() && Value->Type == EJson::Object ? Value->AsObject() : nullptr;
		if (!TestTrue(TEXT("Fixture needs no external assets"), Component.IsValid()
			&& Component->HasTypedField<EJson::Number>(TEXT("NumFBXFiles")) && Component->GetIntegerField(TEXT("NumFBXFiles")) == 0
			&& Component->HasTypedField<EJson::Number>(TEXT("NumMaterials")) && Component->GetIntegerField(TEXT("NumMaterials")) == 0))
			return false;
	}
	const TArray<TSharedPtr<FJsonValue>>* SourceSections = nullptr;
	if (!TestTrue(TEXT("AMD fixture has one render section"), (*Components)[0]->AsObject()->TryGetArrayField(TEXT("RenderMesh"), SourceSections)
		&& SourceSections->Num() == 1 && (*SourceSections)[0].IsValid() && (*SourceSections)[0]->Type == EJson::Object))
		return false;
	const TSharedPtr<FJsonObject> SourceSection = (*SourceSections)[0]->AsObject();
	const TArray<TSharedPtr<FJsonValue>>* ExpandedPositions = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* ShardIds = nullptr;
	if (!TestTrue(TEXT("AMD fixture has expanded positions and shard IDs"), SourceSection->TryGetArrayField(TEXT("DebugExpandedPosBuffer"), ExpandedPositions)
		&& ExpandedPositions->Num() > 0 && SourceSection->TryGetArrayField(TEXT("ShardIds"), ShardIds)))
		return false;
	const FString Prefix = FString(TEXT("/Temp/FEMAMDExample_")) + FGuid::NewGuid().ToString(EGuidFormats::Digits);
	const bool bWorldWasDirty = World->GetOutermost()->IsDirty();
	TSet<AActor*> OriginalActors;
	for (AActor* Actor : World->PersistentLevel->Actors)
		if (IsValid(Actor))
			OriginalActors.Add(Actor);
	ON_SCOPE_EXIT
	{
		const TArray<AActor*> Actors = World->PersistentLevel->Actors;
		for (AActor* Actor : Actors)
			if (IsValid(Actor) && !OriginalActors.Contains(Actor))
				World->EditorDestroyActor(Actor, true);
		CleanImportPackages(Prefix);
		World->GetOutermost()->SetDirtyFlag(bWorldWasDirty);
	};
	TStrongObjectPtr<UFEMFactory> Factory(NewObject<UFEMFactory>());
	Factory->bEditAfterNew = false;
	UPackage* Package = CreatePackage(*Prefix);
	FFEMCompatibilityFeedback Feedback;
	bool bCanceled = false;
	UBlueprint* Blueprint = Cast<UBlueprint>(Factory->FactoryCreateFile(AFEMActor::StaticClass(), Package, TEXT("AMDExample"), RF_Transient, Filename, nullptr, &Feedback, bCanceled));
	TestFalse(FString(TEXT("AMD sample imports without cancellation: ")) + Feedback.LastError, bCanceled);
	TestEqual(FString(TEXT("AMD sample has no import errors: ")) + Feedback.LastError, Feedback.NumErrors, 0);
	if (TestNotNull(TEXT("AMD sample creates Blueprint"), Blueprint) && TestNotNull(TEXT("AMD sample generated class"), Blueprint->GeneratedClass.Get()))
	{
		AFEMActor* Defaults = Cast<AFEMActor>(Blueprint->GeneratedClass->GetDefaultObject());
		if (TestNotNull(TEXT("AMD sample FEM actor defaults"), Defaults))
		{
			TestEqual(TEXT("AMD sample has one component"), Defaults->ComponentResources.Num(), 1);
			if (Defaults->ComponentResources.Num() == 1)
			{
				TestEqual(TEXT("AMD sample has nine vertices"), Defaults->ComponentResources[0].NumVerts, 9);
				TestEqual(TEXT("AMD sample has twelve tetrahedra"), Defaults->ComponentResources[0].NumTets, 12);
				TestEqual(TEXT("AMD sample imports embedded render mesh"), Defaults->ComponentResources[0].meshSections.Num(), 1);
				if (Defaults->ComponentResources[0].meshSections.Num() == 1)
				{
					const auto& Section = Defaults->ComponentResources[0].meshSections[0];
					TestEqual(TEXT("AMD sample expands triangle corner positions"), Section.VertexPosition.Num(), 432);
					TestEqual(TEXT("AMD sample preserves all triangle corners"), Section.Triangles.Num(), 144);
					TestEqual(TEXT("AMD sample preserves corner shard assignments"), Section.ShardVertexIds.Num(), 144);
					for (int32 Index = 0; Index < Section.Triangles.Num(); ++Index)
						TestEqual(FString::Printf(TEXT("AMD sample expanded triangle index %d"), Index), Section.Triangles[Index], Index);
					const auto& SourcePositions = SourceSection->GetArrayField(TEXT("DebugExpandedPosBuffer"));
					const auto& SourceShards = SourceSection->GetArrayField(TEXT("ShardIds"));
					if (TestEqual(TEXT("Expanded position count matches source"), Section.VertexPosition.Num(), SourcePositions.Num()))
						for (int32 Index = 0; Index < SourcePositions.Num(); ++Index)
							TestEqual(FString::Printf(TEXT("Expanded position %d matches source"), Index), Section.VertexPosition[Index], static_cast<float>(SourcePositions[Index]->AsNumber()));
					if (TestEqual(TEXT("Corner shard count matches source"), Section.ShardVertexIds.Num(), SourceShards.Num()))
						for (int32 Index = 0; Index < SourceShards.Num(); ++Index)
							TestEqual(FString::Printf(TEXT("Corner shard %d matches source"), Index), Section.ShardVertexIds[Index], static_cast<int32>(SourceShards[Index]->AsNumber()));
				}
			}
		}
	}
	TestEqual(TEXT("AMD sample preserves live actors"), CountLiveActors(World), OriginalActors.Num());
	TArray<TSharedPtr<FJsonValue>> BadPositions = SourceSection->GetArrayField(TEXT("DebugExpandedPosBuffer"));
	if (!TestTrue(TEXT("AMD sample supplies expanded positions"), BadPositions.Num() > 0))
		return false;
	BadPositions[0] = MakeShared<FJsonValueNumber>(BadPositions[0]->AsNumber() + 1.0);
	SourceSection->SetArrayField(TEXT("DebugExpandedPosBuffer"), BadPositions);
	FString BadContents;
	FJsonSerializer::Serialize(Document.ToSharedRef(), TJsonWriterFactory<>::Create(&BadContents));
	const FString BadFilename = FPaths::CreateTempFilename(*FPaths::ProjectSavedDir(), TEXT("FEMBadExpanded-"), TEXT(".fem"));
	ON_SCOPE_EXIT { IFileManager::Get().Delete(*BadFilename); };
	if (!TestTrue(TEXT("Write malformed legacy fixture"), FFileHelper::SaveStringToFile(BadContents, *BadFilename)))
		return false;
	UPackage* BadPackage = CreatePackage(*(Prefix + TEXT("_Invalid")));
	FFEMCompatibilityFeedback BadFeedback;
	bCanceled = false;
	TestNull(TEXT("Incorrect expanded positions are rejected"), Factory->FactoryCreateFile(AFEMActor::StaticClass(), BadPackage, TEXT("BadExpanded"), RF_Transient, BadFilename, nullptr, &BadFeedback, bCanceled));
	TestTrue(TEXT("Incorrect expanded positions cancel import"), bCanceled);
	TestEqual(TEXT("Incorrect expanded positions report one error"), BadFeedback.NumErrors, 1);
	TArray<UObject*> PartialObjects;
	GetObjectsWithOuter(BadPackage, PartialObjects, true);
	TestEqual(TEXT("Incorrect expanded positions create no partial objects"), PartialObjects.Num(), 0);
	TestFalse(TEXT("Incorrect expanded positions keep package clean"), BadPackage->IsDirty());
	TestEqual(TEXT("Incorrect expanded positions preserve live actors"), CountLiveActors(World), OriginalActors.Num());
	return true;
}

#endif
