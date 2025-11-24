// Copyright 1998-2017 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DynamicMeshBuilder.h"
#include "FEMMeshTypes.h"
#include "Materials/Material.h"
#include "LocalVertexFactory.h"

BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FFEMFXMeshVertexFactoryUniformShaderParameters, )
SHADER_PARAMETER(FIntVector4, VertexFetch_Parameters)
SHADER_PARAMETER(uint32, LODLightmapDataIndex)
SHADER_PARAMETER_SRV(Buffer<float2>, VertexFetch_TexCoordBuffer)
SHADER_PARAMETER_SRV(Buffer<float>, VertexFetch_PositionBuffer)
SHADER_PARAMETER_SRV(Buffer<float4>, VertexFetch_PackedTangentsBuffer)
SHADER_PARAMETER_SRV(Buffer<float4>, VertexFetch_ColorComponentsBuffer)
END_GLOBAL_SHADER_PARAMETER_STRUCT()

extern TUniformBufferRef<FFEMFXMeshVertexFactoryUniformShaderParameters> CreateFEMFXMeshVFUniformBuffer(
    const class FFEMFXMeshVertexFactory* VertexFactory,
    uint32 LODLightmapDataIndex,
    class FColorVertexBuffer* OverrideColorVertexBuffer,
    int32 BaseVertexIndex);

/** Vertex Factory */
class FEM_API FFEMFXMeshVertexFactory : public FVertexFactory
{
    DECLARE_VERTEX_FACTORY_TYPE(FFEMFXMeshVertexFactory);

public:

    FFEMFXMeshVertexFactory(ERHIFeatureLevel::Type InFeatureLevel)
        : FVertexFactory(InFeatureLevel), ColorStreamIndex(-1)
    {
        bSupportsManualVertexFetch = true;
    }

    struct FDataType
    {
        /** The stream to read the vertex position from. */
        FVertexStreamComponent PositionComponent;

        /** The streams to read the tangent basis from. */
        FVertexStreamComponent TangentBasisComponents[2];

        /** The streams to read the texture coordinates from. */
        TArray<FVertexStreamComponent, TFixedAllocator<MAX_STATIC_TEXCOORDS / 2> > TextureCoordinates;

        /** The stream to read the shadow map texture coordinates from. */
        FVertexStreamComponent LightMapCoordinateComponent;

        /** The stream to read the vertex color from. */
        FVertexStreamComponent ColorComponent;

        FRHIShaderResourceView* PositionComponentSRV = nullptr;

        FRHIShaderResourceView* TangentsSRV = nullptr;

        /** A SRV to manually bind and load TextureCoordinates in the Vertexshader. */
        FRHIShaderResourceView* TextureCoordinatesSRV = nullptr;

        /** A SRV to manually bind and load Colors in the Vertexshader. */
        FRHIShaderResourceView* ColorComponentsSRV = nullptr;

        /** The stream to read the shard vertex id from. */
        FVertexStreamComponent BaryPosOffsetIdComponent;

        /** The stream to read the barycentric position base id from. */
        FVertexStreamComponent BaryPosBaseIdComponent;

        int32 LightMapCoordinateIndex = -1;
        int32 NumTexCoords = -1;
        uint32 ColorIndexMask = ~0u;
    };

