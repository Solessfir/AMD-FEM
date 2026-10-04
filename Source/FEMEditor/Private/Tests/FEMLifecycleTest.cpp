#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Editor.h"
#include "Engine/World.h"
#include "FEMActor.h"
#include "FEMFXMeshComponent.h"
#include "FEMFXScene.h"
#include "FEMMesh.h"
#include "Materials/Material.h"
#include "PreProcessedMesh.h"
#include "Tests/AutomationEditorCommon.h"

namespace
{

AFEMActor* CreateFEMActor(UWorld* World, AFEMFXScene* Scene)
{
	const FTransform Transform(FVector(0.0f, 0.0f, 500.0f));
	AFEMActor* Actor = World->SpawnActorDeferred<AFEMActor>(AFEMActor::StaticClass(), Transform);
	Actor->bOverride_FEMScene = true;
	Actor->SceneName = Scene->Name;

	UFEMMesh* Mesh = NewObject<UFEMMesh>(Actor);
	if (!Mesh->CreateProceduralMesh(ProceduralMeshOptions()))
	{
		Actor->Destroy();
		return nullptr;
	}

	for (int32 Index = 0; Index < 2; ++Index)
	{
		UFEMFXMeshComponent* Component = NewObject<UFEMFXMeshComponent>(Actor);
		Component->FEMMesh = Mesh;
		Component->RenderMaterials.Add(UMaterial::GetDefaultMaterial(MD_Surface));
		Component->AddToSimulation = Index == 0;
		if (Index == 0)
		{
			Actor->SetRootComponent(Component);
		}
		else
		{
			Component->SetupAttachment(Actor->GetRootComponent());
		}
		Actor->AddInstanceComponent(Component);
		Component->SetWorldTransform(Transform);
		Component->RegisterComponent();
	}

	Actor->FinishSpawning(Transform);
	return Actor;
}

class FFEMPIECycleCommand : public IAutomationLatentCommand
{
public:
	FFEMPIECycleCommand(FAutomationTestBase* InTest, bool bInDestroyScene)
		: Test(InTest), bDestroyScene(bInDestroyScene), StartTime(0.0)
	{
	}

	virtual bool Update() override
	{
		if (StartTime == 0.0)
		{
			StartTime = FPlatformTime::Seconds();
		}
		UWorld* World = GEditor->PlayWorld;
		if (!World || !World->HasBegunPlay())
		{
			if (FPlatformTime::Seconds() - StartTime < 60.0)
			{
				return false;
			}
			Test->AddError(TEXT("Timed out waiting for PIE to begin."));
			return true;
		}

		World->SpawnActor<APreProcessedMeshHelper>();
		AFEMFXScene* Scene = World->SpawnActor<AFEMFXScene>();
		Scene->Name = FGuid::NewGuid().ToString();
		Scene->MaxTetMeshBuffers = 4;
		Scene->MaxTetMeshes = 32;
		Scene->MaxRigidBodies = 4;
		Scene->MaxDistanceContacts = 128;
		Scene->MaxVolumeContacts = 16;
		Scene->MaxVolumeContactVerts = 128;
		Scene->MaxGlueConstraints = 16;
		Scene->MaxPlaneConstraints = 16;
		Scene->MaxRigidBodyAngleConstraints = 16;
		Scene->MaxDeformationConstraints = 64;
		Scene->MaxBroadPhasePairs = 64;
		Scene->MaxUserBroadPhasePairs = 32;
		Scene->MaxVerts = 1024;

		AFEMActor* Actor = CreateFEMActor(World, Scene);
		if (!Test->TestNotNull(TEXT("FEM actor created during PIE"), Actor))
		{
			return true;
		}
		UFEMFXMeshComponent* Component = CastChecked<UFEMFXMeshComponent>(Actor->GetRootComponent());
		Test->TestNotNull(TEXT("BeginPlay creates the simulation buffer"), Component->GetTetMeshBuffer());
		Test->TestEqual(TEXT("Only simulated components are registered"), Scene->m_ComponentsAllocated.Num(), 1);

		if (bDestroyScene)
		{
			Actor->Destroy();
			Test->TestEqual(TEXT("Actor destruction releases simulated and render-only components safely"), Scene->m_ComponentsAllocated.Num(), 0);

			Actor = CreateFEMActor(World, Scene);
			if (!Test->TestNotNull(TEXT("Replacement FEM actor"), Actor))
			{
				return true;
			}
			Component = CastChecked<UFEMFXMeshComponent>(Actor->GetRootComponent());
			Scene->RemoveActor(Actor);
			Test->TestTrue(TEXT("Removing an actor leaves it alive"), IsValid(Actor));
			Test->TestNull(TEXT("Removing an actor clears its scene"), Actor->Scene);
			Test->TestFalse(TEXT("Removed actor is no longer tracked"), Scene->FEMActors.Contains(Actor));
			Test->TestNull(TEXT("Removing an actor clears its simulation buffer"), Component->GetTetMeshBuffer());
			Test->TestEqual(TEXT("Removing an actor releases its scene registration"), Scene->m_ComponentsAllocated.Num(), 0);
			Test->TestEqual(TEXT("Released angle constraint query is empty"), Actor->GetAngleConstraintsByName(TEXT("Missing")).Num(), 0);
			Test->TestEqual(TEXT("Released glue constraint query is empty"), Actor->GetGlueConstraintsByName(TEXT("Missing")).Num(), 0);
			Test->TestEqual(TEXT("Released plane constraint query is empty"), Actor->GetPlaneConstraintsByName(TEXT("Missing")).Num(), 0);
			Scene->RemoveActor(Actor);
			Actor->Destroy();
			Actor = CreateFEMActor(World, Scene);
			if (!Test->TestNotNull(TEXT("Actor recreated after explicit removal"), Actor))
			{
				return true;
			}
			Component = CastChecked<UFEMFXMeshComponent>(Actor->GetRootComponent());
			Scene->Destroy();
			Test->TestNull(TEXT("Scene destruction clears the component buffer"), Component->GetTetMeshBuffer());
			Test->TestNull(TEXT("Scene destruction clears the component tet mesh"), Component->GetTetMeshPtr());
			Component->UpdateBounds();
			Component->ReleaseSimulationResources();
			Actor->Destroy();
		}
		return true;
	}

private:
	FAutomationTestBase* Test;
	bool bDestroyScene;
	double StartTime;
};

class FFEMWaitForPIEEndCommand : public IAutomationLatentCommand
{
public:
	explicit FFEMWaitForPIEEndCommand(FAutomationTestBase* InTest)
		: Test(InTest), StartTime(0.0)
	{
	}

	virtual bool Update() override
	{
		if (StartTime == 0.0)
		{
			StartTime = FPlatformTime::Seconds();
		}
		if (!GEditor->PlayWorld)
		{
			return true;
		}
		if (FPlatformTime::Seconds() - StartTime < 60.0)
		{
			return false;
		}
		Test->AddError(TEXT("Timed out waiting for PIE to end."));
		return true;
	}

private:
	FAutomationTestBase* Test;
	double StartTime;
};

}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFEMLifecycleTest, "FEM.Simulation.PIEStartStop", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFEMLifecycleTest::RunTest(const FString& Parameters)
{
	if (!GEditor || GEditor->PlayWorld)
	{
		AddError(TEXT("Run this test in an editor with no active PIE session."));
		return false;
	}

	for (int32 Cycle = 0; Cycle < 2; ++Cycle)
	{
		ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
		FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FFEMPIECycleCommand>(this, Cycle == 0));
		ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
		FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FFEMWaitForPIEEndCommand>(this));
	}
	return true;
}

#endif
