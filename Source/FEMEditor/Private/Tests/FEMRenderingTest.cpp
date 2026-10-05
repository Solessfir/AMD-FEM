#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Components/SceneCaptureComponent2D.h"
#include "Components/DirectionalLightComponent.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "FEMFXMeshComponent.h"
#include "FEMMesh.h"
#include "GameFramework/Actor.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "HAL/IConsoleManager.h"
#include "HAL/FileManager.h"
#include "ImageUtils.h"
#include "Math/Float16Color.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "RenderingThread.h"
#include "ProceduralMeshComponent.h"
#include "ShaderCompiler.h"
#include "TextureResource.h"
#include "UObject/UnrealType.h"

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
	TestTrue(TEXT("FEM section produces visible geometry"), MeshPixels > 16);

	UFEMMesh* Replacement = NewObject<UFEMMesh>(Component);
	Options.Scale = 2.0f;
	if (!TestTrue(TEXT("Create replacement mesh"), Replacement->CreateProceduralMesh(Options)))
	{
		return false;
	}
	FProperty* MeshProperty = FindFProperty<FProperty>(UFEMFXMeshComponent::StaticClass(), GET_MEMBER_NAME_CHECKED(UFEMFXMeshComponent, FEMMesh));
	Component->PreEditChange(MeshProperty);
	Component->FEMMesh = Replacement;
	FPropertyChangedEvent ChangedEvent(MeshProperty);
	Component->PostEditChangeProperty(ChangedEvent);
	TestTrue(TEXT("Component remains registered after changing its mesh"), Component->IsRegistered());
	Capture->CaptureScene();
	FlushRenderingCommands();
	TArray<FColor> ReplacementPixels;
	if (!TestTrue(TEXT("Read replacement capture"), Resource->ReadPixels(ReplacementPixels)))
	{
		return false;
	}
	TestTrue(TEXT("Changing the mesh updates rendered geometry"), ReplacementPixels != Pixels);
	int32 ReplacementMeshPixels = 0;
	for (int32 Index = 0; Index < ReplacementPixels.Num(); ++Index)
	{
		const FColor& Pixel = ReplacementPixels[Index];
		const FColor& EmptyPixel = Background[Index];
		ReplacementMeshPixels += Pixel.R != EmptyPixel.R || Pixel.G != EmptyPixel.G || Pixel.B != EmptyPixel.B;
	}
	TestTrue(TEXT("Replacement mesh produces visible geometry"), ReplacementMeshPixels > 16);

	Component->PreEditChange(MeshProperty);
	Component->FEMMesh = nullptr;
	Component->PostEditChangeProperty(ChangedEvent);
	Capture->CaptureScene();
	FlushRenderingCommands();
	TArray<FColor> ClearedPixels;
	if (!TestTrue(TEXT("Read cleared capture"), Resource->ReadPixels(ClearedPixels)))
	{
		return false;
	}
	TestTrue(TEXT("Clearing the mesh removes rendered geometry"), ClearedPixels == Background);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFEMLitDeformationTest, "FEM.Rendering.LitDeformation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFEMLitDeformationTest::RunTest(const FString& Parameters)
{
	if (!FApp::CanEverRender())
	{
		AddError(TEXT("This test requires rendering. Run with a real RHI and -AllowCommandletRendering."));
		return false;
	}

	IConsoleVariable* EarlyZPass = IConsoleManager::Get().FindConsoleVariable(TEXT("r.EarlyZPass"));
	if (!TestNotNull(TEXT("Depth prepass setting exists"), EarlyZPass))
	{
		return false;
	}
	const int32 PreviousEarlyZPass = EarlyZPass->GetInt();
	const EConsoleVariableFlags PreviousEarlyZPassFlags = static_cast<EConsoleVariableFlags>(EarlyZPass->GetFlags());
	EarlyZPass->Set(2, ECVF_SetByCode);
	UWorld* World = UWorld::CreateWorld(EWorldType::EditorPreview, false);
	ON_SCOPE_EXIT
	{
		World->DestroyWorld(false);
		EarlyZPass->Set(PreviousEarlyZPass, ECVF_SetByCode);
		EarlyZPass->SetFlags(PreviousEarlyZPassFlags);
		FlushRenderingCommands();
	};

	AActor* Actor = World->SpawnActor<AActor>();
	UMaterial* Material = NewObject<UMaterial>(Actor);
	Material->SetShadingModel(MSM_DefaultLit);
	UMaterialExpressionConstant3Vector* BaseColor = NewObject<UMaterialExpressionConstant3Vector>(Material);
	BaseColor->Constant = FLinearColor(0.5f, 0.5f, 0.5f);
	Material->Expressions.Add(BaseColor);
	Material->BaseColor.Expression = BaseColor;
	UMaterialExpressionConstant3Vector* Normal = NewObject<UMaterialExpressionConstant3Vector>(Material);
	// A nonzero tangent-space Y component exercises the tangent handedness sign.
	Normal->Constant = FLinearColor(0.36f, 0.48f, 0.8f);
	Material->Expressions.Add(Normal);
	Material->Normal.Expression = Normal;
	Material->PostEditChange();
	GShaderCompilingManager->FinishAllCompilation();

	UFEMFXMeshComponent* Component = NewObject<UFEMFXMeshComponent>(Actor);
	Actor->SetRootComponent(Component);
	Actor->AddInstanceComponent(Component);
	Component->SetMobility(EComponentMobility::Static);
	Component->FEMMesh = NewObject<UFEMMesh>(Component);
	Component->RenderMaterials.Add(Material);
	Component->AddToSimulation = false;
	ProceduralMeshOptions Options;
	Options.NumCubesX = Options.NumCubesY = Options.NumCubesZ = 1;
	Options.CubeX = Options.CubeY = Options.CubeZ = 1.0f;
	Options.Scale = 1.0f;
	if (!TestTrue(TEXT("Create lit FEM mesh"), Component->FEMMesh->CreateProceduralMesh(Options)))
	{
		return false;
	}
	Component->SetWorldRotation(FRotator(15.0f, 35.0f, 10.0f));
	Component->RegisterComponent();
	const FFEMFXMeshSection& Section = Component->FEMMesh->GetImportedResource()->GetMeshSections()[0];

	TArray<FVector> Positions;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;
	TArray<FColor> Colors;
	TArray<FProcMeshTangent> Tangents;
	for (const FFEMFXMeshVertex& Vertex : Section.VertexBuffer)
	{
		Positions.Add(Vertex.Position);
		Normals.Add(Vertex.Normal);
		UVs.Add(Vertex.UV0);
		Colors.Add(Vertex.Color);
		Tangents.Add(FProcMeshTangent(Vertex.Tangent.TangentX, Vertex.Tangent.bFlipTangentY));
	}
	UProceduralMeshComponent* Reference = NewObject<UProceduralMeshComponent>(Actor);
	Actor->AddInstanceComponent(Reference);
	Reference->SetMobility(EComponentMobility::Static);
	Reference->CreateMeshSection(0, Positions, Section.IndexBuffer, Normals, UVs, Colors, Tangents, false);
	Reference->SetMaterial(0, Material);
	Reference->SetWorldTransform(Component->GetComponentTransform());
	Reference->RegisterComponent();
	GShaderCompilingManager->FinishAllCompilation();

	const FVector LocalCenter = Section.SectionLocalBox.GetCenter();
	const FVector ViewOffset(300.0f, 300.0f, 200.0f);
	UDirectionalLightComponent* Light = NewObject<UDirectionalLightComponent>(Actor);
	Actor->AddInstanceComponent(Light);
	Light->SetMobility(EComponentMobility::Movable);
	Light->SetWorldRotation((-ViewOffset).Rotation());
	Light->SetIntensity(3.0f);
	Light->SetCastShadows(false);
	Light->RegisterComponent();

	UTextureRenderTarget2D* Target = NewObject<UTextureRenderTarget2D>(Actor);
	Target->ClearColor = FLinearColor::Black;
	Target->InitCustomFormat(64, 64, PF_FloatRGBA, true);
	Target->UpdateResourceImmediate(true);
	USceneCaptureComponent2D* Capture = NewObject<USceneCaptureComponent2D>(Actor);
	Actor->AddInstanceComponent(Capture);
	Capture->TextureTarget = Target;
	Capture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
	Capture->bCaptureEveryFrame = false;
	Capture->bCaptureOnMovement = false;
	Capture->ShowFlags.SetPostProcessing(false);
	Capture->ShowFlags.SetAntiAliasing(false);
	Capture->RegisterComponent();
	const auto AimCapture = [&](const FVector& Center)
	{
		Capture->SetWorldLocation(Center + ViewOffset);
		Capture->SetWorldRotation((Center - Capture->GetComponentLocation()).Rotation());
	};
	AimCapture(Component->GetComponentTransform().TransformPosition(LocalCenter));
	const auto ReadCapture = [&](UPrimitiveComponent* Primitive, ESceneCaptureSource Source, TArray<FFloat16Color>& Pixels)
	{
		Capture->CaptureSource = Source;
		Capture->ClearShowOnlyComponents();
		Capture->ShowOnlyComponent(Primitive);
		Capture->CaptureScene();
		FlushRenderingCommands();
		return Target->GameThread_GetRenderTargetResource()->ReadFloat16Pixels(Pixels);
	};
	const FString ArtifactDirectory = FPaths::ProjectSavedDir() / TEXT("Automation/FEMRendering");
	IFileManager::Get().MakeDirectory(*ArtifactDirectory, true);
	int32 CaptureIndex = 0;
	const auto SaveCapture = [&](const TArray<FFloat16Color>& Pixels, const TArray<FFloat16Color>& Coverage, ESceneCaptureSource Source, const TCHAR* MeshName)
	{
		TArray<FColor> Image;
		for (int32 Index = 0; Index < Pixels.Num(); ++Index)
		{
			FLinearColor Color(Pixels[Index]);
			if (Source == SCS_Normal)
			{
				Color = Color * 0.5f + FLinearColor(0.5f, 0.5f, 0.5f, 0.0f);
			}
			const FLinearColor CoverageColor(Coverage[Index]);
			if (CoverageColor.R * CoverageColor.R + CoverageColor.G * CoverageColor.G + CoverageColor.B * CoverageColor.B < 0.01f)
			{
				Color = FLinearColor::Black;
			}
			Color.A = 1.0f;
			Image.Add(Color.ToFColor(Source != SCS_Normal));
		}
		TArray<uint8> PNG;
		FImageUtils::CompressImageArray(Target->SizeX, Target->SizeY, Image, PNG);
		const TCHAR* SourceName = Source == SCS_Normal ? TEXT("Normal") : Source == SCS_BaseColor ? TEXT("BaseColor") : TEXT("Lit");
		const FString Filename = ArtifactDirectory / FString::Printf(TEXT("%d-%s-%s.png"), CaptureIndex, MeshName, SourceName);
		TestTrue(TEXT("Write rendering comparison PNG"), FFileHelper::SaveArrayToFile(PNG, *Filename));
	};
	const auto CompareCapture = [&](ESceneCaptureSource Source, const TCHAR* Description, float Tolerance)
	{
		TArray<FFloat16Color> Coverage;
		TArray<FFloat16Color> Expected;
		TArray<FFloat16Color> Actual;
		if (!TestTrue(TEXT("Read native base-color coverage"), ReadCapture(Reference, SCS_BaseColor, Coverage))
			|| !TestTrue(TEXT("Read native procedural capture"), ReadCapture(Reference, Source, Expected))
			|| !TestTrue(TEXT("Read FEM capture"), ReadCapture(Component, Source, Actual))
			|| !TestEqual(TEXT("Comparison capture pixel count"), Actual.Num(), Expected.Num())
			|| !TestEqual(TEXT("Coverage capture pixel count"), Coverage.Num(), Expected.Num()))
		{
			return false;
		}
		SaveCapture(Expected, Coverage, Source, TEXT("Native"));
		SaveCapture(Actual, Coverage, Source, TEXT("FEM"));
		++CaptureIndex;
		int32 ReferencePixels = 0;
		int32 MatchingPixels = 0;
		for (int32 Index = 0; Index < Expected.Num(); ++Index)
		{
			const FLinearColor CoverageColor(Coverage[Index]);
			if (CoverageColor.R * CoverageColor.R + CoverageColor.G * CoverageColor.G + CoverageColor.B * CoverageColor.B < 0.01f)
			{
				continue;
			}
			++ReferencePixels;
			const FLinearColor ExpectedColor(Expected[Index]);
			const FVector ExpectedValue(ExpectedColor.R, ExpectedColor.G, ExpectedColor.B);
			const FLinearColor ActualColor(Actual[Index]);
			const FVector ActualValue(ActualColor.R, ActualColor.G, ActualColor.B);
			MatchingPixels += !ActualValue.ContainsNaN() && (ActualValue - ExpectedValue).Size() < Tolerance;
		}
		AddInfo(FString::Printf(TEXT("%s: %d/%d native mesh pixels match FEM."), Description, MatchingPixels, ReferencePixels));
		return TestTrue(TEXT("Native reference is visible"), ReferencePixels > 16)
			&& TestTrue(Description, MatchingPixels >= ReferencePixels * 0.9f);
	};

	CompareCapture(SCS_Normal, TEXT("Normal-mapped FEM matches native tangent basis"), 0.05f);
	CompareCapture(SCS_SceneColorHDR, TEXT("Lit FEM matches native procedural mesh"), 0.1f);

	UFEMTetMesh* TetMesh = Component->FEMMesh->GetTetMesh();
	FArrayProperty* PositionProperty = FindFProperty<FArrayProperty>(UFEMTetMesh::StaticClass(), TEXT("FEMMeshVertexPositions"));
	FArrayProperty* RotationProperty = FindFProperty<FArrayProperty>(UFEMTetMesh::StaticClass(), TEXT("FEMMeshVertexRotations"));
	if (!TestNotNull(TEXT("Tet position property exists"), PositionProperty)
		|| !TestNotNull(TEXT("Tet rotation property exists"), RotationProperty))
	{
		return false;
	}
	const FQuat TetRotation = FRotator(20.0f, -25.0f, 15.0f).Quaternion();
	const FVector Translation(150.0f, 0.0f, 0.0f);
	const auto DeformPosition = [&](const FVector& Position)
	{
		return TetRotation.RotateVector(Position - LocalCenter) + LocalCenter + Translation;
	};
	for (FVector& Position : *PositionProperty->ContainerPtrToValuePtr<TArray<FVector>>(TetMesh))
	{
		Position = DeformPosition(Position);
	}
	for (FFEMFXMeshTetRotation& Rotation : *RotationProperty->ContainerPtrToValuePtr<TArray<FFEMFXMeshTetRotation>>(TetMesh))
	{
		Rotation.Col0 = TetRotation.RotateVector(Rotation.Col0);
		Rotation.Col1 = TetRotation.RotateVector(Rotation.Col1);
		Rotation.Col2 = TetRotation.RotateVector(Rotation.Col2);
	}
	// Change the simulated buffers without moving the render vertices or component transform.
	ENQUEUE_RENDER_COMMAND(FEMTestDeformTetMesh)([Component](FRHICommandListImmediate& RHICmdList)
	{
		Component->PostEditSceneProxyUpdate();
	});
	FlushRenderingCommands();
	for (int32 Index = 0; Index < Positions.Num(); ++Index)
	{
		Positions[Index] = DeformPosition(Positions[Index]);
		Normals[Index] = TetRotation.RotateVector(Normals[Index]);
		Tangents[Index].TangentX = TetRotation.RotateVector(Tangents[Index].TangentX);
	}
	Reference->CreateMeshSection(0, Positions, Section.IndexBuffer, Normals, UVs, Colors, Tangents, false);
	AimCapture(Component->GetComponentTransform().TransformPosition(LocalCenter + Translation));
	CompareCapture(SCS_BaseColor, TEXT("Deformed FEM base pass agrees with depth prepass"), 0.05f);
	CompareCapture(SCS_Normal, TEXT("Rotated tet basis agrees with native component rotation"), 0.05f);
	CompareCapture(SCS_SceneColorHDR, TEXT("Deformed lit FEM matches native procedural mesh"), 0.1f);
	return true;
}

#endif