    /** Init function that should only be called on render thread. */
    void Init_RenderThread(const FFEMFXMeshVertexBuffer* VertexBuffer)
    {
        check(IsInRenderingThread());

        // Initialize the vertex factory's stream components.
        FDataType NewData;
        NewData.PositionComponent = STRUCTMEMBER_VERTEXSTREAMCOMPONENT(VertexBuffer, FFEMFXMeshRenderVertex, Position, VET_Float3);
        NewData.TextureCoordinates.Add(
            FVertexStreamComponent(VertexBuffer, STRUCT_OFFSET(FFEMFXMeshRenderVertex, TextureCoordinate), sizeof(FFEMFXMeshRenderVertex), VET_Float2)
        );
        NewData.TangentBasisComponents[0] = STRUCTMEMBER_VERTEXSTREAMCOMPONENT(VertexBuffer, FFEMFXMeshRenderVertex, TangentX, VET_PackedNormal);
        NewData.TangentBasisComponents[1] = STRUCTMEMBER_VERTEXSTREAMCOMPONENT(VertexBuffer, FFEMFXMeshRenderVertex, TangentZ, VET_PackedNormal);
        NewData.ColorComponent = STRUCTMEMBER_VERTEXSTREAMCOMPONENT(VertexBuffer, FFEMFXMeshRenderVertex, Color, VET_Color);
        NewData.BaryPosOffsetIdComponent = STRUCTMEMBER_VERTEXSTREAMCOMPONENT(VertexBuffer, FFEMFXMeshRenderVertex, ShardId, VET_Float1);
        NewData.BaryPosBaseIdComponent = STRUCTMEMBER_VERTEXSTREAMCOMPONENT(VertexBuffer, FFEMFXMeshRenderVertex, BaryPosBaseId, VET_Float1);
        SetData(NewData);
    }

    /** Init function that can be called on any thread, and will do the right thing (enqueue command if called on main thread) */
    void Init(const FFEMFXMeshVertexBuffer* VertexBuffer)
    {
        if (IsInRenderingThread())
        {
            Init_RenderThread(VertexBuffer);
        }
        else
        {
            ENQUEUE_RENDER_COMMAND(InitFEMFXMeshVertexFactory)([this, VertexBuffer](FRHICommandListImmediate& RHICmdList)
            {
                this->Init_RenderThread(VertexBuffer);
            });
        }
    }

    /**
    * Should we cache the material's shadertype on this platform with this vertex factory?
    */
    static bool ShouldCompilePermutation(const FVertexFactoryShaderPermutationParameters& Parameters);

    static void ModifyCompilationEnvironment(const FVertexFactoryShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment);

    static void ValidateCompiledResult(const FVertexFactoryType* Type, EShaderPlatform Platform, const FShaderParameterMap& ParameterMap, TArray<FString>& OutErrors);

    /**
    * An implementation of the interface used by TSynchronizedResource to update the resource with new data from the game thread.
    */
    void SetData(const FDataType& InData);

    /**
    * Copy the data from another vertex factory
    * @param Other - factory to copy from
    */
    void Copy(const FFEMFXMeshVertexFactory& Other);

    // FRenderResource interface.
    virtual void InitRHI() override;
    virtual void ReleaseRHI() override
    {
        UniformBuffer.SafeRelease();
        FVertexFactory::ReleaseRHI();
    }

    static bool SupportsTessellationShaders() { return true; }

    static FVertexFactoryShaderParameters* ConstructShaderParameters(EShaderFrequency ShaderFrequency);

    void GetColorOverrideStream(const FVertexBuffer* ColorVertexBuffer, FVertexInputStreamArray& VertexStreams) const {
        checkf(ColorVertexBuffer->IsInitialized(), TEXT("Color Vertex buffer was not initialized! Name %s"), *ColorVertexBuffer->GetFriendlyName());
        checkf(IsInitialized() && EnumHasAnyFlags(EVertexStreamUsage::Overridden, Data.ColorComponent.VertexStreamUsage) && ColorStreamIndex > 0, TEXT("Per-mesh colors with bad stream setup! Name %s"), * ColorVertexBuffer->GetFriendlyName());

        VertexStreams.Add(FVertexInputStream(ColorStreamIndex, 0, ColorVertexBuffer->VertexBufferRHI));
    }

    inline FRHIShaderResourceView* GetPositionsSRV() const
    {
        return Data.PositionComponentSRV;
    }

    inline FRHIShaderResourceView* GetTangentsSRV() const
    {
        return Data.TangentsSRV;
    }

    inline FRHIShaderResourceView* GetTextureCoordinatesSRV() const
    {
        return Data.TextureCoordinatesSRV;
    }

    inline FRHIShaderResourceView* GetColorComponentsSRV() const
    {
        return Data.ColorComponentsSRV;
    }

    inline const uint32 GetColorIndexMask() const
    {
        return Data.ColorIndexMask;
    }

    inline const int GetLightMapCoordinateIndex() const
    {
        return Data.LightMapCoordinateIndex;
    }

