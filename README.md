# This Plugin doesn't work yet!

Plugin will hang on `45%` Editor loading after trying to compile
shaders.

Breakpoint will be triggered on:
`void FShaderCompilingManager::ProcessCompiledShaderMaps::3458` with
`"Failed to compile default material!"` message.

Actual error can be found inside the log file, located in:
`...Project/Saved/Logs/*.log`

``` ini
/Plugin/FEM/Private/FEMFXMeshVertexFactory.ush(381,21-29):  error X3004: undeclared identifier 'Primitive'
```

Which points to:

``` glsl
half3x3 CalcTangentToWorldNoScale(in half3x3 TangentToLocal)
{
    half3x3 LocalToWorld = GetLocalToWorld3x3();
    half3 InvScale = Primitive.InvNonUniformScale.xyz; // ERROR here `Primitive`
    LocalToWorld[0] *= InvScale.x;
    LocalToWorld[1] *= InvScale.y;
    LocalToWorld[2] *= InvScale.z;
    return mul(TangentToLocal, LocalToWorld);
}
```

I've tried adding `#include "/Engine/Private/SceneData.ush"` to the `FEMFXMeshVertexFactory.ush`, because it defines `Primitive`

``` glsl
half3x3 CalcTangentToWorldNoScale(in half3x3 TangentToLocal)
{
    half3x3 LocalToWorld = GetLocalToWorld3x3();
    float3 InvNonUniformScale = GetPrimitiveData(Parameters).InvNonUniformScale; // Maybe now it will work?
    LocalToWorld[0] *= InvNonUniformScale.x;
    LocalToWorld[1] *= InvNonUniformScale.y;
    LocalToWorld[2] *= InvNonUniformScale.z;
    return mul(TangentToLocal, (half3x3)LocalToWorld);
}
```

But it will define it as `#define GetPrimitiveData(x) Primitive` which means the same exact error.

Adding `OutEnvironment.SetDefine(TEXT("VF_SUPPORTS_PRIMITIVE_SCENE_DATA"), 1);` to the `ModifyCompilationEnvironment` didn't help either.

### References:

[Original repo (UE4.18)](https://github.com/GPUOpenSoftware/UnrealEngine/tree/FEMFX-4.18)

[Archived implementation of this plugin for UE5.3](https://github.com/matiasgql/FEMFX-UE5)

[Youtube: Integrating an FEM Physics System into Unreal Engine](https://youtu.be/IYClvszCCPA?si=kuNgz-jNWwvvt7UI)

---

# AMD FEMFX plugin

*Disclaimer:* This is not a full integration of FEMFX into the Unreal Physics system.

*AMD FEMFX plugin* is an Unreal Engine 4 is a UE4 game plugin based on the AMD FEMFX multi-threaded CPU library. The FEMFX library utilizes the finite element method to realistically simulate deformation of objects with different material properties.
For more information about the FEMFX library, please visit: [FEMFX Page](https://github.com/GPUOpen-Effects/FEMFX)


### Prerequisites
* Unreal Engine version 4.18
* Visual Studio&reg; 2017.
* AMD Ryzen™ 7 2700X Processor or equivalent


### Getting Started
* Make sure the Unreal 4.18 source code is already on your system
* Download the plugin and double click the project file to build the plugin and the dependency modules
	
	
The editor built based on the above instructions will have the FEM plugin enabled. For more information on creating and importing models into Unreal for real-time FEM simulation, please refer to [FEMFX Page](https://github.com/GPUOpen-Effects/FEMFX)

### Building the test content with FEM plugin

* Download the content 
* Copy the "Plugins" folder in the same directory as your game project 
* Double click on the project file to build the game with FEM effects