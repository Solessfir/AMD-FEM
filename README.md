# AMD FEMFX Plugin

**AMD Finite Element Material** is an Unreal Engine plugin based on the `AMD FEMFX` multi-threaded CPU library. The FEMFX library utilizes the finite element method to realistically simulate deformation of objects with different material properties.
For more information about the FEMFX library, please visit: [FEMFX Page](https://github.com/GPUOpen-Effects/FEMFX)


## Known Issues

**Crash on Component Usage (Uniform Buffer Mismatch)**

When adding `UFEMMesh` to `UFEMFXMeshComponent` or opening a Blueprint Viewport containing assigned `UFEMTetMesh`, you will encounter the following critical assertion failure, causing the editor to crash:

```cpp
Assertion failed: Buffer->GetLayout().GetHash() == Shader->ShaderResourceTable.ResourceTableLayoutHashes[BufferIndex] [File:C:/Git/UE5-Vite/Engine/Source/Runtime/D3D12RHI/Private/D3D12Commands.cpp] [Line: 1477] 

// Assertion located void FD3D12CommandContext::SetResourcesFromTables::1477
FD3D12UniformBuffer* Buffer = BoundUniformBuffers[ShaderType::StaticFrequency][BufferIndex];
check(Buffer);
check(BufferIndex < Shader->ShaderResourceTable.ResourceTableLayoutHashes.Num());
```

Error indicates a **Uniform Buffer Layout Mismatch**. It occurs because the memory layout of the C++ Uniform Buffer struct (`FFEMFXMeshVertexFactoryUniformShaderParameters`) does not match the expected layout of the compiled HLSL Shader. This is strictly enforced in newer versions of Unreal Engine (4.27+) and DX12. It typically requires manual padding of the C++ struct to align with HLSL 16-byte registers and a matching manual definition in the `.ush` shader file.

### References:
[FEMFX](https://github.com/GPUOpen-Effects/FEMFX)  
[FEMFX Plugin (UE 4.18)](https://github.com/GPUOpenSoftware/UnrealEngine/tree/FEMFX-4.18)  
[Example Project (UE 4.18)](https://github.com/GPUOpenSoftware/UnrealEngine/tree/FEMFX-AlienPods)

[Project Borealis FEMFX Plugin (UE 4.24)](https://github.com/ProjectBorealisTeam/UnrealEngine/tree/FEMFX-4.24)  
[Archived FEMFX Plugin (UE 5.3)](https://github.com/matiasgql/FEMFX-UE5)  
[Youtube: Integrating an FEM Physics System into Unreal Engine](https://youtu.be/IYClvszCCPA?si=kuNgz-jNWwvvt7UI)
