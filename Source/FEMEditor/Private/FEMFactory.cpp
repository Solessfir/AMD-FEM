//---------------------------------------------------------------------------------------
//
// Copyright (c) 2019 Advanced Micro Devices, Inc. All rights reserved.
//
//---------------------------------------------------------------------------------------

#include "FEMFactory.h"
#include "FEMFileImportFactory.h"
#include "FEMFXMeshComponent.h"
#include "FEMActor.h"
#include "FEMMesh.h"
#include "AssetRegistryModule.h"
#include "PackageTools.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "AssetSelection.h"
#include "FEMActorFactory.h"
#include "FEMTetMeshParametersFactory.h"
#include "Factories/FbxFactory.h"
#include "Misc/FeedbackContext.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "AssetTypeCategories.h"
#include "Serialization/JsonSerializer.h"
#include "Engine/World.h"
#include "AMD_FEMFX.h"
#include "FEMFXVectormath.h"

UFEMFactory::UFEMFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SupportedClass = AFEMActor::StaticClass();

	Formats.Add(FString(TEXT("fem;")));

	bCreateNew = false;
	bEditorImport = true;
	bEditAfterNew = true;

}

uint32 UFEMFactory::GetMenuCategories() const
{
	return EAssetTypeCategories::Physics;
}

bool UFEMFactory::ShouldShowInNewMenu() const
{
	return false;
}

bool UFEMFactory::FactoryCanImport(const FString& Filename)
{
	const FString FileExtension = FPaths::GetExtension(Filename);
	return (FileExtension.ToUpper() == FString("FEM"));
}

UObject* UFEMFactory::FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn, FName CallingContext)
{
	return NewObject<AFEMActor>(InParent, InClass, InName, Flags);
}

