// Copyright 1998-2017 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
FEMFXVertexFactory.cpp: Local vertex factory implementation
=============================================================================*/

#include "FEMFXRender.h"
#include "SceneView.h"
#include "MeshBatch.h"
#include "ShaderParameterUtils.h"
#include "Rendering/ColorVertexBuffer.h"
#include "MeshDrawShaderBindings.h"
#include "MeshMaterialShader.h"

void FFEMFXMeshVertexFactoryShaderParameters::Bind(const FShaderParameterMap& ParameterMap)
{
    TetMeshVertexPosBufferParameter.Bind(ParameterMap, TEXT("TetMeshVertexPosBuffer"));
    TetMeshVertexRotBufferParameter.Bind(ParameterMap, TEXT("TetMeshVertexRotBuffer"));
	TetMeshDeformationBufferParameter.Bind(ParameterMap, TEXT("TetMeshDeformationBuffer"));
    TetVertexIdBufferParameter.Bind(ParameterMap, TEXT("TetVertexIdBuffer"));
    BarycentricPosIdBufferParameter.Bind(ParameterMap, TEXT("BarycentricPosIdBuffer"));
    BarycentricPosBufferParameter.Bind(ParameterMap, TEXT("BarycentricPosBuffer"));
}

void FFEMFXMeshVertexFactoryShaderParameters::Serialize(FArchive& Ar)
{
    Ar << TetMeshVertexPosBufferParameter
        << TetMeshVertexRotBufferParameter
        << TetMeshDeformationBufferParameter
        << TetVertexIdBufferParameter
        << BarycentricPosIdBufferParameter
        << BarycentricPosBufferParameter;
}

IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FFEMFXMeshVertexFactoryUniformShaderParameters, "FEMFXMeshVF");

TUniformBufferRef<FFEMFXMeshVertexFactoryUniformShaderParameters> CreateFEMFXMeshVFUniformBuffer(const FFEMFXMeshVertexFactory* VertexFactory, uint32 LODLightmapDataIndex, FColorVertexBuffer* OverrideColorVertexBuffer, int32 BaseVertexIndex)
{
    FFEMFXMeshVertexFactoryUniformShaderParameters UniformParameters;

    UniformParameters.LODLightmapDataIndex = LODLightmapDataIndex;
    int32 ColorIndexMask = 0;

    if (RHISupportsManualVertexFetch(GMaxRHIShaderPlatform))
    {
        UniformParameters.VertexFetch_PositionBuffer = VertexFactory->GetPositionsSRV();

        UniformParameters.VertexFetch_PackedTangentsBuffer = VertexFactory->GetTangentsSRV();
        UniformParameters.VertexFetch_TexCoordBuffer = VertexFactory->GetTextureCoordinatesSRV();

        if (OverrideColorVertexBuffer)
        {
            UniformParameters.VertexFetch_ColorComponentsBuffer = OverrideColorVertexBuffer->GetColorComponentsSRV();
            ColorIndexMask = OverrideColorVertexBuffer->GetNumVertices() > 1 ? ~0 : 0;
        }
        else
        {
            UniformParameters.VertexFetch_ColorComponentsBuffer = VertexFactory->GetColorComponentsSRV();
            ColorIndexMask = (int32)VertexFactory->GetColorIndexMask();
        }
    }
    else
    {
        UniformParameters.VertexFetch_PackedTangentsBuffer = GNullColorVertexBuffer.VertexBufferSRV;
        UniformParameters.VertexFetch_TexCoordBuffer = GNullColorVertexBuffer.VertexBufferSRV;
    }

    if (!UniformParameters.VertexFetch_ColorComponentsBuffer)
    {
        UniformParameters.VertexFetch_ColorComponentsBuffer = GNullColorVertexBuffer.VertexBufferSRV;
    }

    const int32 NumTexCoords = VertexFactory->GetNumTexcoords();
    const int32 LightMapCoordinateIndex = VertexFactory->GetLightMapCoordinateIndex();
    const int32 EffectiveBaseVertexIndex = RHISupportsAbsoluteVertexID(GMaxRHIShaderPlatform) ? 0 : BaseVertexIndex;
    UniformParameters.VertexFetch_Parameters = { ColorIndexMask, NumTexCoords, LightMapCoordinateIndex, EffectiveBaseVertexIndex };

    return TUniformBufferRef<FFEMFXMeshVertexFactoryUniformShaderParameters>::CreateUniformBufferImmediate(UniformParameters, UniformBuffer_MultiFrame);
}

