#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "FEMFXMeshComponent.h"
#include "FEMMesh.h"
#include "GameFramework/Actor.h"
#include "Materials/Material.h"
#include "Misc/App.h"
#include "Misc/ScopeExit.h"
#include "RenderingThread.h"
#include "TextureResource.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFEMRenderingTest, "FEM.Rendering.ProceduralMesh", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFEMRenderingTest::RunTest(const FString& Parameters)
{
	if (!FApp::CanEverRender())
	{
		AddError(TEXT("This test requires rendering. Run with a real RHI and -AllowCommandletRendering."));
		return false;
	}

	UWorld* World = UWorld::CreateWorld(EWorldType::EditorPreview, false);
	ON_SCOPE_EXIT
	{
		World->DestroyWorld(false);
		FlushRenderingCommands();
	};

	AActor* Actor = World->SpawnActor<AActor>();
	UFEMFXMeshComponent* Component = NewObject<UFEMFXMeshComponent>(Actor);
	Actor->SetRootComponent(Component);
	Actor->AddInstanceComponent(Component);
	Component->FEMMesh = NewObject<UFEMMesh>(Component);
	Component->RenderMaterials.Add(UMaterial::GetDefaultMaterial(MD_Surface));
	Component->AddToSimulation = false;

	ProceduralMeshOptions Options;
	Options.NumCubesX = Options.NumCubesY = Options.NumCubesZ = 1;
	Options.CubeX = Options.CubeY = Options.CubeZ = 1.0f;
	Options.Scale = 1.0f;
	Component->FEMMesh->CreateProceduralMesh(Options);
	Component->RegisterComponent();

	if (!TestEqual(TEXT("Procedural mesh section count"), Component->FEMMesh->GetImportedResource()->GetNumSections(), 1))
	{
		return false;
	}
	const FFEMFXMeshSection& Section = Component->FEMMesh->GetImportedResource()->GetMeshSections()[0];
	TestTrue(TEXT("Procedural mesh has triangles"), Section.IndexBuffer.Num() >= 3);
	const FVector Center = Section.SectionLocalBox.GetCenter();

	UTextureRenderTarget2D* Target = NewObject<UTextureRenderTarget2D>(Actor);
	Target->ClearColor = FLinearColor::Black;
	Target->InitCustomFormat(64, 64, PF_B8G8R8A8, true);
	Target->UpdateResourceImmediate(true);

	USceneCaptureComponent2D* Capture = NewObject<USceneCaptureComponent2D>(Actor);
	Actor->AddInstanceComponent(Capture);
	Capture->TextureTarget = Target;
	Capture->CaptureSource = SCS_BaseColor;
	Capture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
	Capture->bCaptureEveryFrame = false;
	Capture->bCaptureOnMovement = false;
	Capture->SetWorldLocation(Center + FVector(300.0f, -300.0f, 200.0f));
	Capture->SetWorldRotation((Center - Capture->GetComponentLocation()).Rotation());
	Capture->RegisterComponent();

	// Compare against an empty capture so background pixels cannot satisfy the test.
	TArray<FColor> Background;
	Capture->CaptureScene();
	FlushRenderingCommands();
	FTextureRenderTargetResource* Resource = Target->GameThread_GetRenderTargetResource();
	if (!TestTrue(TEXT("Read empty capture"), Resource->ReadPixels(Background)))
	{
		return false;
	}

	TArray<FColor> Pixels;
	Capture->ShowOnlyComponent(Component);
	Capture->CaptureScene();
	FlushRenderingCommands();
	if (!TestTrue(TEXT("Read FEM capture"), Resource->ReadPixels(Pixels)))
	{
		return false;
	}
	if (!TestEqual(TEXT("Capture pixel count"), Pixels.Num(), Background.Num()))
	{
		return false;
	}

	int32 MeshPixels = 0;
	for (int32 Index = 0; Index < Pixels.Num(); ++Index)
	{
		const FColor& Pixel = Pixels[Index];
		const FColor& EmptyPixel = Background[Index];
		MeshPixels += Pixel.R != EmptyPixel.R || Pixel.G != EmptyPixel.G || Pixel.B != EmptyPixel.B;
	}
	AddInfo(FString::Printf(TEXT("FEM geometry changed %d of %d capture pixels."), MeshPixels, Pixels.Num()));
	return TestTrue(TEXT("FEM section produces visible geometry"), MeshPixels > 16);
}

#endif
