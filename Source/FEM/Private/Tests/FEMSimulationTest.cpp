#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "FEMFXVectormath.h"
#include "AMD_FEMFX.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "FEMFXMeshComponent.h"
#include "FEMFXScene.h"
#include "FEMFXTetMeshParameters.h"
#include "FEMMesh.h"
#include "GameFramework/Actor.h"
#include "Materials/Material.h"
#include "Misc/App.h"
#include "RenderingThread.h"
#include "PreProcessedMesh.h"

namespace
{
	FVector GetPosition(const AMD::FmTetMesh& Mesh, AMD::uint VertexId)
	{
		const AMD::FmVector3 Position = AMD::FmGetVertPosition(Mesh, VertexId);
		return FVector(Position.x, Position.y, Position.z);
	}

	struct FFEMSimulationFixture
	{
		UWorld* World = nullptr;
		AFEMFXScene* Scene = nullptr;
		UFEMFXMeshComponent* Component = nullptr;
		TArray<FVector> InitialPositions;
		TArray<bool> AnchoredVertices;
		FVector ForceDirection;

		~FFEMSimulationFixture()
		{
			if (Component)
				Component->ReleaseSimulationResources();
			if (Scene)
				Scene->Destroy();
			if (World)
			{
				World->DestroyWorld(false);
				GEngine->DestroyWorldContext(World);
			}
			FlushRenderingCommands();
		}