void FFEMFXMeshVertexFactoryShaderParameters::GetElementShaderBindings(
    const FSceneInterface* Scene, const FSceneView* View,
    const FMeshMaterialShader* Shader,
    const EVertexInputStreamType InputStreamType,
    ERHIFeatureLevel::Type FeatureLevel, const FVertexFactory* VertexFactory,
    const FMeshBatchElement& BatchElement,
    FMeshDrawSingleShaderBindings& ShaderBindings,
    FVertexInputStreamArray& VertexStreams) const
{
    const auto* FEMVertexFactory = static_cast<const FFEMFXMeshVertexFactory*>(VertexFactory);

    if (FEMVertexFactory->SupportsManualVertexFetch(FeatureLevel) || UseGPUScene(GMaxRHIShaderPlatform, FeatureLevel))
    {
        // Decode VertexFactoryUserData as VertexFactoryUniformBuffer
        FRHIUniformBuffer* VertexFactoryUniformBuffer = static_cast<FRHIUniformBuffer*>(BatchElement.VertexFactoryUserData);
        if (!VertexFactoryUniformBuffer)
        {
            // No batch element override
            VertexFactoryUniformBuffer = FEMVertexFactory->GetUniformBuffer();
        }

        ShaderBindings.Add(Shader->GetUniformBufferParameter<FLocalVertexFactoryUniformShaderParameters>(), VertexFactoryUniformBuffer);
    }

    if (BatchElement.bUserDataIsColorVertexBuffer)
    {
        FColorVertexBuffer* OverrideColorVertexBuffer = (FColorVertexBuffer*)BatchElement.UserData;
        check(OverrideColorVertexBuffer);

        if (!FEMVertexFactory->SupportsManualVertexFetch(FeatureLevel))
        {
            FEMVertexFactory->GetColorOverrideStream(OverrideColorVertexBuffer, VertexStreams);
        }
    }
    else
    {
        FFEMFXMeshBatchElementParams* BatchElementParams = (FFEMFXMeshBatchElementParams*)BatchElement.UserData;
        check(BatchElementParams);

        FShaderResourceViewRHIRef TetMeshVertexPosBufferSRV = BatchElementParams->TetMeshVertexPosBufferSRV;
        FShaderResourceViewRHIRef TetMeshVertexRotBufferSRV = BatchElementParams->TetMeshVertexRotBufferSRV;
        FShaderResourceViewRHIRef TetMeshDeformationBufferSRV = BatchElementParams->TetMeshDeformationBufferSRV;
        FShaderResourceViewRHIRef TetVertexIdBufferSRV = BatchElementParams->TetVertexIdBufferSRV;
        FShaderResourceViewRHIRef BarycentricPosIdBufferSRV = BatchElementParams->BarycentricPosIdBufferSRV;
        FShaderResourceViewRHIRef BarycentricPosBufferSRV = BatchElementParams->BarycentricPosBufferSRV;

        if (TetMeshVertexPosBufferParameter.IsBound() && TetMeshVertexPosBufferSRV)
        {
            if (Shader->GetTarget().Frequency == SF_Vertex)
            {
                ShaderBindings.Add(TetMeshVertexPosBufferParameter, TetMeshVertexPosBufferSRV);
            }
        }
        if (TetMeshVertexRotBufferParameter.IsBound() && TetMeshVertexRotBufferSRV)
        {
            if (Shader->GetTarget().Frequency == SF_Vertex)
            {
                ShaderBindings.Add(TetMeshVertexRotBufferParameter, TetMeshVertexRotBufferSRV);
            }
        }
        if (TetMeshDeformationBufferParameter.IsBound() && TetMeshDeformationBufferSRV)
        {
            if (Shader->GetTarget().Frequency == SF_Vertex)
            {
                ShaderBindings.Add(TetMeshDeformationBufferParameter, TetMeshDeformationBufferSRV);
            }
        }
        if (TetVertexIdBufferParameter.IsBound() && TetVertexIdBufferSRV)
        {
            if (Shader->GetTarget().Frequency == SF_Vertex)
            {
                ShaderBindings.Add(TetVertexIdBufferParameter, TetVertexIdBufferSRV);
            }
        }
        if (BarycentricPosIdBufferParameter.IsBound() && BarycentricPosIdBufferSRV)
        {
            if (Shader->GetTarget().Frequency == SF_Vertex)
            {
                ShaderBindings.Add(BarycentricPosIdBufferParameter, BarycentricPosIdBufferSRV);
            }
        }
        if (BarycentricPosBufferParameter.IsBound() && BarycentricPosBufferSRV)
        {
            if (Shader->GetTarget().Frequency == SF_Vertex)
            {
                ShaderBindings.Add(BarycentricPosBufferParameter, BarycentricPosBufferSRV);
            }
        }
    }
}