    inline const int GetNumTexcoords() const
    {
        return Data.NumTexCoords;
    }

    FRHIUniformBuffer* GetUniformBuffer() const
    {
        return UniformBuffer.GetReference();
    }

protected:
    const FDataType& GetData() const { return Data; }

    FDataType Data;
    TUniformBufferRef<FFEMFXMeshVertexFactoryUniformShaderParameters> UniformBuffer;

    int32 ColorStreamIndex;
};

class FFEMFXMeshVertexFactoryShaderParameters : public FVertexFactoryShaderParameters
{
public:
    virtual void Bind(const FShaderParameterMap& ParameterMap);
    virtual void Serialize(FArchive& Ar);

	virtual void GetElementShaderBindings(
		const FSceneInterface* Scene, const FSceneView* View,
		const FMeshMaterialShader* Shader,
		const EVertexInputStreamType InputStreamType,
		ERHIFeatureLevel::Type FeatureLevel,
		const FVertexFactory* VertexFactory,
		const FMeshBatchElement& BatchElement,
		FMeshDrawSingleShaderBindings& ShaderBindings,
		FVertexInputStreamArray& VertexStreams) const;

    FFEMFXMeshVertexFactoryShaderParameters()
    {
    }

protected:
    FShaderResourceParameter TetMeshVertexPosBufferParameter;
    FShaderResourceParameter TetMeshVertexRotBufferParameter;
	FShaderResourceParameter TetMeshDeformationBufferParameter;
    FShaderResourceParameter TetVertexIdBufferParameter;
    FShaderResourceParameter BarycentricPosIdBufferParameter;
    FShaderResourceParameter BarycentricPosBufferParameter;
};

// User data for vertex shader.   Includes the structured buffer SRVs to support deformation of render mesh by tet mesh.
struct FFEMFXMeshBatchElementParams
{
    FFEMFXMeshBatchElementParams() {  }

    // Tet mesh vertex positions
    FShaderResourceViewRHIRef TetMeshVertexPosBufferSRV;

    // Tet mesh vertex rotations
    FShaderResourceViewRHIRef TetMeshVertexRotBufferSRV;

	// Tet mesh deformation values
	FShaderResourceViewRHIRef TetMeshDeformationBufferSRV;

    // Tet vertex indices
    FShaderResourceViewRHIRef TetVertexIdBufferSRV;

    // Ids for each render mesh vertex indexing into barycentric position buffer.
    FShaderResourceViewRHIRef BarycentricPosIdBufferSRV;

    // Buffer of tet barycentric coordinates.  May contain several coordinates for each vertex to support a change in tet assignments after fracture.
    FShaderResourceViewRHIRef BarycentricPosBufferSRV;
};

// Grouping of structured buffer resource and SRV with support for CPU updates (on render thread).
template<class Element>
class FStructuredBufferAndSRV
{
public:
    FStructuredBufferRHIRef StructuredBufferRHI;
    FShaderResourceViewRHIRef SRV;
    int32 NumElements;

    FStructuredBufferAndSRV() : NumElements(0) {}
    ~FStructuredBufferAndSRV() { Release(); }

    void Init_RenderThread(int32 InNumElements)
    {
        check(IsInRenderingThread());

        if (InNumElements == 0)
            return;

        StructuredBufferRHI.SafeRelease();
        SRV.SafeRelease();

        NumElements = InNumElements;
        int32 Size = sizeof(Element) * NumElements;

        FRHIResourceCreateInfo CreateInfo;
        StructuredBufferRHI = RHICreateStructuredBuffer(sizeof(Element), Size, BUF_Dynamic | BUF_ShaderResource, CreateInfo);
        SRV = RHICreateShaderResourceView(StructuredBufferRHI);
    }

    // Init RHI buffer with given number of elements.  If not called from rendering thread enqueues call to Init_RenderThread.
    void Init(int32 InNumElements)
    {
        if (IsInRenderingThread())
        {
            Init_RenderThread(InNumElements);
        }
        else
        {
            ENQUEUE_RENDER_COMMAND(InitCommand)([this, InNumElements](FRHICommandListImmediate& RHICmdList)
            {
                this->Init_RenderThread(InNumElements);
            });
        }
    }