UObject* UFEMFactory::FactoryCreateFile(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, const FString& Filename, const TCHAR* Parms, FFeedbackContext* Warn, bool& bOutOperationCanceled)
{
	bOutOperationCanceled = false;
	auto FailImport = [&](const TCHAR* Reason) -> UObject*
	{
		if (Warn != nullptr)
		{
			Warn->Logf(ELogVerbosity::Error, TEXT("Failed to load FEM file '%s': %s"), *Filename, Reason);
		}
		bOutOperationCanceled = true;
		return nullptr;
	};

	FString JsonString;
	if (!FFileHelper::LoadFileToString(JsonString, *Filename))
	{
		return FailImport(TEXT("could not read the file"));
	}

	TSharedPtr<FJsonObject> JsonObject;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
	if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
	{
		return FailImport(TEXT("invalid JSON object"));
	}

	FString Version;
	if (!JsonObject->HasTypedField<EJson::String>(TEXT("Version")) || !JsonObject->TryGetStringField(TEXT("Version"), Version))
	{
		return FailImport(TEXT("missing or invalid Version"));
	}

	TArray<FString> VersionParts;
	Version.ParseIntoArray(VersionParts, TEXT("."), false);
	if (VersionParts.Num() < 2)
	{
		return FailImport(TEXT("invalid Version"));
	}
	for (const FString& Part : VersionParts)
	{
		if (Part.IsEmpty())
		{
			return FailImport(TEXT("invalid Version"));
		}
		for (const TCHAR Character : Part)
		{
			if (Character < TEXT('0') || Character > TEXT('9'))
			{
				return FailImport(TEXT("invalid Version"));
			}
		}
	}
	if (VersionParts[0] != TEXT("1") || VersionParts[1] != TEXT("0"))
	{
		return FailImport(TEXT("unsupported Version; expected 1.0"));
	}

	const auto HasFields = [](const TSharedPtr<FJsonObject>& Object, EJson Type, const TArray<FString>& Fields)
	{
		for (const FString& Field : Fields)
		{
			const TSharedPtr<FJsonValue> Value = Object->TryGetField(Field);
			if (!Value.IsValid() || Value->Type != Type
				|| (Type == EJson::Number && (!FMath::IsFinite(Value->AsNumber()) || FMath::Abs(Value->AsNumber()) > MAX_flt)))
			{
				return false;
			}
		}
		return true;
	};
	const auto HasNumberArray = [](const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, int32 MinimumSize)
	{
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (!Object->TryGetArrayField(Field, Values) || Values->Num() < MinimumSize)
		{
			return false;
		}
		for (const TSharedPtr<FJsonValue>& Value : *Values)
		{
			if (!Value.IsValid() || Value->Type != EJson::Number || !FMath::IsFinite(Value->AsNumber()) || FMath::Abs(Value->AsNumber()) > MAX_flt)
			{
				return false;
			}
		}
		return true;
	};
	const auto IsInteger = [](const TSharedPtr<FJsonValue>& Value, int32 Minimum, int32 Maximum)
	{
		if (!Value.IsValid() || Value->Type != EJson::Number)
			return false;
		const double Number = Value->AsNumber();
		return FMath::IsFinite(Number) && Number >= Minimum && Number <= Maximum && Number == static_cast<double>(static_cast<int32>(Number));
	};
	// AMD exports some Boolean flags as numeric 0 or 1.
	const auto HasBoolean = [&IsInteger](const TSharedPtr<FJsonObject>& Object, const TCHAR* Field)
	{
		const TSharedPtr<FJsonValue> Value = Object->TryGetField(Field);
		return Value.IsValid() && (Value->Type == EJson::Boolean || IsInteger(Value, 0, 1));
	};

	const TCHAR* RequiredArrays[] = { TEXT("FEMMeshComponents"), TEXT("RigidBodies"), TEXT("RBAngleConstraints"), TEXT("GlueConstraints"), TEXT("PlaneConstraints") };
	for (const TCHAR* Field : RequiredArrays)
	{
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (!JsonObject->TryGetArrayField(Field, Values))
		{
			return FailImport(TEXT("missing or invalid resource array"));
		}
		for (const TSharedPtr<FJsonValue>& Value : *Values)
		{
			if (!Value.IsValid() || Value->Type != EJson::Object)
			{
				return FailImport(TEXT("resource array entries must be objects"));
			}
			const TSharedPtr<FJsonObject> Object = Value->AsObject();
			if (FCString::Strcmp(Field, TEXT("FEMMeshComponents")) == 0)
			{
				if (!Object->HasTypedField<EJson::String>(TEXT("Name"))
					|| !Object->HasTypedField<EJson::Object>(TEXT("Node"))
					|| !Object->HasTypedField<EJson::Object>(TEXT("Ele"))
					|| !HasFields(Object, EJson::Number, { TEXT("NumFBXFiles"), TEXT("NumCornersPerShard"), TEXT("CollisionGroup"), TEXT("NumTags"), TEXT("NumMaterials") })
					|| !HasBoolean(Object, TEXT("IsFracturable"))
					|| !HasFields(Object, EJson::Array, { TEXT("Tags"), TEXT("Materials"), TEXT("FbxFiles"), TEXT("RenderMesh") }))
				{
					return FailImport(TEXT("missing or invalid mesh component fields"));
				}

				const TSharedPtr<FJsonObject> Node = Object->GetObjectField(TEXT("Node"));
				const TSharedPtr<FJsonObject> Ele = Object->GetObjectField(TEXT("Ele"));
				if (!HasBoolean(Node, TEXT("IsBoundaryMarker"))
					|| !IsInteger(Node->TryGetField(TEXT("NumAttributes")), 0, MAX_int32)
					|| !IsInteger(Node->TryGetField(TEXT("NumDimensions")), 3, 3)
					|| !IsInteger(Node->TryGetField(TEXT("NumPoints")), 4, MAX_int32 / sizeof(AMD::FmVector3))
					|| !HasNumberArray(Node, TEXT("Data"), 0)
					|| !HasBoolean(Ele, TEXT("IsRegionAttribute"))
					|| !IsInteger(Ele->TryGetField(TEXT("NumNodesPerTets")), 4, 4)
					|| !IsInteger(Ele->TryGetField(TEXT("NumTetrahedra")), 1, MAX_int32 / sizeof(AMD::FmTetVertIds))
					|| !Ele->HasTypedField<EJson::Array>(TEXT("Data")))
				{
					return FailImport(TEXT("invalid tetrahedral node or element fields"));
				}
				const int32 NumPoints = Node->GetIntegerField(TEXT("NumPoints"));
				const int32 NumTets = Ele->GetIntegerField(TEXT("NumTetrahedra"));
				const TArray<TSharedPtr<FJsonValue>>& Points = Node->GetArrayField(TEXT("Data"));
				const TArray<TSharedPtr<FJsonValue>>& Tets = Ele->GetArrayField(TEXT("Data"));
				if (static_cast<int64>(NumPoints) * 4 != Points.Num() || NumTets != Tets.Num())
				{
					return FailImport(TEXT("tetrahedral counts do not match their data arrays"));
				}
				for (const TSharedPtr<FJsonValue>& TetValue : Tets)
				{
					if (!TetValue.IsValid() || TetValue->Type != EJson::Object)
						return FailImport(TEXT("tetrahedral elements must be objects"));
					const TSharedPtr<FJsonObject> Tet = TetValue->AsObject();
					if (!IsInteger(Tet->TryGetField(TEXT("TetIndex")), 0, MAX_int32) || !HasNumberArray(Tet, TEXT("Indices"), 4))
						return FailImport(TEXT("invalid tetrahedral element fields"));
					const TArray<TSharedPtr<FJsonValue>>& Indices = Tet->GetArrayField(TEXT("Indices"));
					for (int32 Corner = 0; Corner < 4; ++Corner)
					{
						if (!IsInteger(Indices[Corner], 1, NumPoints))
							return FailImport(TEXT("tetrahedral vertex index is out of range"));
						for (int32 Previous = 0; Previous < Corner; ++Previous)
							if (Indices[Corner]->AsNumber() == Indices[Previous]->AsNumber())
								return FailImport(TEXT("tetrahedral elements require four different vertices"));
					}
				}

				if (!IsInteger(Object->TryGetField(TEXT("NumFBXFiles")), 0, MAX_int32)
					|| !IsInteger(Object->TryGetField(TEXT("NumTags")), 0, MAX_int32)
					|| !IsInteger(Object->TryGetField(TEXT("NumMaterials")), 0, MAX_int32)
					|| !IsInteger(Object->TryGetField(TEXT("NumCornersPerShard")), 0, MAX_int32)
					|| !IsInteger(Object->TryGetField(TEXT("CollisionGroup")), 0, 31)
					|| Object->GetIntegerField(TEXT("NumFBXFiles")) != Object->GetArrayField(TEXT("FbxFiles")).Num()
					|| Object->GetIntegerField(TEXT("NumTags")) != Object->GetArrayField(TEXT("Tags")).Num()
					|| Object->GetIntegerField(TEXT("NumMaterials")) != Object->GetArrayField(TEXT("Materials")).Num())
				{
					return FailImport(TEXT("invalid component counts or collision group"));
				}
				for (const TSharedPtr<FJsonValue>& FbxFile : Object->GetArrayField(TEXT("FbxFiles")))
				{
					if (!FbxFile.IsValid() || FbxFile->Type != EJson::String || FbxFile->AsString().IsEmpty())
						return FailImport(TEXT("FBX references must be nonempty strings"));
				}
				for (const TCHAR* AssignmentField : { TEXT("Tags"), TEXT("Materials") })
				{
					for (const TSharedPtr<FJsonValue>& AssignmentValue : Object->GetArrayField(AssignmentField))
					{
						if (!AssignmentValue.IsValid() || AssignmentValue->Type != EJson::Object)
							return FailImport(TEXT("tag and material assignments must be objects"));
						const TSharedPtr<FJsonObject> Assignment = AssignmentValue->AsObject();
						const bool IsMaterial = FCString::Strcmp(AssignmentField, TEXT("Materials")) == 0;
						if (!Assignment->HasTypedField<EJson::String>(IsMaterial ? TEXT("MaterialName") : TEXT("Tag"))
							|| !HasNumberArray(Assignment, TEXT("TetIds"), 0)
							|| (IsMaterial && !HasNumberArray(Assignment, TEXT("NoFractureFaces"), 0)))
							return FailImport(TEXT("invalid tag or material assignment fields"));
						const TArray<TSharedPtr<FJsonValue>>& TetIds = Assignment->GetArrayField(TEXT("TetIds"));
						for (const TSharedPtr<FJsonValue>& TetId : TetIds)
							if (!IsInteger(TetId, 0, NumTets - 1))
								return FailImport(TEXT("assigned tetrahedral index is out of range"));
						if (IsMaterial)
						{
							const TArray<TSharedPtr<FJsonValue>>& Faces = Assignment->GetArrayField(TEXT("NoFractureFaces"));
							if (Faces.Num() != 0 && Faces.Num() != TetIds.Num())
								return FailImport(TEXT("material fracture face count does not match its tetrahedral assignments"));
							for (const TSharedPtr<FJsonValue>& Face : Faces)
								if (!IsInteger(Face, 0, 15))
									return FailImport(TEXT("invalid material fracture face flags"));
						}
					}
				}

				for (const TSharedPtr<FJsonValue>& SectionValue : Object->GetArrayField(TEXT("RenderMesh")))
				{
					if (!SectionValue.IsValid() || SectionValue->Type != EJson::Object)
						return FailImport(TEXT("render mesh sections must be objects"));
					const TSharedPtr<FJsonObject> Section = SectionValue->AsObject();
					const TCHAR* BufferFields[] = { TEXT("AssignedTetFaceBuffer"), TEXT("BarycentricCoordsBuffer"), TEXT("BarycentricPosIds"), TEXT("TetAssignmentBuffer"), TEXT("ColorBuffer"), TEXT("NormalBuffer"), TEXT("PositionBuffer"), TEXT("TangentBuffer"), TEXT("UVsBuffer"), TEXT("ShardIds"), TEXT("Triangles"), TEXT("Centroids") };
					for (const TCHAR* BufferField : BufferFields)
						if (!HasNumberArray(Section, BufferField, 0))
							return FailImport(TEXT("render mesh buffers must contain finite numbers"));
					{
						// AMD viewer files can store positions per point and attributes per triangle corner.
						const TArray<TSharedPtr<FJsonValue>>& PointPositions = Section->GetArrayField(TEXT("PositionBuffer"));
						const TArray<TSharedPtr<FJsonValue>>& CornerIds = Section->GetArrayField(TEXT("ShardIds"));
						const TArray<TSharedPtr<FJsonValue>>& PointTriangles = Section->GetArrayField(TEXT("Triangles"));
						const TArray<TSharedPtr<FJsonValue>>* ExpandedPositions = nullptr;
						const int32 NumRenderPoints = PointPositions.Num() / 3;
						bool bPointIndexed = PointPositions.Num() % 3 == 0 && NumRenderPoints > 0 && CornerIds.Num() > NumRenderPoints
							&& PointTriangles.Num() == CornerIds.Num() && CornerIds.Num() % 3 == 0
							&& IsInteger(Section->TryGetField(TEXT("NumberOfShards")), NumRenderPoints, NumRenderPoints)
							&& Section->GetArrayField(TEXT("Centroids")).Num() == 0
							&& Section->GetArrayField(TEXT("AssignedTetFaceBuffer")).Num() == 0
							&& Section->TryGetArrayField(TEXT("DebugExpandedPosBuffer"), ExpandedPositions)
							&& static_cast<int64>(CornerIds.Num()) * 3 == ExpandedPositions->Num()
							&& HasNumberArray(Section, TEXT("DebugExpandedPosBuffer"), 0);
						for (int32 Corner = 0; bPointIndexed && Corner < CornerIds.Num(); ++Corner)
						{
							bPointIndexed = IsInteger(CornerIds[Corner], 0, NumRenderPoints - 1)
								&& CornerIds[Corner]->AsNumber() == PointTriangles[Corner]->AsNumber();
							if (!bPointIndexed)
								break;
							const int32 Point = static_cast<int32>(CornerIds[Corner]->AsNumber());
							for (int32 Axis = 0; Axis < 3; ++Axis)
								bPointIndexed &= (*ExpandedPositions)[Corner * 3 + Axis]->AsNumber() == PointPositions[Point * 3 + Axis]->AsNumber();
						}
						if (bPointIndexed)
						{
							TArray<TSharedPtr<FJsonValue>> CornerTriangles;
							CornerTriangles.Reserve(CornerIds.Num());
							for (int32 Corner = 0; Corner < CornerIds.Num(); ++Corner)
								CornerTriangles.Add(MakeShared<FJsonValueNumber>(Corner));
							Section->SetArrayField(TEXT("PositionBuffer"), *ExpandedPositions);
							Section->SetArrayField(TEXT("Triangles"), CornerTriangles);
						}
					}
					const TArray<TSharedPtr<FJsonValue>>& Positions = Section->GetArrayField(TEXT("PositionBuffer"));
					const TArray<TSharedPtr<FJsonValue>>& ShardIds = Section->GetArrayField(TEXT("ShardIds"));
					const TArray<TSharedPtr<FJsonValue>>& BarycentricIds = Section->GetArrayField(TEXT("BarycentricPosIds"));
					const TArray<TSharedPtr<FJsonValue>>& TetAssignments = Section->GetArrayField(TEXT("TetAssignmentBuffer"));
					const TArray<TSharedPtr<FJsonValue>>& Centroids = Section->GetArrayField(TEXT("Centroids"));
					const TArray<TSharedPtr<FJsonValue>>& Faces = Section->GetArrayField(TEXT("AssignedTetFaceBuffer"));
					const TArray<TSharedPtr<FJsonValue>>& Triangles = Section->GetArrayField(TEXT("Triangles"));
					const int32 NumVertices = Positions.Num() / 3;
					if (Positions.Num() % 3 != 0 || NumVertices < 1 || ShardIds.Num() != NumVertices
					|| !IsInteger(Section->TryGetField(TEXT("NumberOfShards")), 1, MAX_int32)
						|| Triangles.Num() % 3 != 0 || (Faces.Num() != 0 && Faces.Num() < NumVertices))
						return FailImport(TEXT("invalid render mesh vertex or triangle counts"));
					const int32 NumShards = Section->GetIntegerField(TEXT("NumberOfShards"));
					if ((Centroids.Num() > 0 && (NumShards > NumVertices || static_cast<int64>(NumShards) * 3 > Centroids.Num()))
						|| (Centroids.Num() == 0 && (BarycentricIds.Num() < NumShards || static_cast<int64>(TetAssignments.Num()) * 4 > Section->GetArrayField(TEXT("BarycentricCoordsBuffer")).Num())))
						return FailImport(TEXT("render mesh barycentric or centroid data is too short"));
					for (const TSharedPtr<FJsonValue>& ShardId : ShardIds)
						if (!IsInteger(ShardId, 0, NumShards - 1))
							return FailImport(TEXT("render mesh shard index is out of range"));
					for (const TSharedPtr<FJsonValue>& TetId : TetAssignments)
						if (!IsInteger(TetId, 0, NumTets - 1))
							return FailImport(TEXT("render mesh tetrahedral assignment is out of range"));
					if (Centroids.Num() == 0)
						for (const TSharedPtr<FJsonValue>& BarycentricId : BarycentricIds)
							if (!IsInteger(BarycentricId, 0, TetAssignments.Num() - 1))
								return FailImport(TEXT("render mesh barycentric index is out of range"));
					for (const TSharedPtr<FJsonValue>& Face : Faces)
						if (!IsInteger(Face, -1, 3))
							return FailImport(TEXT("render mesh tetrahedral face index is out of range"));
					for (const TSharedPtr<FJsonValue>& Triangle : Triangles)
					{
						if (!IsInteger(Triangle, 0, NumVertices - 1))
							return FailImport(TEXT("render mesh triangle index is out of range"));
						if (Faces.Num() == 0)
							continue;
						const int32 Face = static_cast<int32>(Faces[static_cast<int32>(Triangle->AsNumber())]->AsNumber());
						if (Face < 0)
							continue;
						if (!ShardIds.IsValidIndex(Face))
							return FailImport(TEXT("render mesh face has no shard assignment"));
						const int32 Shard = static_cast<int32>(ShardIds[Face]->AsNumber());
						if (!BarycentricIds.IsValidIndex(Shard) || !IsInteger(BarycentricIds[Shard], 0, TetAssignments.Num() - 1))
							return FailImport(TEXT("render mesh face has no tetrahedral assignment"));
					}
				}
			}
			else if (FCString::Strcmp(Field, TEXT("RigidBodies")) == 0)
			{
				if (!HasFields(Object, EJson::Number, { TEXT("Mass") })
					|| !HasNumberArray(Object, TEXT("Position"), 3)
					|| !HasNumberArray(Object, TEXT("Dimensions"), 3)
					|| !HasNumberArray(Object, TEXT("Rotation"), 4)
					|| !HasNumberArray(Object, TEXT("BodyInertiaTensor"), 9))
				{
					return FailImport(TEXT("missing or invalid rigid body fields"));
				}
			}
			else if (!Object->HasTypedField<EJson::String>(TEXT("Name"))
				|| !Object->HasTypedField<EJson::Number>(TEXT("BodyA"))
				|| !Object->HasTypedField<EJson::Number>(TEXT("BodyB")))
			{
				return FailImport(TEXT("constraints require Name, BodyA and BodyB"));
			}
			else if (FCString::Strcmp(Field, TEXT("RBAngleConstraints")) == 0)
			{
				if (!HasNumberArray(Object, TEXT("AxisBodySpaceA"), 3) || !HasNumberArray(Object, TEXT("AxisBodySpaceB"), 3))
				{
					return FailImport(TEXT("missing or invalid constraint axes"));
				}
				const int32 NumBodies = JsonObject->GetArrayField(TEXT("RigidBodies")).Num();
				if (!IsInteger(Object->TryGetField(TEXT("BodyA")), 0, NumBodies - 1) || !IsInteger(Object->TryGetField(TEXT("BodyB")), 0, NumBodies - 1))
					return FailImport(TEXT("angle constraint rigid body index is out of range"));
			}
			else if (!HasFields(Object, EJson::Boolean, { TEXT("IsRigidBodyA"), TEXT("IsRigidBodyB") })
				|| !HasFields(Object, EJson::Number, { TEXT("TetIdA"), TEXT("TetIdB") })
				|| !HasNumberArray(Object, TEXT("PosBodySpaceA"), 4) || !HasNumberArray(Object, TEXT("PosBodySpaceB"), 4))
			{
				return FailImport(TEXT("missing or invalid constraint attachment fields"));
			}
			else if (FCString::Strcmp(Field, TEXT("GlueConstraints")) == 0)
			{
				if (!HasFields(Object, EJson::Number, { TEXT("BreakThreshold"), TEXT("MinGlueConstraints") }))
				{
					return FailImport(TEXT("missing or invalid glue constraint fields"));
				}
				if (!IsInteger(Object->TryGetField(TEXT("MinGlueConstraints")), 0, MAX_int32))
					return FailImport(TEXT("invalid minimum glue constraint count"));
			}
			else if (!Object->HasTypedField<EJson::Number>(TEXT("NumberOfPlanes")) || !Object->HasTypedField<EJson::Array>(TEXT("Planes")))
			{
				return FailImport(TEXT("missing or invalid plane constraint fields"));
			}
			else
			{
				const TArray<TSharedPtr<FJsonValue>>& Planes = Object->GetArrayField(TEXT("Planes"));
				if (!IsInteger(Object->TryGetField(TEXT("NumberOfPlanes")), 1, 3) || Object->GetIntegerField(TEXT("NumberOfPlanes")) != Planes.Num())
					return FailImport(TEXT("plane constraints require one to three matching plane definitions"));
				for (const TSharedPtr<FJsonValue>& PlaneValue : Planes)
				{
					if (!PlaneValue.IsValid() || PlaneValue->Type != EJson::Object)
						return FailImport(TEXT("constraint planes must be objects"));
					const TSharedPtr<FJsonObject> Plane = PlaneValue->AsObject();
					if (!HasFields(Plane, EJson::Number, { TEXT("Bias") }) || !HasBoolean(Plane, TEXT("NonNegative")) || !HasNumberArray(Plane, TEXT("PlaneNormal"), 3))
						return FailImport(TEXT("invalid plane definition fields"));
				}
			}
			if (FCString::Strcmp(Field, TEXT("GlueConstraints")) == 0 || FCString::Strcmp(Field, TEXT("PlaneConstraints")) == 0)
			{
				for (const TCHAR* Side : { TEXT("A"), TEXT("B") })
				{
					const FString BodyField = FString(TEXT("Body")) + Side;
					const FString TetField = FString(TEXT("TetId")) + Side;
					const bool IsRigidBody = Object->GetBoolField(FString(TEXT("IsRigidBody")) + Side);
					const TArray<TSharedPtr<FJsonValue>>& Bodies = JsonObject->GetArrayField(IsRigidBody ? TEXT("RigidBodies") : TEXT("FEMMeshComponents"));
					if (!IsInteger(Object->TryGetField(BodyField), 0, Bodies.Num() - 1))
						return FailImport(TEXT("constraint body index is out of range"));
					const int32 NumTets = IsRigidBody ? MAX_int32 : Bodies[Object->GetIntegerField(BodyField)]->AsObject()->GetObjectField(TEXT("Ele"))->GetIntegerField(TEXT("NumTetrahedra"));
					if (!IsInteger(Object->TryGetField(TetField), IsRigidBody ? -1 : 0, NumTets - 1))
						return FailImport(TEXT("constraint tetrahedral index is out of range"));
				}
			}
		}
	}

	TUniquePtr<FEMFileImportFactory> fileFactory = FEMFileImportFactory::GetFactory(Version);
	if (fileFactory == nullptr)
	{
		return FailImport(TEXT("unsupported Version"));
	}

	AActor* RootActorContainer = nullptr;
	UBlueprint* Blueprint = nullptr;
	USceneComponent* ActorRootComponent = nullptr;
	EComponentMobility::Type MobilityType = EComponentMobility::Movable;

	UActorFactory* ActorFactory = NewObject<UFEMActorFactory>(InParent, "FEMActorFactory");// GEditor->FindActorFactoryByClass(UActorFactoryEmptyActor::StaticClass());
	FAssetData FEMActorAssetData = FAssetData(ActorFactory->GetDefaultActorClass(FAssetData()));
	UObject* FEMActorAsset = FEMActorAssetData.GetAsset();

	RootActorContainer = FActorFactoryAssetProxy::AddActorForAsset(FEMActorAsset, false);
	check(RootActorContainer != nullptr);
	ActorRootComponent = NewObject<USceneComponent>(RootActorContainer, USceneComponent::GetDefaultSceneRootVariableName());
	check(ActorRootComponent != nullptr);



	FName resourceName = MakeUniqueObjectName(RootActorContainer, UFEMResource::StaticClass(), InName);
	UFEMResource* Resource = NewObject<UFEMResource>(InParent, UFEMResource::StaticClass(), resourceName);
	Resource->Version = Version;

	FEMFileImportInputs fileFactoryInputs = FEMFileImportInputs();
	fileFactoryInputs.JsonObject = JsonObject;
	fileFactoryInputs.Resource = Resource;
	fileFactory->ImportFEMFile(&fileFactoryInputs);

	ActorRootComponent->Mobility = MobilityType;
	ActorRootComponent->bVisualizeComponent = false;
	RootActorContainer->SetRootComponent(ActorRootComponent);
	RootActorContainer->AddInstanceComponent(ActorRootComponent);
	ActorRootComponent->RegisterComponent();
	RootActorContainer->SetActorLabel("FEM");
	RootActorContainer->SetFlags(RF_Transactional);
	ActorRootComponent->SetFlags(RF_Transactional);

	Cast<AFEMActor>(RootActorContainer)->FEMResource = Resource->ActorResource;
	Cast<AFEMActor>(RootActorContainer)->ComponentResources = Resource->ComponentResources;

	for (int i = 0; i < Resource->ComponentResources.Num(); ++i)
	{
		UFEMFXMeshComponent* meshComponent = NewObject<UFEMFXMeshComponent>(RootActorContainer, *Resource->ComponentResources[i].Name);
		//meshComponent->resource = Resource->ComponentResources[i];
		meshComponent->Name = Resource->ComponentResources[i].Name;
		meshComponent->FractureEnabled = Resource->ComponentResources[i].IsFracturable;

		for (auto it = Resource->ComponentResources[i].Materials.CreateIterator(); it; it++)
		{
			if (it->TetIds.Num() > 0)
				meshComponent->MeshParameters.Add(it->Name);

			UPackage* package = UPackageTools::LoadPackage(*it->Name);

			UFEMFXTetMeshParameters* material = Cast<UFEMFXTetMeshParameters>(StaticLoadObject(UFEMFXTetMeshParameters::StaticClass(), package, *it->Name));
			if (!material)
			{
				UFEMTetMeshParametersFactory* factory = NewObject<UFEMTetMeshParametersFactory>();
				const FString PackageName = "/Game/FEM/FEMMaterials/" + it->Name;
				package = CreatePackage(*PackageName);
				EObjectFlags objectFlags = RF_Public | RF_Standalone;

				material = Cast<UFEMFXTetMeshParameters>(factory->FactoryCreateNew(UFEMFXTetMeshParameters::StaticClass(), package, *it->Name, objectFlags, nullptr, Warn, NAME_None));
				FAssetRegistryModule::AssetCreated(material);
				material->MarkPackageDirty();
			}
			if (material)
			{
				if (it->TetIds.Num() == 0)
				{
					meshComponent->MeshParameters["Default"] = material;
				}
				else
				{
					meshComponent->MeshParameters[it->Name] = material;
				}
			}
		}

		for (auto it = Resource->ComponentResources[i].FBXFiles.CreateIterator(); it; it++)
		{
			UPackage* package = UPackageTools::LoadPackage(*it);

			UStaticMesh* Mesh = Cast<UStaticMesh>(StaticLoadObject(UStaticMesh::StaticClass(), package, **it));
			if (!Mesh)
			{
				UFbxFactory* factory = NewObject<UFbxFactory>();

				FName name = MakeUniqueObjectName(InParent, UStaticMesh::StaticClass(), InName);
				Mesh = Cast<UStaticMesh>(factory->FactoryCreateFile(UStaticMesh::StaticClass(), InParent, name, Flags, *it, Parms, Warn, bOutOperationCanceled));
				if (!Mesh)
				{
					RootActorContainer->GetWorld()->EditorDestroyActor(RootActorContainer, true);
					return FailImport(*FString::Printf(TEXT("could not import referenced FBX '%s'; check its path and contents"), **it));
				}
				FAssetRegistryModule::AssetCreated(Mesh);
				Mesh->MarkPackageDirty();
			}

			if (Mesh)
			{
				meshComponent->staticMeshes.Add(Mesh);
			}
		}


		/** Create the UFEMMesh f */
		// Only need to do this here because I need a valid TetMeshBuffer as well as BvHierarchy.
		// Once those things can come from the fem file I won't need to do this.
		if (meshComponent->staticMeshes.Num() > 0 || Resource->ComponentResources[i].meshSections.Num() > 0)
		{


			FString name = MakeUniqueObjectName(InParent, UFEMMesh::StaticClass(), InName).ToString();
			FString packageName = InParent->GetPathName();

			UPackage* package = CreatePackage(*(packageName + name));
			EObjectFlags objectFlags = RF_Public | RF_Standalone;

			UFEMMesh* mesh = Cast<UFEMMesh>(NewObject<UFEMMesh>(package, *name, objectFlags));
			FAssetRegistryModule::AssetCreated(mesh);
			mesh->MarkPackageDirty();
			if (IsValid(mesh))
			{
				mesh->ComponentResources = Resource->ComponentResources[i];
				mesh->NumberOfCornersPerShard = Resource->ComponentResources[i].NumberOfCornersPerShard;
				meshComponent->FEMMesh = mesh;
				meshComponent->LoadResource();
				meshComponent->LoadSimObject();

				if (meshComponent->staticMeshes.Num() > 0)
				{
					for (int j = 0; j < meshComponent->staticMeshes.Num(); ++j)
					{
						UStaticMesh* staticMesh = meshComponent->staticMeshes[j];

						FFEMFXMeshSection* meshSection = mesh->CreateMeshSection(staticMesh, meshComponent->GetTetMeshBuffer(), meshComponent->GetBvHierarchy(), j);
						meshSection->MaterialIndex = j;

						UMaterialInterface* material = staticMesh->GetMaterial(0);
						meshComponent->SetMaterial(j, material);

						mesh->GetTetMesh()->UpdateTetMesh(meshComponent->GetTetMeshBuffer());
					}
				}
				else
				{
					for (int j = 0; j < Resource->ComponentResources[i].meshSections.Num(); j++)
					{
						FFEMFXMeshSection* meshSection = mesh->CreateMeshSectionFromFEMFile(meshComponent->GetTetMeshBuffer(), meshComponent->GetBvHierarchy(), j);
						meshSection->MaterialIndex = j;

						mesh->GetTetMesh()->UpdateTetMesh(meshComponent->GetTetMeshBuffer());
					}
				}
			}
			meshComponent->UpdateSceneProxy();

			meshComponent->CleanUpAfterImport();
		}

		RootActorContainer->AddInstanceComponent(meshComponent);
		meshComponent->RegisterComponent();
		meshComponent->AttachToComponent(ActorRootComponent, FAttachmentTransformRules::KeepWorldTransform);
		meshComponent->PostEditChange();

		Cast<AFEMActor>(RootActorContainer)->MeshComponents.Add(meshComponent);
	}

	FKismetEditorUtilities::FCreateBlueprintFromActorParams Params;
	Params.bReplaceActor = false;
	Params.bKeepMobility = true;
	Params.bOpenBlueprint = bEditAfterNew && !FApp::IsUnattended();
	Blueprint = FKismetEditorUtilities::CreateBlueprintFromActor(InParent->GetName(), RootActorContainer, Params);

	if (Blueprint)
	{
		FAssetRegistryModule::AssetCreated(Blueprint);
		Blueprint->MarkPackageDirty();
	}

	GWorld->EditorDestroyActor(RootActorContainer, true);

	return Blueprint;

}
