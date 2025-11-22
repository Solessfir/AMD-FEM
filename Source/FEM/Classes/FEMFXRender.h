// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "FEMMeshTypes.h"
#include "MeshMaterialShader.h"

struct FFEMFXMeshBatchElementParams;

/** Vertex Factory */
class FFEMFXMeshVertexFactory : public FVertexFactory
{
    DECLARE_VERTEX_FACTORY_TYPE(FFEMFXMeshVertexFactory);

    explicit FFEMFXMeshVertexFactory(ERHIFeatureLevel::Type InFeatureLevel)
        : FVertexFactory(InFeatureLevel)
    {
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

        /** The stream to read the shard vertex id from. */
        FVertexStreamComponent BaryPosOffsetIdComponent;

        /** The stream to read the barycentric position base id from. */
        FVertexStreamComponent BaryPosBaseIdComponent;
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
            ENQUEUE_RENDER_COMMAND(InitFEMFXMeshVertexFactory)([VertexFactory = this, VertexBuffer](FRHICommandListImmediate& RHICmdList)
           {
               VertexFactory->Init_RenderThread(VertexBuffer);
           });
        }
    }

    /**
    * Should we cache the material's shadertype on this platform with this vertex factory?
    */
    static bool ShouldCache(EShaderPlatform Platform, const class FMaterial* Material, const class FShaderType* ShaderType);

    static void ModifyCompilationEnvironment(const FVertexFactoryShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
    {
        OutEnvironment.SetDefine(TEXT("VF_SUPPORTS_SPEEDTREE_WIND"), TEXT("1"));
        OutEnvironment.SetDefine(TEXT("VF_SUPPORTS_PRIMITIVE_SCENE_DATA"), 1);
    }

    static bool ShouldCompilePermutation(const FVertexFactoryShaderPermutationParameters& Parameters)
    {
        return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
    }

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

    static bool SupportsTessellationShaders() { return true; }

    static FVertexFactoryShaderParameters* ConstructShaderParameters(EShaderFrequency ShaderFrequency);

    void SetColorOverrideStream(FRHICommandList& RHICmdList, const FVertexBuffer* ColorVertexBuffer) const
    {
        checkf(ColorVertexBuffer->IsInitialized(), TEXT("Color Vertex buffer was not initialized! Name %s"), *ColorVertexBuffer->GetFriendlyName());
        checkf(IsInitialized() && ColorStreamIndex > 0, TEXT("Per-mesh colors with bad stream setup! Name %s"), *ColorVertexBuffer->GetFriendlyName());
        RHICmdList.SetStreamSource(ColorStreamIndex, ColorVertexBuffer->VertexBufferRHI,  0);
    }

protected:
    const FDataType& GetData() const { return Data; }

    FDataType Data;

    int32 ColorStreamIndex = -1;
};

// User data for vertex shader.   Includes the structured buffer SRVs to support deformation of render mesh by tet mesh.
struct FFEMFXMeshBatchElementParams
{
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

class FFEMFXMeshVertexFactoryShaderParameters : public FVertexFactoryShaderParameters
{
    DECLARE_TYPE_LAYOUT(FFEMFXMeshVertexFactoryShaderParameters, NonVirtual);

    void Bind(const FShaderParameterMap& ParameterMap)
    {
        TetMeshVertexPosBufferParameter.Bind(ParameterMap, TEXT("TetMeshVertexPosBuffer"));
        TetMeshVertexRotBufferParameter.Bind(ParameterMap, TEXT("TetMeshVertexRotBuffer"));
        TetMeshDeformationBufferParameter.Bind(ParameterMap, TEXT("TetMeshDeformationBuffer"));
        TetVertexIdBufferParameter.Bind(ParameterMap, TEXT("TetVertexIdBuffer"));
        BarycentricPosIdBufferParameter.Bind(ParameterMap, TEXT("BarycentricPosIdBuffer"));
        BarycentricPosBufferParameter.Bind(ParameterMap, TEXT("BarycentricPosBuffer"));
    }

    void Serialize(FArchive& Ar)
    {
        Ar << TetMeshVertexPosBufferParameter
           << TetMeshVertexRotBufferParameter
           << TetMeshDeformationBufferParameter
           << TetVertexIdBufferParameter
           << BarycentricPosIdBufferParameter
           << BarycentricPosBufferParameter;
    }

