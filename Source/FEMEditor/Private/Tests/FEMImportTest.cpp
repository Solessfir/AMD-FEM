#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Editor.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "FEMActor.h"
#include "FEMFactory.h"
#include "HAL/FileManager.h"
#include "Misc/FeedbackContext.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "UObject/Package.h"
#include "UObject/UObjectHash.h"

namespace
{
	class FFEMImportFeedback : public FFeedbackContext
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
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFEMImportFailureTest, "FEM.Import.InvalidFiles", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFEMImportFailureTest::RunTest(const FString& Parameters)
{
	UWorld* World = GEditor != nullptr ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("Editor world"), World))
	{
		return false;
	}

	const FString Filename = FPaths::CreateTempFilename(*FPaths::ProjectSavedDir(), TEXT("FEMImport-"), TEXT(".fem"));
	ON_SCOPE_EXIT
	{
		IFileManager::Get().Delete(*Filename);
	};

	const FString PackageName = FString(TEXT("/Temp/FEMImportTest_")) + FGuid::NewGuid().ToString(EGuidFormats::Digits);
	UPackage* Package = CreatePackage(*PackageName);
	UFEMFactory* Factory = NewObject<UFEMFactory>();
	const bool bWorldWasDirty = World->GetOutermost()->IsDirty();
	const int32 ActorCount = World->PersistentLevel->Actors.Num();

	struct FInvalidFile
	{
		const TCHAR* Name;
		const TCHAR* Contents;
	};
	const FInvalidFile InvalidFiles[] =
	{
		{ TEXT("Missing file"), nullptr },
		{ TEXT("Empty file"), TEXT("") },
		{ TEXT("Malformed JSON"), TEXT("{") },
		{ TEXT("Non-object JSON"), TEXT("[]") },
		{ TEXT("Missing version"), TEXT("{}") },
		{ TEXT("Numeric version"), TEXT("{\"Version\":1.0}") },
		{ TEXT("Empty version"), TEXT("{\"Version\":\"\"}") },
		{ TEXT("Incomplete version"), TEXT("{\"Version\":\"1\"}") },
		{ TEXT("Invalid version token"), TEXT("{\"Version\":\"1.x\"}") },
		{ TEXT("Missing version token"), TEXT("{\"Version\":\"1..0\"}") },
		{ TEXT("Unsupported major version"), TEXT("{\"Version\":\"2.0\"}") },
		{ TEXT("Unsupported minor version"), TEXT("{\"Version\":\"1.1\"}") },
		{ TEXT("Missing resource arrays"), TEXT("{\"Version\":\"1.0\"}") },
		{ TEXT("Invalid resource array"), TEXT("{\"Version\":\"1.0\",\"FEMMeshComponents\":{}}") },
		{ TEXT("Invalid resource entry"), TEXT("{\"Version\":\"1.0\",\"FEMMeshComponents\":[null]}") },
		{ TEXT("Missing component fields"), TEXT("{\"Version\":\"1.0\",\"FEMMeshComponents\":[{}]}") },
		{ TEXT("Missing rigid body fields"), TEXT("{\"Version\":\"1.0\",\"FEMMeshComponents\":[],\"RigidBodies\":[{}]}") },
		{ TEXT("Short rigid body vector"), TEXT("{\"Version\":\"1.0\",\"FEMMeshComponents\":[],\"RigidBodies\":[{\"Mass\":1,\"Position\":[0,0],\"Dimensions\":[1,1,1],\"Rotation\":[0,0,0,1],\"BodyInertiaTensor\":[]}]}") },
		{ TEXT("Missing constraint fields"), TEXT("{\"Version\":\"1.0\",\"FEMMeshComponents\":[],\"RigidBodies\":[],\"RBAngleConstraints\":[{}]}") }
	};

	const auto CheckRejectedFile = [&](const FString& Name, const TCHAR* ExpectedReason)
	{
		FFEMImportFeedback Feedback;
		bool bCanceled = false;
		UObject* Imported = Factory->FactoryCreateFile(AFEMActor::StaticClass(), Package, TEXT("InvalidFEM"), RF_Public | RF_Standalone, Filename, nullptr, &Feedback, bCanceled);
		TestNull(Name, Imported);
		TestTrue(Name + TEXT(" reports cancellation"), bCanceled);
		TestEqual(Name + TEXT(" reports one import error"), Feedback.NumErrors, 1);
		if (ExpectedReason)
			TestTrue(Name + TEXT(" reports the specific invalid field"), Feedback.LastError.Contains(ExpectedReason));
		TestEqual(Name + TEXT(" preserves world actors"), World->PersistentLevel->Actors.Num(), ActorCount);
		TestEqual(Name + TEXT(" preserves world package state"), World->GetOutermost()->IsDirty(), bWorldWasDirty);
		TestFalse(Name + TEXT(" keeps import package clean"), Package->IsDirty());
		TArray<UObject*> Objects;
		GetObjectsWithOuter(Package, Objects, true);
		TestEqual(Name + TEXT(" creates no package objects"), Objects.Num(), 0);
	};

	for (const FInvalidFile& InvalidFile : InvalidFiles)
	{
		if (InvalidFile.Contents != nullptr)
		{
			if (!TestTrue(TEXT("Write temporary FEM file"), FFileHelper::SaveStringToFile(InvalidFile.Contents, *Filename)))
			{
				return false;
			}
		}

		CheckRejectedFile(InvalidFile.Name, nullptr);
	}

	const FString Component = TEXT("{\"Name\":\"Mesh\",\"NumFBXFiles\":0,\"NumCornersPerShard\":0,\"CollisionGroup\":0,\"NumTags\":0,\"NumMaterials\":0,\"IsFracturable\":true,\"Tags\":[],\"Materials\":[],\"FbxFiles\":[],\"RenderMesh\":[],\"Node\":{\"IsBoundaryMarker\":false,\"NumAttributes\":0,\"NumDimensions\":3,\"NumPoints\":4,\"Data\":[1,0,0,0,2,1,0,0,3,0,1,0,4,0,0,1]},\"Ele\":{\"IsRegionAttribute\":false,\"NumNodesPerTets\":4,\"NumTetrahedra\":1,\"Data\":[{\"TetIndex\":1,\"Indices\":[1,2,3,4]}]}}");
	const FString RigidBodies = TEXT("[{\"Mass\":1,\"Position\":[0,0,0],\"Dimensions\":[1,1,1],\"Rotation\":[0,0,0,1],\"BodyInertiaTensor\":[1,0,0,0,1,0,0,0,1]}]");
	const FString RenderSection = TEXT("{\"NumberOfShards\":3,\"AssignedTetFaceBuffer\":[-1,-1,-1],\"BarycentricCoordsBuffer\":[1,0,0,0,0,1,0,0,0,0,1,0],\"BarycentricPosIds\":[0,1,2],\"TetAssignmentBuffer\":[0,0,0],\"ColorBuffer\":[],\"NormalBuffer\":[],\"PositionBuffer\":[0,0,0,1,0,0,0,1,0],\"TangentBuffer\":[],\"UVsBuffer\":[],\"ShardIds\":[0,1,2],\"Triangles\":[0,1,2],\"Centroids\":[]}");
	const FString PlaneConstraint = TEXT("{\"Name\":\"Plane\",\"BodyA\":0,\"BodyB\":0,\"IsRigidBodyA\":true,\"IsRigidBodyB\":true,\"TetIdA\":-1,\"TetIdB\":-1,\"PosBodySpaceA\":[0,0,0,0],\"PosBodySpaceB\":[0,0,0,0],\"NumberOfPlanes\":1,\"Planes\":[{\"Bias\":0,\"NonNegative\":false,\"PlaneNormal\":[1,0,0]}]}");
	const auto Document = [](const FString& Mesh, const FString& Bodies = TEXT("[]"), const FString& Planes = TEXT("[]"))
	{
		return FString::Printf(TEXT("{\"Version\":\"1.0\",\"FEMMeshComponents\":[%s],\"RigidBodies\":%s,\"RBAngleConstraints\":[],\"GlueConstraints\":[],\"PlaneConstraints\":%s}"), *Mesh, *Bodies, *Planes);
	};
	const auto WithRenderSection = [&](const FString& Section)
	{
		return Component.Replace(TEXT("\"RenderMesh\":[]"), *(FString(TEXT("\"RenderMesh\":[")) + Section + TEXT("]")));
	};
	struct FMalformedResource
	{
		const TCHAR* Name;
		FString Contents;
		const TCHAR* Reason;
	};
	const FMalformedResource MalformedResources[] =
	{
		{ TEXT("Short node data"), Document(Component.Replace(TEXT("\"NumPoints\":4"), TEXT("\"NumPoints\":5"))), TEXT("counts do not match") },
		{ TEXT("Negative point count"), Document(Component.Replace(TEXT("\"NumPoints\":4"), TEXT("\"NumPoints\":-1"))), TEXT("invalid tetrahedral node") },
		{ TEXT("Fractional point count"), Document(Component.Replace(TEXT("\"NumPoints\":4"), TEXT("\"NumPoints\":4.5"))), TEXT("invalid tetrahedral node") },
		{ TEXT("Unsupported node dimensions"), Document(Component.Replace(TEXT("\"NumDimensions\":3"), TEXT("\"NumDimensions\":2"))), TEXT("invalid tetrahedral node") },
		{ TEXT("Non-number node coordinate"), Document(Component.Replace(TEXT("1,0,0,0,2"), TEXT("1,null,0,0,2"))), TEXT("invalid tetrahedral node") },
		{ TEXT("Short element data"), Document(Component.Replace(TEXT("\"NumTetrahedra\":1"), TEXT("\"NumTetrahedra\":2"))), TEXT("counts do not match") },
		{ TEXT("Null element entry"), Document(Component.Replace(TEXT("{\"TetIndex\":1,\"Indices\":[1,2,3,4]}"), TEXT("null"))), TEXT("elements must be objects") },
		{ TEXT("Short element indices"), Document(Component.Replace(TEXT("\"Indices\":[1,2,3,4]"), TEXT("\"Indices\":[1,2,3]"))), TEXT("invalid tetrahedral element fields") },
		{ TEXT("Zero element vertex index"), Document(Component.Replace(TEXT("\"Indices\":[1,2,3,4]"), TEXT("\"Indices\":[0,2,3,4]"))), TEXT("vertex index is out of range") },
		{ TEXT("Repeated element vertices"), Document(Component.Replace(TEXT("\"Indices\":[1,2,3,4]"), TEXT("\"Indices\":[1,1,3,4]"))), TEXT("four different vertices") },
		{ TEXT("Mismatched tag count"), Document(Component.Replace(TEXT("\"NumTags\":0"), TEXT("\"NumTags\":1"))), TEXT("invalid component counts") },
		{ TEXT("Null tag entry"), Document(Component.Replace(TEXT("\"NumTags\":0"), TEXT("\"NumTags\":1")).Replace(TEXT("\"Tags\":[]"), TEXT("\"Tags\":[null]"))), TEXT("assignments must be objects") },
		{ TEXT("Out-of-range tag tet"), Document(Component.Replace(TEXT("\"NumTags\":0"), TEXT("\"NumTags\":1")).Replace(TEXT("\"Tags\":[]"), TEXT("\"Tags\":[{\"Tag\":\"Test\",\"TetIds\":[1]}]"))), TEXT("assigned tetrahedral index is out of range") },
		{ TEXT("Mismatched material faces"), Document(Component.Replace(TEXT("\"NumMaterials\":0"), TEXT("\"NumMaterials\":1")).Replace(TEXT("\"Materials\":[]"), TEXT("\"Materials\":[{\"MaterialName\":\"Test\",\"TetIds\":[0],\"NoFractureFaces\":[0,0]}]"))), TEXT("fracture face count") },
		{ TEXT("Numeric FBX reference"), Document(Component.Replace(TEXT("\"NumFBXFiles\":0"), TEXT("\"NumFBXFiles\":1")).Replace(TEXT("\"FbxFiles\":[]"), TEXT("\"FbxFiles\":[1]"))), TEXT("FBX references") },
		{ TEXT("Null render section"), Document(WithRenderSection(TEXT("null"))), TEXT("sections must be objects") },
		{ TEXT("Short barycentric data"), Document(WithRenderSection(RenderSection.Replace(TEXT("\"BarycentricCoordsBuffer\":[1,0,0,0,0,1,0,0,0,0,1,0]"), TEXT("\"BarycentricCoordsBuffer\":[]")))), TEXT("barycentric or centroid data is too short") },
		{ TEXT("Out-of-range triangle"), Document(WithRenderSection(RenderSection.Replace(TEXT("\"Triangles\":[0,1,2]"), TEXT("\"Triangles\":[0,1,3]")))), TEXT("triangle index is out of range") },
		{ TEXT("Out-of-range shard"), Document(WithRenderSection(RenderSection.Replace(TEXT("\"ShardIds\":[0,1,2]"), TEXT("\"ShardIds\":[0,1,3]")))), TEXT("shard index is out of range") },
		{ TEXT("Out-of-range barycentric index"), Document(WithRenderSection(RenderSection.Replace(TEXT("\"BarycentricPosIds\":[0,1,2]"), TEXT("\"BarycentricPosIds\":[0,1,3]")))), TEXT("barycentric index is out of range") },
		{ TEXT("Out-of-range tet assignment"), Document(WithRenderSection(RenderSection.Replace(TEXT("\"TetAssignmentBuffer\":[0,0,0]"), TEXT("\"TetAssignmentBuffer\":[0,0,1]")))), TEXT("tetrahedral assignment is out of range") },
		{ TEXT("Out-of-range tet face"), Document(WithRenderSection(RenderSection.Replace(TEXT("\"AssignedTetFaceBuffer\":[-1,-1,-1]"), TEXT("\"AssignedTetFaceBuffer\":[4,4,4]")))), TEXT("face index is out of range") },
		{ TEXT("Short centroid data"), Document(WithRenderSection(RenderSection.Replace(TEXT("\"Centroids\":[]"), TEXT("\"Centroids\":[0]")))), TEXT("barycentric or centroid data is too short") },
		{ TEXT("Short rigid body inertia"), Document(Component, RigidBodies.Replace(TEXT("\"BodyInertiaTensor\":[1,0,0,0,1,0,0,0,1]"), TEXT("\"BodyInertiaTensor\":[1,0,0]"))), TEXT("rigid body fields") },
		{ TEXT("Mismatched plane count"), Document(Component, RigidBodies, FString(TEXT("[")) + PlaneConstraint.Replace(TEXT("\"NumberOfPlanes\":1"), TEXT("\"NumberOfPlanes\":2")) + TEXT("]")), TEXT("matching plane definitions") },
		{ TEXT("Null constraint plane"), Document(Component, RigidBodies, FString(TEXT("[")) + PlaneConstraint.Replace(TEXT("{\"Bias\":0,\"NonNegative\":false,\"PlaneNormal\":[1,0,0]}"), TEXT("null")) + TEXT("]")), TEXT("planes must be objects") },
		{ TEXT("Short plane normal"), Document(Component, RigidBodies, FString(TEXT("[")) + PlaneConstraint.Replace(TEXT("\"PlaneNormal\":[1,0,0]"), TEXT("\"PlaneNormal\":[1,0]")) + TEXT("]")), TEXT("plane definition fields") },
		{ TEXT("Out-of-range constraint body"), Document(Component, RigidBodies, FString(TEXT("[")) + PlaneConstraint.Replace(TEXT("\"BodyB\":0"), TEXT("\"BodyB\":1")) + TEXT("]")), TEXT("body index is out of range") }
	};
	for (const FMalformedResource& Malformed : MalformedResources)
	{
		if (!TestTrue(TEXT("Write malformed FEM resource"), FFileHelper::SaveStringToFile(Malformed.Contents, *Filename)))
			return false;
		CheckRejectedFile(Malformed.Name, Malformed.Reason);
	}
	return true;
}

#endif
