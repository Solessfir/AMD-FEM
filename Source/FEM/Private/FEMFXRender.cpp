// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
FEMFXVertexFactory.cpp: Local vertex factory implementation
=============================================================================*/

#include "FEMFXRender.h"
#include "SceneView.h"
#include "SceneUtils.h"
#include "MeshBatch.h"
#include "ShaderParameterUtils.h"
#include "Rendering/ColorVertexBuffer.h"

/**
* Should we cache the material's shadertype on this platform with this vertex factory?
*/
bool FFEMFXMeshVertexFactory::ShouldCache(EShaderPlatform Platform, const class FMaterial* Material, const class FShaderType* ShaderType)
{
    return true;
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
    // SetPrimitiveIdStreamIndex(EVertexInputStreamType::Default, 0);
    // SetPrimitiveIdStreamIndex(EVertexInputStreamType::PositionOnly, 0);
    // SetPrimitiveIdStreamIndex(EVertexInputStreamType::PositionAndNormalOnly, 0);

    // If the vertex buffer containing position is not the same vertex buffer containing the rest of the data,
    // then initialize PositionStream and PositionDeclaration.
    if (true)//Data.PositionComponent.VertexBuffer != Data.TangentBasisComponents[0].VertexBuffer)
    {
        FVertexDeclarationElementList PositionOnlyStreamElements;
        PositionOnlyStreamElements.Add(AccessStreamComponent(Data.PositionComponent, 0, EVertexInputStreamType::PositionOnly));
        PositionOnlyStreamElements.Add(AccessStreamComponent(Data.BaryPosOffsetIdComponent, 16, EVertexInputStreamType::PositionOnly));
        PositionOnlyStreamElements.Add(AccessStreamComponent(Data.BaryPosBaseIdComponent, 17, EVertexInputStreamType::PositionOnly));

        InitDeclaration(PositionOnlyStreamElements, EVertexInputStreamType::PositionOnly);
    }

    FVertexDeclarationElementList Elements;
    if (Data.PositionComponent.VertexBuffer)
    {
        Elements.Add(AccessStreamComponent(Data.PositionComponent, 0));
    }

    // only tangent,normal are used by the stream. the binormal is derived in the shader
    for (int32 AxisIndex = 0; AxisIndex < 2; AxisIndex++)
    {
        // Only tangent (X) and normal (Z) are used by the stream; binormal is derived in the shader.
        if (Data.TangentBasisComponents[0].VertexBuffer) // TangentX
        {
            Elements.Add(AccessStreamComponent(Data.TangentBasisComponents[0], 1)); // TEXCOORD1
        }

        if (Data.TangentBasisComponents[1].VertexBuffer) // TangentZ
        {
            Elements.Add(AccessStreamComponent(Data.TangentBasisComponents[1], 2)); // TEXCOORD2
        }
    }

    if (Data.ColorComponent.VertexBuffer)
    {
        Elements.Add(AccessStreamComponent(Data.ColorComponent, 3));
    }
    else
    {
        //If the mesh has no color component, set the null color buffer on a new stream with a stride of 0.
        //This wastes 4 bytes of bandwidth per vertex, but prevents having to compile out twice the number of vertex factories.
        FVertexStreamComponent NullColorComponent(&GNullColorVertexBuffer, 0, 0, VET_Color);
        Elements.Add(AccessStreamComponent(NullColorComponent, 3));
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

    return nullptr;
}

// Implement vertex factory, proving shader file and options.

// Implement vertex factory, proving shader file and options.

// IMPLEMENT_VERTEX_FACTORY_TYPE(FFEMFXMeshVertexFactory, "/Plugin/FEM/Shaders/Private/FEMFXMeshVertexFactory.ush", true, true, true, false, true);

IMPLEMENT_VERTEX_FACTORY_TYPE(FFEMFXMeshVertexFactory, "/Plugin/FEM/Private/FEMFXMeshVertexFactory.ush", true, true, true, false, true);