    void GetElementShaderBindings(const FSceneInterface* Scene, const FSceneView* View, const FMeshMaterialShader* Shader,
        const EVertexInputStreamType InputStreamType, ERHIFeatureLevel::Type FeatureLevel, const FVertexFactory* VertexFactory,
        const FMeshBatchElement& BatchElement, FMeshDrawSingleShaderBindings& ShaderBindings, FVertexInputStreamArray& VertexStreams) const
    {
        // If the batch carries a color vertex buffer override, the old code used
        // VertexFactory->SetColorOverrideStream(RHICmdList, OverrideColorVertexBuffer).
        // In the new API you should add a vertex stream into VertexStreams so the
        // vertex factory will use it when setting up the draw. Implementing that
        // depends on your vertex-factory implementation (helpers / overloads).
        //
        // Many vertex factories expose helper methods to append vertex streams
        // (or you can construct FVertexStreamComponent and add it to VertexStreams).
        //
        // If you have a helper on your vertex factory to set the color override stream
        // for the new API, call it here. Example (pseudo; keep or adapt to your VF):
        //
        // if (BatchElement.bUserDataIsColorVertexBuffer)
        // {
        //     FColorVertexBuffer* OverrideColorVertexBuffer = (FColorVertexBuffer*)BatchElement.UserData;
        //     check(OverrideColorVertexBuffer);
        //     static_cast<const FFEMFXMeshVertexFactory*>(VertexFactory)->AddColorOverrideVertexStream(VertexStreams, OverrideColorVertexBuffer);
        // }
        //
        // If you don't have such helper, omit the override handling here or implement
        // construction of FVertexStreamComponent entries and append them to VertexStreams.

        // Bind SRV parameters the same way we used to set SRVs in SetMesh.
        // FMeshDrawSingleShaderBindings::Add supports adding FRHIShaderResourceView*.
        const FFEMFXMeshBatchElementParams* BatchElementParams = nullptr;
        if (!BatchElement.bUserDataIsColorVertexBuffer)
        {
            BatchElementParams = static_cast<const FFEMFXMeshBatchElementParams*>(BatchElement.UserData);
        }

        // Guard: if there is no user-data or user-data is color override, nothing to bind here.
        if (!BatchElementParams)
        {
            return;
        }

        const FShaderResourceViewRHIRef TetMeshVertexPosBufferSRV = BatchElementParams->TetMeshVertexPosBufferSRV;
        const FShaderResourceViewRHIRef TetMeshVertexRotBufferSRV = BatchElementParams->TetMeshVertexRotBufferSRV;
        const FShaderResourceViewRHIRef TetMeshDeformationBufferSRV = BatchElementParams->TetMeshDeformationBufferSRV;
        const FShaderResourceViewRHIRef TetVertexIdBufferSRV = BatchElementParams->TetVertexIdBufferSRV;
        const FShaderResourceViewRHIRef BarycentricPosIdBufferSRV = BatchElementParams->BarycentricPosIdBufferSRV;
        const FShaderResourceViewRHIRef BarycentricPosBufferSRV = BatchElementParams->BarycentricPosBufferSRV;

        // Only add bindings for parameters that were bound at compile time and whose SRV is valid.
        if (TetMeshVertexPosBufferParameter.IsBound() && TetMeshVertexPosBufferSRV)
        {
            ShaderBindings.Add(TetMeshVertexPosBufferParameter, TetMeshVertexPosBufferSRV.GetReference());
        }

        if (TetMeshVertexRotBufferParameter.IsBound() && TetMeshVertexRotBufferSRV)
        {
            ShaderBindings.Add(TetMeshVertexRotBufferParameter, TetMeshVertexRotBufferSRV.GetReference());
        }

        if (TetMeshDeformationBufferParameter.IsBound() && TetMeshDeformationBufferSRV)
        {
            ShaderBindings.Add(TetMeshDeformationBufferParameter, TetMeshDeformationBufferSRV.GetReference());
        }

        if (TetVertexIdBufferParameter.IsBound() && TetVertexIdBufferSRV)
        {
            ShaderBindings.Add(TetVertexIdBufferParameter, TetVertexIdBufferSRV.GetReference());
        }

        if (BarycentricPosIdBufferParameter.IsBound() && BarycentricPosIdBufferSRV)
        {
            ShaderBindings.Add(BarycentricPosIdBufferParameter, BarycentricPosIdBufferSRV.GetReference());
        }

        if (BarycentricPosBufferParameter.IsBound() && BarycentricPosBufferSRV)
        {
            ShaderBindings.Add(BarycentricPosBufferParameter, BarycentricPosBufferSRV.GetReference());
        }
    }

