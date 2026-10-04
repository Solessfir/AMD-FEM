#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "FEMFXVectorMath.h"
#include "FEMMesh.h"
#include "FEMMeshFactory.h"
#include "HAL/FileManager.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "UObject/UObjectGlobals.h"
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFEMOptionsPersistenceTest, "FEM.Creation.OptionsPersistence", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFEMOptionsPersistenceTest::RunTest(const FString& Parameters)
{
	const UFEMMeshImportData* Defaults = GetDefault<UFEMMeshImportData>();
	UFEMMeshImportData* Source = NewObject<UFEMMeshImportData>();
	Source->bProceduralGenerate = false;
	Source->Randomize = true;
	Source->NumCubesX = 2;
	Source->NumCubesY = 3;
	Source->NumCubesZ = 4;
	Source->CubeX = 5.0f;
	Source->CubeY = 6.0f;
	Source->CubeZ = 7.0f;
	Source->Scale = 2.0f;
	Source->IsWoodPanel = true;

	const FString Filename = FPaths::CreateTempFilename(*FPaths::ProjectSavedDir(), TEXT("FEMOptions"), TEXT(".ini"));
	ON_SCOPE_EXIT
	{
		GConfig->UnloadFile(Filename);
		IFileManager::Get().Delete(*Filename);
	};
	Source->SaveConfig(CPF_Config, *Filename, GConfig, false);
	GConfig->UnloadFile(Filename);

	UFEMMeshImportData* Loaded = NewObject<UFEMMeshImportData>();
	Loaded->LoadConfig(nullptr, *Filename);
	TestEqual(TEXT("Persist procedural generation"), Loaded->bProceduralGenerate, Source->bProceduralGenerate);
	TestEqual(TEXT("Persist randomization"), Loaded->Randomize, Source->Randomize);
	TestEqual(TEXT("Persist cube count X"), Loaded->NumCubesX, Source->NumCubesX);
	TestEqual(TEXT("Persist cube count Y"), Loaded->NumCubesY, Source->NumCubesY);
	TestEqual(TEXT("Persist cube count Z"), Loaded->NumCubesZ, Source->NumCubesZ);
	TestEqual(TEXT("Persist cube size X"), Loaded->CubeX, Source->CubeX);
	TestEqual(TEXT("Persist cube size Y"), Loaded->CubeY, Source->CubeY);
	TestEqual(TEXT("Persist cube size Z"), Loaded->CubeZ, Source->CubeZ);
	TestEqual(TEXT("Persist scale"), Loaded->Scale, Source->Scale);
	TestEqual(TEXT("Persist wood panel option"), Loaded->IsWoodPanel, Source->IsWoodPanel);

	Loaded->CopyFrom(Defaults);
	TestEqual(TEXT("Reset procedural generation"), Loaded->bProceduralGenerate, Defaults->bProceduralGenerate);
	TestEqual(TEXT("Reset randomization"), Loaded->Randomize, Defaults->Randomize);
	TestEqual(TEXT("Reset cube count X"), Loaded->NumCubesX, Defaults->NumCubesX);
	TestEqual(TEXT("Reset cube count Y"), Loaded->NumCubesY, Defaults->NumCubesY);
	TestEqual(TEXT("Reset cube count Z"), Loaded->NumCubesZ, Defaults->NumCubesZ);
	TestEqual(TEXT("Reset cube size X"), Loaded->CubeX, Defaults->CubeX);
	TestEqual(TEXT("Reset cube size Y"), Loaded->CubeY, Defaults->CubeY);
	TestEqual(TEXT("Reset cube size Z"), Loaded->CubeZ, Defaults->CubeZ);
	TestEqual(TEXT("Reset scale"), Loaded->Scale, Defaults->Scale);
	TestEqual(TEXT("Reset wood panel option"), Loaded->IsWoodPanel, Defaults->IsWoodPanel);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFEMCreationTest, "FEM.Creation.ProceduralMesh", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFEMCreationTest::RunTest(const FString& Parameters)
{
	UFEMMeshFactory* Factory = NewObject<UFEMMeshFactory>();
	UFEMMeshImportData* Data = NewObject<UFEMMeshImportData>();
	Data->bProceduralGenerate = true;
	Data->Randomize = false;
	Data->NumCubesX = Data->NumCubesY = Data->NumCubesZ = 1;
	Data->CubeX = Data->CubeY = Data->CubeZ = 1.0f;
	Data->Scale = 1.0f;
	Data->IsWoodPanel = false;

	UFEMMesh* Mesh = Factory->CreateMesh(UFEMMesh::StaticClass(), Factory, NAME_None, RF_Transient, Data);
	if (!TestNotNull(TEXT("Create valid procedural mesh"), Mesh))
	{
		return false;
	}
	TestEqual(TEXT("One cube has eight vertices"), Mesh->GetComponentResource().NumVerts, 8);
	TestEqual(TEXT("One cube has one render section"), Mesh->GetImportedResource()->GetNumSections(), 1);

	const auto CheckRejected = [this, Factory, Data](const TCHAR* Name)
	{
		TestNull(Name, Factory->CreateMesh(UFEMMesh::StaticClass(), Factory, FName(Name), RF_Transient, Data));
		TestNull(TEXT("Rejected options leave no partial asset"), FindObject<UFEMMesh>(Factory, Name));
	};
	Data->bProceduralGenerate = false;
	CheckRejected(TEXT("DisabledProceduralGeneration"));
	Data->bProceduralGenerate = true;
	Data->NumCubesX = 0;
	CheckRejected(TEXT("ZeroCubeCount"));
	Data->NumCubesX = -1;
	CheckRejected(TEXT("NegativeCubeCount"));
	Data->NumCubesX = std::numeric_limits<int32>::max();
	CheckRejected(TEXT("OverflowCubeCount"));
	Data->NumCubesX = Data->NumCubesY = Data->NumCubesZ = 16;
	CheckRejected(TEXT("ExceedsTetVertexCapacity"));
	Data->NumCubesX = Data->NumCubesY = Data->NumCubesZ = 1;
	Data->CubeX = 0.0f;
	CheckRejected(TEXT("ZeroCubeSize"));
	Data->CubeX = -1.0f;
	CheckRejected(TEXT("NegativeCubeSize"));
	Data->CubeX = std::numeric_limits<float>::quiet_NaN();
	CheckRejected(TEXT("NaNCubeSize"));
	Data->CubeX = std::numeric_limits<float>::infinity();
	CheckRejected(TEXT("InfiniteCubeSize"));
	Data->CubeX = 1.0f;
	Data->Scale = 0.0f;
	CheckRejected(TEXT("ZeroScale"));
	Data->Scale = -1.0f;
	CheckRejected(TEXT("NegativeScale"));
	Data->Scale = std::numeric_limits<float>::quiet_NaN();
	CheckRejected(TEXT("NaNScale"));
	Data->Scale = std::numeric_limits<float>::infinity();
	CheckRejected(TEXT("InfiniteScale"));

	Data->Scale = 1.0f;
	Data->NumCubesX = Data->NumCubesY = Data->NumCubesZ = 2;
	UFEMMesh* Regular = Factory->CreateMesh(UFEMMesh::StaticClass(), Factory, NAME_None, RF_Transient, Data);
	Data->Randomize = true;
	UFEMMesh* Randomized = Factory->CreateMesh(UFEMMesh::StaticClass(), Factory, NAME_None, RF_Transient, Data);
	if (!TestNotNull(TEXT("Create regular grid"), Regular) || !TestNotNull(TEXT("Create randomized grid"), Randomized))
	{
		return false;
	}
	const FComponentResources RegularResource = Regular->GetComponentResource();
	const FComponentResources RandomResource = Randomized->GetComponentResource();
	bool bDifferentPositions = false;
	for (int32 Index = 0; Index < RegularResource.NumVerts; ++Index)
	{
		AMD::FmVector3 RegularPosition;
		AMD::FmVector3 RandomPosition;
		FMemory::Memcpy(&RegularPosition, RegularResource.restPositions.GetData() + Index * sizeof(RegularPosition), sizeof(RegularPosition));
		FMemory::Memcpy(&RandomPosition, RandomResource.restPositions.GetData() + Index * sizeof(RandomPosition), sizeof(RandomPosition));
		bDifferentPositions |= RegularPosition.x != RandomPosition.x || RegularPosition.y != RandomPosition.y || RegularPosition.z != RandomPosition.z;
	}
	TestTrue(TEXT("Factory forwards randomization to mesh generation"), bDifferentPositions);
	return true;
}

#endif
