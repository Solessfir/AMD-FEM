// Copyright 1998-2017 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DynamicMeshBuilder.h"
#include "FEMMeshTypes.h"
#include "Materials/Material.h"
#include "LocalVertexFactory.h"



/** Vertex Factory */
class FFEMFXMeshVertexFactory : public FVertexFactory
{
    DECLARE_VERTEX_FACTORY_TYPE(FFEMFXMeshVertexFactory);

public:

    FFEMFXMeshVertexFactory()
        : ColorStreamIndex(-1)
    {}

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
            ENQUEUE_UNIQUE_RENDER_COMMAND_TWOPARAMETER(
                InitFEMFXMeshVertexFactory,
                FFEMFXMeshVertexFactory*, VertexFactory, this,
                const FFEMFXMeshVertexBuffer*, VertexBuffer, VertexBuffer,
                {
                    VertexFactory->Init_RenderThread(VertexBuffer);
                });
        }
    }

    /**
    * Should we cache the material's shadertype on this platform with this vertex factory?
    */
    static bool ShouldCache(EShaderPlatform Platform, const class FMaterial* Material, const class FShaderType* ShaderType);

    static void ModifyCompilationEnvironment(EShaderPlatform Platform, const FMaterial* Material, FShaderCompilerEnvironment& OutEnvironment)
    {
        OutEnvironment.SetDefine(TEXT("VF_SUPPORTS_SPEEDTREE_WIND"), TEXT("1"));
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

    FORCEINLINE_DEBUGGABLE void SetColorOverrideStream(FRHICommandList& RHICmdList, const FVertexBuffer* ColorVertexBuffer) const
    {
        checkf(ColorVertexBuffer->IsInitialized(), TEXT("Color Vertex buffer was not initialized! Name %s"), *ColorVertexBuffer->GetFriendlyName());
        checkf(IsInitialized() && Data.ColorComponent.bSetByVertexFactoryInSetMesh && ColorStreamIndex > 0, TEXT("Per-mesh colors with bad stream setup! Name %s"), *ColorVertexBuffer->GetFriendlyName());
        RHICmdList.SetStreamSource(ColorStreamIndex, ColorVertexBuffer->VertexBufferRHI,  0);
    }

protected:
    FDataType Data;
    int32 ColorStreamIndex;

    const FDataType& GetData() const { return Data; }
};

class FFEMFXMeshVertexFactoryShaderParameters : public FVertexFactoryShaderParameters
{
public:
    virtual void Bind(const FShaderParameterMap& ParameterMap) override;
    virtual void Serialize(FArchive& Ar)
    {
        Ar << TetMeshVertexPosBufferParameter
            << TetMeshVertexRotBufferParameter
			<< TetMeshDeformationBufferParameter
            << TetVertexIdBufferParameter
            << BarycentricPosIdBufferParameter
            << BarycentricPosBufferParameter;
    }
    virtual void SetMesh(FRHICommandList& RHICmdList, FShader* Shader, const FVertexFactory* VertexFactory, const FSceneView& View, const FMeshBatchElement& BatchElement, uint32 DataFlags) const override;

    virtual uint32 GetSize() const
    {
        return sizeof(*this);
    }

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
            ENQUEUE_UNIQUE_RENDER_COMMAND_TWOPARAMETER(
                InitCommand,
                FStructuredBufferAndSRV*, Buffer, this,
                int32, InNumElements, InNumElements,
                {
                    Buffer->Init_RenderThread(InNumElements);
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

            ENQUEUE_UNIQUE_RENDER_COMMAND_TWOPARAMETER(
                InitCommand,
                FStructuredBufferAndSRV*, Buffer, this,
                TArray<Element>*, CopyBuffer, CopyBuffer,
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

            ENQUEUE_UNIQUE_RENDER_COMMAND_TWOPARAMETER(
            UpdateCommand,
            FStructuredBufferAndSRV*, Buffer, this,
            TArray<Element>*, CopyBuffer, CopyBuffer,
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
	uint16 MaterialIndex;
	FFEMFXMeshVertexBuffer  VertexBuffer;
	FFEMFXMeshIndexBuffer   IndexBuffer;
	FFEMFXMeshVertexFactory VertexFactory;

	// Data split this way to minimize CPU updates on fracture
	FStructuredBufferAndSRV<int32> VertexBarycentricPosOffsets;                         // These ids can be updated to change tet assignments after fracture
	FStructuredBufferAndSRV<FFEMFXMeshBarycentricPos> VertexBarycentricPositions;   // Buffer can include pre and post fracture barycentric data

	bool bSectionVisible;

	//UMaterialInterface* OverrideMaterial;

	FFEMFXMeshProxySection()
		: MaterialIndex(0)
		, bSectionVisible(true)
		//, OverrideMaterial(NULL)
	{}
};