/**
* Should we cache the material's shadertype on this platform with this vertex factory?
*/
bool FFEMFXMeshVertexFactory::ShouldCompilePermutation(const FVertexFactoryShaderPermutationParameters& Parameters)
{
    return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
}

void FFEMFXMeshVertexFactory::ModifyCompilationEnvironment(const FVertexFactoryShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
{
    OutEnvironment.SetDefine(TEXT("VF_SUPPORTS_SPEEDTREE_WIND"), TEXT("1"));

    const bool ContainsManualVertexFetch = OutEnvironment.GetDefinitions().Contains("MANUAL_VERTEX_FETCH");
    if (!ContainsManualVertexFetch && RHISupportsManualVertexFetch(Parameters.Platform))
    {
        OutEnvironment.SetDefine(TEXT("MANUAL_VERTEX_FETCH"), TEXT("1"));
    }

    const bool bUseGPUSceneAndPrimitiveIdStream = Parameters.VertexFactoryType->SupportsPrimitiveIdStream() && UseGPUScene(Parameters.Platform, GetMaxSupportedFeatureLevel(Parameters.Platform));
    OutEnvironment.SetDefine(TEXT("VF_SUPPORTS_PRIMITIVE_SCENE_DATA"), bUseGPUSceneAndPrimitiveIdStream);
    OutEnvironment.SetDefine(TEXT("VF_GPU_SCENE_BUFFER"), bUseGPUSceneAndPrimitiveIdStream && !GPUSceneUseTexture2D(Parameters.Platform));
}

void FFEMFXMeshVertexFactory::ValidateCompiledResult(const FVertexFactoryType* Type, EShaderPlatform Platform, const FShaderParameterMap& ParameterMap, TArray<FString>& OutErrors)
{
    if (Type->SupportsPrimitiveIdStream() && UseGPUScene(Platform, GetMaxSupportedFeatureLevel(Platform)) && ParameterMap.ContainsParameterAllocation(FPrimitiveUniformShaderParameters::StaticStructMetadata.GetShaderVariableName()))
    {
        OutErrors.AddUnique(*FString::Printf(TEXT("Shader attempted to bind the Primitive uniform buffer even though Vertex Factory %s computes a PrimitiveId per-instance.  This will break auto-instancing.  Shaders should use GetPrimitiveData(Parameters.PrimitiveId).Member instead of Primitive.Member."), Type->GetName()));
    }
}

void FFEMFXMeshVertexFactory::SetData(const FDataType& InData)
{
    check(IsInRenderingThread());

    // The shader code makes assumptions that the color component is a FColor, performing swizzles on ES2 and Metal platforms as necessary
    // If the color is sent down as anything other than VET_Color then you'll get an undesired swizzle on those platforms
    check((InData.ColorComponent.Type == VET_None) || (InData.ColorComponent.Type == VET_Color));

    Data = InData;
    UpdateRHI();
}

/**
* Copy the data from another vertex factory
* @param Other - factory to copy from
*/
void FFEMFXMeshVertexFactory::Copy(const FFEMFXMeshVertexFactory& Other)
{
    FFEMFXMeshVertexFactory* VertexFactory = this;
    const FDataType* DataCopy = &Other.Data;
    ENQUEUE_RENDER_COMMAND(FFEMFXMeshVertexFactoryCopyData)([VertexFactory, DataCopy](FRHICommandListImmediate& RHICmdList)
    {
        VertexFactory->Data = *DataCopy;
    });
    BeginUpdateResourceRHI(this);
}

void FFEMFXMeshVertexFactory::InitRHI()
{
    // We create different streams based on feature level
    check(HasValidFeatureLevel());

    // VertexFactory needs to be able to support max possible shader platform and feature level
    // in case if we switch feature level at runtime.
    const bool bCanUseGPUScene = UseGPUScene(GMaxRHIShaderPlatform, GMaxRHIFeatureLevel);

    // If the vertex buffer containing position is not the same vertex buffer containing the rest of the data,
    // then initialize PositionStream and PositionDeclaration.
    if (true)//Data.PositionComponent.VertexBuffer != Data.TangentBasisComponents[0].VertexBuffer)
    {
        auto AddDeclaration = [this, bCanUseGPUScene](EVertexInputStreamType InputStreamType, bool bAddNormal)
        {
            FVertexDeclarationElementList StreamElements;
            StreamElements.Add(AccessStreamComponent(Data.PositionComponent, 0, InputStreamType));
            StreamElements.Add(AccessStreamComponent(Data.BaryPosOffsetIdComponent, 16, InputStreamType));
            StreamElements.Add(AccessStreamComponent(Data.BaryPosBaseIdComponent, 17, InputStreamType));

            bAddNormal = bAddNormal && Data.TangentBasisComponents[1].VertexBuffer != nullptr;
            if (bAddNormal)
            {
                StreamElements.Add(AccessStreamComponent(Data.TangentBasisComponents[1], 2, InputStreamType));
            }

            const uint8 TypeIndex = static_cast<uint8>(InputStreamType);
            PrimitiveIdStreamIndex[TypeIndex] = -1;
            if (GetType()->SupportsPrimitiveIdStream() && bCanUseGPUScene)
            {
                // When the VF is used for rendering in normal mesh passes, this vertex buffer and offset will be overridden
                StreamElements.Add(AccessStreamComponent(FVertexStreamComponent(&GPrimitiveIdDummy, 0, 0, sizeof(uint32), VET_UInt, EVertexStreamUsage::Instancing), 1, InputStreamType));
                PrimitiveIdStreamIndex[TypeIndex] = StreamElements.Last().StreamIndex;
            }

            InitDeclaration(StreamElements, InputStreamType);
        };
        AddDeclaration(EVertexInputStreamType::PositionOnly, false);
        AddDeclaration(EVertexInputStreamType::PositionAndNormalOnly, true);
    }

    FVertexDeclarationElementList Elements;
    if (Data.PositionComponent.VertexBuffer != nullptr)
    {
        Elements.Add(AccessStreamComponent(Data.PositionComponent, 0));
    }

    {
        const uint8 Index = static_cast<uint8>(EVertexInputStreamType::Default);
        PrimitiveIdStreamIndex[Index] = -1;
        if (GetType()->SupportsPrimitiveIdStream() && bCanUseGPUScene)
        {
            // When the VF is used for rendering in normal mesh passes, this vertex buffer and offset will be overridden
            Elements.Add(AccessStreamComponent(FVertexStreamComponent(&GPrimitiveIdDummy, 0, 0, sizeof(uint32), VET_UInt, EVertexStreamUsage::Instancing), 13));
            PrimitiveIdStreamIndex[Index] = Elements.Last().StreamIndex;
        }
    }

    // only tangent,normal are used by the stream. the binormal is derived in the shader
    uint8 TangentBasisAttributes[2] = { 1, 2 };
    for (int32 AxisIndex = 0; AxisIndex < 2; AxisIndex++)
    {
        if (Data.TangentBasisComponents[AxisIndex].VertexBuffer != nullptr)
        {
            Elements.Add(AccessStreamComponent(Data.TangentBasisComponents[AxisIndex], TangentBasisAttributes[AxisIndex]));
        }
    }

    ColorStreamIndex = -1;
    if (Data.ColorComponent.VertexBuffer)
    {
        Elements.Add(AccessStreamComponent(Data.ColorComponent, 3));
        ColorStreamIndex = Elements.Last().StreamIndex;
    }
    else
    {
        //If the mesh has no color component, set the null color buffer on a new stream with a stride of 0.
        //This wastes 4 bytes of bandwidth per vertex, but prevents having to compile out twice the number of vertex factories.
        FVertexStreamComponent NullColorComponent(&GNullColorVertexBuffer, 0, 0, VET_Color, EVertexStreamUsage::ManualFetch);
        Elements.Add(AccessStreamComponent(NullColorComponent, 3));
        ColorStreamIndex = Elements.Last().StreamIndex;
    }
    ColorStreamIndex = Elements.Last().StreamIndex;

    if (Data.TextureCoordinates.Num())
    {
        constexpr int32 BaseTexCoordAttribute = 4;
        for (int32 CoordinateIndex = 0; CoordinateIndex < Data.TextureCoordinates.Num(); CoordinateIndex++)
        {
            Elements.Add(AccessStreamComponent(
                Data.TextureCoordinates[CoordinateIndex],
                BaseTexCoordAttribute + CoordinateIndex
            ));
        }

        for (int32 CoordinateIndex = Data.TextureCoordinates.Num(); CoordinateIndex < MAX_STATIC_TEXCOORDS / 2; CoordinateIndex++)
        {
            Elements.Add(AccessStreamComponent(
                Data.TextureCoordinates[Data.TextureCoordinates.Num() - 1],
                BaseTexCoordAttribute + CoordinateIndex
            ));
        }
    }

    if (Data.LightMapCoordinateComponent.VertexBuffer)
    {
        Elements.Add(AccessStreamComponent(Data.LightMapCoordinateComponent, 15));
    }
    else if (Data.TextureCoordinates.Num())
    {
        Elements.Add(AccessStreamComponent(Data.TextureCoordinates[0], 15));
    }

    if (Data.BaryPosOffsetIdComponent.VertexBuffer)
    {
        Elements.Add(AccessStreamComponent(Data.BaryPosOffsetIdComponent, 16));
    }

    if (Data.BaryPosBaseIdComponent.VertexBuffer)
    {
        Elements.Add(AccessStreamComponent(Data.BaryPosBaseIdComponent, 17));
    }

    check(Streams.Num() > 0);

    InitDeclaration(Elements);

    check(IsValidRef(GetDeclaration()));
}

FVertexFactoryShaderParameters* FFEMFXMeshVertexFactory::ConstructShaderParameters(EShaderFrequency ShaderFrequency)
{
    if (ShaderFrequency == SF_Vertex)
    {
        return new FFEMFXMeshVertexFactoryShaderParameters();
    }

#if RHI_RAYTRACING
    if (ShaderFrequency == SF_RayHitGroup)
    {
        return new FFEMFXMeshVertexFactoryShaderParameters();
    }

    if (ShaderFrequency == SF_Compute)
    {
        return new FFEMFXMeshVertexFactoryShaderParameters();
    }
#endif // RHI_RAYTRACING

    return nullptr;
}

// Implement vertex factory, proving shader file and options.
//IMPLEMENT_VERTEX_FACTORY_TYPE_EX(FFEMFXMeshVertexFactory, "/Plugin/FEM/Private/FEMFXMeshVertexFactory.ush", true, true, true, false, true, true, true);

 FVertexFactoryType FFEMFXMeshVertexFactory::StaticType( L"FFEMFXMeshVertexFactory",
     L"/Plugin/FEM/Private/FEMFXMeshVertexFactory.ush", true, true, true,
     false, true, true, true,
     &ConstructVertexFactoryParameters<FFEMFXMeshVertexFactory>,
     &GetVertexFactoryParametersLayout<FFEMFXMeshVertexFactory>,
     &GetVertexFactoryParametersElementShaderBindings<FFEMFXMeshVertexFactory>,
     FFEMFXMeshVertexFactory::ShouldCompilePermutation,
     FFEMFXMeshVertexFactory::ModifyCompilationEnvironment, FFEMFXMeshVertexFactory::ValidateCompiledResult, FFEMFXMeshVertexFactory::SupportsTessellationShaders );  FVertexFactoryType* FFEMFXMeshVertexFactory::GetType() const { return &StaticType; };


// typedef bool (*ShouldCacheType)(const FVertexFactoryShaderPermutationParameters&);
// typedef void (*ModifyCompilationEnvironmentType)(const FVertexFactoryShaderPermutationParameters&, FShaderCompilerEnvironment&);
// typedef void (*ValidateCompiledResultType)(const FVertexFactoryType*, EShaderPlatform, const FShaderParameterMap& ParameterMap, TArray<FString>& OutErrors);
// typedef bool (*SupportsTessellationShadersType)();