    // Keep size field for layout compatibility
    LAYOUT_FIELD_INITIALIZED(uint32, Size_DEPRECATED, 0u);

protected:
    LAYOUT_FIELD(FShaderResourceParameter, TetMeshVertexPosBufferParameter);
    LAYOUT_FIELD(FShaderResourceParameter, TetMeshVertexRotBufferParameter);
    LAYOUT_FIELD(FShaderResourceParameter, TetMeshDeformationBufferParameter);
    LAYOUT_FIELD(FShaderResourceParameter, TetVertexIdBufferParameter);
    LAYOUT_FIELD(FShaderResourceParameter, BarycentricPosIdBufferParameter);
    LAYOUT_FIELD(FShaderResourceParameter, BarycentricPosBufferParameter);
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

        FRHIResourceCreateInfo CreateInfo(TEXT("FStructuredBufferAndSRV"));
        StructuredBufferRHI = RHICreateStructuredBuffer(sizeof(Element), Size, BUF_Dynamic | BUF_ShaderResource, CreateInfo);
        SRV = RHICreateShaderResourceView(StructuredBufferRHI);
    }

    void Init(int32 InNumElements)
    {
        if (IsInRenderingThread())
        {
            Init_RenderThread(InNumElements);
        }
        else
        {
            FStructuredBufferAndSRV<Element>* Buffer = this;
            int32 LocalNumElements = InNumElements;

            ENQUEUE_RENDER_COMMAND(InitCommand)([Buffer, LocalNumElements](FRHICommandListImmediate& RHICmdList)
            {
                Buffer->Init_RenderThread(LocalNumElements);
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

        FRHIResourceCreateInfo CreateInfo(TEXT("FStructuredBufferAndSRV"));
        StructuredBufferRHI = RHICreateStructuredBuffer(sizeof(Element), Size, BUF_Dynamic | BUF_ShaderResource, CreateInfo);
        void* DstBuffer = RHILockStructuredBuffer(StructuredBufferRHI, 0, Size, RLM_WriteOnly);
        FMemory::Memcpy(DstBuffer, SrcBuffer.GetData(), Size);
        RHIUnlockStructuredBuffer(StructuredBufferRHI);

        SRV = RHICreateShaderResourceView(StructuredBufferRHI);
    }

    void Init(const TArray<Element>& SrcBuffer)
    {
        if (IsInRenderingThread())
        {
            Init_RenderThread(SrcBuffer);
        }
        else
        {
            TArray<Element>* CopyBuffer = new TArray<Element>(SrcBuffer);

            FStructuredBufferAndSRV<Element>* Buffer = this;

            ENQUEUE_RENDER_COMMAND(InitCommand_Array)([Buffer, CopyBuffer](FRHICommandListImmediate& RHICmdList)
            {
                Buffer->Init_RenderThread(*CopyBuffer);
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

    void Update(const TArray<Element>& SrcBuffer)
    {
        if (IsInRenderingThread())
        {
            Update_RenderThread(SrcBuffer);
        }
        else
        {
            TArray<Element>* CopyBuffer = new TArray<Element>(SrcBuffer);

            FStructuredBufferAndSRV<Element>* Buffer = this;

            ENQUEUE_RENDER_COMMAND(UpdateCommand_Array)([Buffer, CopyBuffer](FRHICommandListImmediate& RHICmdList)
            {
                Buffer->Update_RenderThread(*CopyBuffer);
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
    bool bSectionVisible = true;
	uint16 MaterialIndex = 0;
	FFEMFXMeshVertexBuffer VertexBuffer;
	FFEMFXMeshIndexBuffer IndexBuffer;
	FFEMFXMeshVertexFactory VertexFactory = FFEMFXMeshVertexFactory(ERHIFeatureLevel::SM5);

	// Data split this way to minimize CPU updates on fracture

    // These ids can be updated to change tet assignments after fracture
	FStructuredBufferAndSRV<int32> VertexBarycentricPosOffsets;

    // Buffer can include pre and post fracture barycentric data
	FStructuredBufferAndSRV<FFEMFXMeshBarycentricPos> VertexBarycentricPositions;
};