    void Init_RenderThread(const TArray<Element>& SrcBuffer)
    {
        check(IsInRenderingThread());

        NumElements = SrcBuffer.Num();

        if (NumElements == 0)
        {
            return;
        }

        int32 Size = sizeof(Element) * NumElements;

        StructuredBufferRHI.SafeRelease();
        SRV.SafeRelease();

        FRHIResourceCreateInfo CreateInfo;
        StructuredBufferRHI = RHICreateStructuredBuffer(sizeof(Element), Size, BUF_Dynamic | BUF_ShaderResource, CreateInfo);
        void* DstBuffer = RHILockStructuredBuffer(StructuredBufferRHI, 0, Size, RLM_WriteOnly);
        FMemory::Memcpy(DstBuffer, SrcBuffer.GetData(), Size);
        RHIUnlockStructuredBuffer(StructuredBufferRHI);

        SRV = RHICreateShaderResourceView(StructuredBufferRHI);
    }

    // Init RHI buffer with array data.  If not called from rendering thread copies source data and enqueues call to Init_RenderThread.
    void Init(const TArray<Element>& SrcBuffer)
    {
        if (IsInRenderingThread())
        {
            Init_RenderThread(SrcBuffer);
        }
        else
        {
            TArray<Element>* CopyBuffer = new TArray<Element>();
            *CopyBuffer = SrcBuffer;

            ENQUEUE_RENDER_COMMAND(InitCommand)([this, CopyBuffer](FRHICommandListImmediate& RHICmdList)
            {
                this->Init_RenderThread(*CopyBuffer);
                delete CopyBuffer;
            });
        }
    }

    void Update_RenderThread(const TArray<Element>& SrcBuffer)
    {
        check(IsInRenderingThread());
        if (SrcBuffer.Num() > NumElements)
        {
            return;
        }

        int32 Size = sizeof(Element) * SrcBuffer.Num();
        void* DstBuffer = RHILockStructuredBuffer(StructuredBufferRHI, 0, Size, RLM_WriteOnly);
        FMemory::Memcpy(DstBuffer, SrcBuffer.GetData(), Size);
        RHIUnlockStructuredBuffer(StructuredBufferRHI);
    }

    // Update RHI buffer.  If not called from rendering thread copies source data and enqueues call to Update_RenderThread.
    void Update(const TArray<Element>& SrcBuffer)
    {
        if (IsInRenderingThread())
        {
            Update_RenderThread(SrcBuffer);
        }
        else
        {
            TArray<Element>* CopyBuffer = new TArray<Element>();
            *CopyBuffer = SrcBuffer;

            ENQUEUE_RENDER_COMMAND(UpdateCommand)([this, CopyBuffer](FRHICommandListImmediate& RHICmdList)
            {
                this->Update_RenderThread(*CopyBuffer);
                delete CopyBuffer;
            });
        }
    }

    void Release()
    {
        StructuredBufferRHI.SafeRelease();
        SRV.SafeRelease();
        NumElements = 0;
    }
};

// RHI resources for drawing a section of FEM mesh.
// Includes structured buffers for skinning mesh with FEM nodes.
class FFEMFXMeshProxySection
{
public:
	uint16 MaterialIndex;
	FFEMFXMeshVertexBuffer  VertexBuffer;
	FFEMFXMeshIndexBuffer   IndexBuffer;
	FFEMFXMeshVertexFactory VertexFactory;

	// Data split this way to minimize CPU updates on fracture
	FStructuredBufferAndSRV<int32> VertexBarycentricPosOffsets;                         // These ids can be updated to change tet assignments after fracture
	FStructuredBufferAndSRV<FFEMFXMeshBarycentricPos> VertexBarycentricPositions;   // Buffer can include pre and post fracture barycentric data

	bool bSectionVisible;

	//UMaterialInterface* OverrideMaterial;

	FFEMFXMeshProxySection(ERHIFeatureLevel::Type InFeatureLevel)
		: MaterialIndex(0)
		, VertexFactory(InFeatureLevel)
		, bSectionVisible(true)
		//, OverrideMaterial(nullptr)
	{}
};