		bool Initialize(FAutomationTestBase& Test, bool FractureEnabled)
		{
			if (!FApp::CanEverRender())
			{
				Test.AddError(TEXT("FEM simulation tests require a real RHI and -AllowCommandletRendering."));
				return false;
			}

			World = UWorld::CreateWorld(EWorldType::EditorPreview, false);
			GEngine->CreateNewWorldContext(EWorldType::EditorPreview).SetCurrentWorld(World);
			World->SpawnActor<APreProcessedMeshHelper>();
			Scene = World->SpawnActor<AFEMFXScene>();
			Scene->Name = TEXT("Default");
			Scene->MaxTetMeshBuffers = 4;
			Scene->MaxTetMeshes = 64;
			Scene->MaxRigidBodies = 1;
			Scene->MaxVerts = 256;
			Scene->MaxDistanceContacts = Scene->MaxVolumeContacts = 128;
			Scene->MaxVolumeContactVerts = 256;
			Scene->MaxGlueConstraints = Scene->MaxPlaneConstraints = 16;
			Scene->MaxRigidBodyAngleConstraints = 16;
			Scene->MaxDeformationConstraints = 256;
			Scene->MaxBroadPhasePairs = Scene->MaxUserBroadPhasePairs = 128;
			Scene->NumWorkerThreads = 1;
			Scene->minPlaneConstraint = FVector(-1000.0f);
			Scene->maxPlaneConstraint = FVector(1000.0f);

			AActor* Actor = World->SpawnActor<AActor>();
			Component = NewObject<UFEMFXMeshComponent>(Actor);
			Actor->SetRootComponent(Component);
			Actor->AddInstanceComponent(Component);
			Component->FEMMesh = NewObject<UFEMMesh>(Component);
			Component->RenderMaterials.Add(UMaterial::GetDefaultMaterial(MD_Surface));
			Component->FractureEnabled = FractureEnabled;
			Component->PlasticityEnabled = false;
			Component->Kinematic = false;
			Component->AddToSimulation = true;
			Component->EditorOnly = false;
			Component->Scene = Scene;

			UFEMFXTetMeshParameters* Material = NewObject<UFEMFXTetMeshParameters>(Component);
			Material->restDensity = 20.0f;
			Material->youngsModulus = 1.0e5f;
			Material->poissonsRatio = 0.3f;
			Material->fractureStressThreshold = FractureEnabled ? 1.0f : 1.0e9f;
			Material->lowerDeformationLimit = Material->upperDeformationLimit = 0.0f;
			Component->MeshParameters.Add(TEXT("Default"), Material);

			ProceduralMeshOptions Options;
			if (FractureEnabled)
			{
				// Fracture planes pass through mesh vertices and need tets on both sides.
				Options.NumCubesX = Options.NumCubesY = Options.NumCubesZ = 2;
				Options.CubeX = Options.CubeY = Options.CubeZ = 0.5f;
			}
			if (!Test.TestTrue(TEXT("Procedural cube created"), Component->FEMMesh->CreateProceduralMesh(Options)))
				return false;

			Component->RegisterComponent();
			Actor->DispatchBeginPlay();
			if (!Test.TestTrue(TEXT("Component began play"), Component->HasBegunPlay()))
				return false;

			Component->LoadResource();
			Component->LoadSimObject();
			if (!Test.TestNotNull(TEXT("Simulation tet mesh buffer"), Component->GetTetMeshBuffer()))
				return false;
			if (!Test.TestTrue(TEXT("Scene allocated component"), Scene->AllocateNewMesh(Component)))
				return false;
			Component->CleanResources();

			AMD::FmSceneControlParams ControlParams = AMD::FmGetSceneControlParams(*Scene->GetSceneBuffer());
			ControlParams.gravityVector = AMD::FmVector3(0.0f);
			AMD::FmSetSceneControlParams(Scene->GetSceneBuffer(), ControlParams);
			AMD::FmTetMesh* Mesh = Component->GetTetMeshPtr();
			AMD::FmEnableSleeping(Scene->GetSceneBuffer(), Mesh, false);
			AMD::FmEnableStrainMagComputation(Mesh, true);
			const AMD::FmTetMaterialParams NativeMaterial = AMD::FmGetTetMaterialParams(*Mesh, 0);
			Test.TestEqual(TEXT("Native material receives stiffness"), NativeMaterial.youngsModulus, Material->youngsModulus);
			Test.AddInfo(FString::Printf(TEXT("FEM setup: fracture=%d, vertices=%u, tets=%u, max_meshes=%u, native_threshold=%g, native_stiffness=%g"), FractureEnabled, AMD::FmGetNumVerts(*Mesh), AMD::FmGetNumTets(*Mesh), AMD::FmGetMaxTetMeshes(*Component->GetTetMeshBuffer()), NativeMaterial.fractureStressThreshold, NativeMaterial.youngsModulus));
			if (FractureEnabled)
			{
				Test.TestEqual(TEXT("Native material receives fracture threshold"), NativeMaterial.fractureStressThreshold, Material->fractureStressThreshold);
				Test.TestTrue(TEXT("Fracture buffer reserves capacity for pieces"), AMD::FmGetMaxTetMeshes(*Component->GetTetMeshBuffer()) > 1);
				uint16_t AllTetFlags = 0;
				for (AMD::uint TetId = 0; TetId < AMD::FmGetNumTets(*Mesh); ++TetId)
				{
					AMD::uint GroupId;
					uint16_t TetFlags;
					AMD::FmGetTetMeshBufferTetInfo(&GroupId, &TetFlags, *Component->GetTetMeshBuffer(), TetId);
					AllTetFlags |= TetFlags;
				}
				Test.AddInfo(FString::Printf(TEXT("FEM initial tet flags: 0x%x"), AllTetFlags));
			}

			const AMD::uint VertexCount = AMD::FmGetNumVerts(*Mesh);
			FVector AnchoredCenter = FVector::ZeroVector;
			FVector FreeCenter = FVector::ZeroVector;
			int32 AnchoredCount = 0;
			int32 FreeCount = 0;
			float MinX = MAX_flt;
			float MaxX = -MAX_flt;
			for (AMD::uint VertexId = 0; VertexId < VertexCount; ++VertexId)
			{
				const float X = AMD::FmGetVertRestPosition(*Mesh, VertexId).x;
				MinX = FMath::Min(MinX, X);
				MaxX = FMath::Max(MaxX, X);
				InitialPositions.Add(GetPosition(*Mesh, VertexId));
			}
			const float MidX = (MinX + MaxX) * 0.5f;
			for (AMD::uint VertexId = 0; VertexId < VertexCount; ++VertexId)
			{
				const bool Anchored = AMD::FmGetVertRestPosition(*Mesh, VertexId).x < MidX;
				AnchoredVertices.Add(Anchored);
				if (Anchored)
				{
					AMD::FmAddVertFlags(Mesh, VertexId, FM_VERT_FLAG_KINEMATIC);
					AnchoredCenter += InitialPositions[VertexId];
					++AnchoredCount;
				}
				else
				{
					FreeCenter += InitialPositions[VertexId];
					++FreeCount;
				}
			}
			if (!Test.TestTrue(TEXT("Cube has anchored and free vertices"), AnchoredCount > 0 && FreeCount > 0))
				return false;
			ForceDirection = (FreeCenter / FreeCount - AnchoredCenter / AnchoredCount).GetSafeNormal();
			FlushRenderingCommands();
			return true;
		}

		void Step()
		{
			AMD::FmTetMeshBuffer* Buffer = Component->GetTetMeshBuffer();
			for (AMD::uint MeshId = 0; MeshId < AMD::FmGetNumTetMeshes(*Buffer); ++MeshId)
			{
				AMD::FmTetMesh* Mesh = AMD::FmGetTetMesh(*Buffer, MeshId);
				for (AMD::uint VertexId = 0; VertexId < AMD::FmGetNumVerts(*Mesh); ++VertexId)
				{
					if (AMD::FmGetVertFlags(*Mesh, VertexId) & FM_VERT_FLAG_KINEMATIC)
						continue;
					const float Weight = AMD::FmGetVertRestPosition(*Mesh, VertexId).y > 0.5f ? 1.5f : 1.0f;
					const FVector Force = ForceDirection * (5000.0f * Weight);
					AMD::FmAddForceToVert(Scene->GetSceneBuffer(), Mesh, VertexId, AMD::FmVector3(Force.X, Force.Y, Force.Z));
				}
			}
			AMD::FmUpdateScene(Scene->GetSceneBuffer(), 1.0f / 120.0f);
			Component->UpdateRenderingDataFromFracture();
			Component->UpdateSceneProxy();
		}

		bool CheckFinite(FAutomationTestBase& Test) const
		{
			AMD::FmTetMeshBuffer* Buffer = Component->GetTetMeshBuffer();
			for (AMD::uint MeshId = 0; MeshId < AMD::FmGetNumTetMeshes(*Buffer); ++MeshId)
			{
				const AMD::FmTetMesh& Mesh = *AMD::FmGetTetMesh(*Buffer, MeshId);
				for (AMD::uint VertexId = 0; VertexId < AMD::FmGetNumVerts(Mesh); ++VertexId)
				{
					const AMD::FmVector3 Velocity = AMD::FmGetVertVelocity(Mesh, VertexId);
					if (GetPosition(Mesh, VertexId).ContainsNaN() || !FMath::IsFinite(Velocity.x) || !FMath::IsFinite(Velocity.y) || !FMath::IsFinite(Velocity.z))
					return Test.TestTrue(TEXT("Simulation positions and velocities remain finite"), false);
				}
			}
			return true;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFEMDeformationTest, "FEM.Simulation.Deformation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFEMDeformationTest::RunTest(const FString& Parameters)
{
	FFEMSimulationFixture Fixture;
	if (!Fixture.Initialize(*this, false))
		return false;
	for (int32 Step = 0; Step < 60; ++Step)
		Fixture.Step();
	if (!Fixture.CheckFinite(*this))
		return false;

	const AMD::FmTetMesh& Mesh = *Fixture.Component->GetTetMeshPtr();
	float MaxEdgeChange = 0.0f;
	float MaxStrain = 0.0f;
	float MaxAnchorMovement = 0.0f;
	for (int32 VertexId = 0; VertexId < Fixture.InitialPositions.Num(); ++VertexId)
	{
		const FVector Position = GetPosition(Mesh, VertexId);
		MaxStrain = FMath::Max(MaxStrain, AMD::FmGetVertTetStrainMagMax(Mesh, VertexId));
		if (Fixture.AnchoredVertices[VertexId])
			MaxAnchorMovement = FMath::Max(MaxAnchorMovement, FVector::Distance(Position, Fixture.InitialPositions[VertexId]));
		for (int32 OtherId = VertexId + 1; OtherId < Fixture.InitialPositions.Num(); ++OtherId)
		{
			const float InitialLength = FVector::Distance(Fixture.InitialPositions[VertexId], Fixture.InitialPositions[OtherId]);
			const float FinalLength = FVector::Distance(Position, GetPosition(Mesh, OtherId));
			MaxEdgeChange = FMath::Max(MaxEdgeChange, FMath::Abs(FinalLength - InitialLength));
		}
	}
	TestTrue(TEXT("Force changes relative vertex distances"), MaxEdgeChange > 1.0e-4f);
	TestTrue(TEXT("Solver computes nonzero finite elastic strain"), FMath::IsFinite(MaxStrain) && MaxStrain > 1.0e-5f);
	TestTrue(TEXT("Anchored vertices remain fixed"), MaxAnchorMovement < 1.0e-4f);
	TestEqual(TEXT("Fracture-disabled mesh stays connected"), AMD::FmGetNumTetMeshes(*Fixture.Component->GetTetMeshBuffer()), 1u);
	FlushRenderingCommands();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFEMFractureTest, "FEM.Simulation.Fracture", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFEMFractureTest::RunTest(const FString& Parameters)
{
	FFEMSimulationFixture Fixture;
	if (!Fixture.Initialize(*this, true))
		return false;
	AMD::FmTetMeshBuffer* Buffer = Fixture.Component->GetTetMeshBuffer();
	const AMD::FmTetMesh& InitialMesh = *Fixture.Component->GetTetMeshPtr();
	const AMD::uint InitialVertexCount = AMD::FmGetNumVerts(InitialMesh);
	const AMD::uint InitialFaceCount = AMD::FmGetNumExteriorFaces(InitialMesh);
	TestEqual(TEXT("Procedural fracture metadata covers every tet"), Fixture.Component->FEMMesh->GetTetMesh()->GetTetFractureNewRenderFaces().Num(), static_cast<int32>(AMD::FmGetNumTets(*Buffer)));

	AMD::uint VertexCount = InitialVertexCount;
	AMD::uint FaceCount = InitialFaceCount;
	float PeakStrain = 0.0f;
	float PeakDisplacement = 0.0f;
	for (int32 Step = 0; Step < 120 && VertexCount == InitialVertexCount && FaceCount == InitialFaceCount; ++Step)
	{
		Fixture.Step();
		if (!Fixture.CheckFinite(*this))
			return false;
		VertexCount = FaceCount = 0;
		for (AMD::uint MeshId = 0; MeshId < AMD::FmGetNumTetMeshes(*Buffer); ++MeshId)
		{
			const AMD::FmTetMesh& Mesh = *AMD::FmGetTetMesh(*Buffer, MeshId);
			VertexCount += AMD::FmGetNumVerts(Mesh);
			FaceCount += AMD::FmGetNumExteriorFaces(Mesh);
			for (AMD::uint VertexId = 0; VertexId < AMD::FmGetNumVerts(Mesh); ++VertexId)
			{
				PeakStrain = FMath::Max(PeakStrain, AMD::FmGetVertTetStrainMagMax(Mesh, VertexId));
				const AMD::uint OriginalId = AMD::FmGetVertIndex0(Mesh, VertexId);
				if (OriginalId < static_cast<AMD::uint>(Fixture.InitialPositions.Num()))
					PeakDisplacement = FMath::Max(PeakDisplacement, FVector::Distance(GetPosition(Mesh, VertexId), Fixture.InitialPositions[OriginalId]));
			}
		}
		if (Step == 0 || Step == 1 || Step == 9 || Step == 59 || Step == 119 || VertexCount > InitialVertexCount || FaceCount > InitialFaceCount)
			AddInfo(FString::Printf(TEXT("FEM fracture step %d: meshes=%u, vertices=%u, exterior_faces=%u, peak_strain=%g, peak_displacement=%g"), Step + 1, AMD::FmGetNumTetMeshes(*Buffer), VertexCount, FaceCount, PeakStrain, PeakDisplacement));
	}
	TestTrue(TEXT("Applied stress duplicates fracture vertices"), VertexCount > InitialVertexCount);
	TestTrue(TEXT("Fracture creates new exterior faces"), FaceCount > InitialFaceCount);
	Fixture.Component->UpdateSceneProxyFromFracture();
	FlushRenderingCommands();

	Fixture.Component->ReleaseSimulationResources();
	Fixture.Component->ReleaseSimulationResources();
	TestNull(TEXT("Released simulation buffer"), Fixture.Component->GetTetMeshBuffer());
	TestNull(TEXT("Released tet mesh"), Fixture.Component->GetTetMeshPtr());
	TestFalse(TEXT("Released component removed from scene"), Fixture.Scene->m_ComponentsAllocated.Contains(Fixture.Component));
	return true;
}

#endif
