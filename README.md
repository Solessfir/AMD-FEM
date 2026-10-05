# AMD FEMFX Plugin

An Unreal Engine plugin based on AMD's [FEMFX](https://github.com/GPUOpen-Effects/FEMFX) CPU finite element library for deformable and breakable objects.

**Supports Unreal Engine 4.27 only, on Windows 64-bit and Linux x86_64.** Linux libraries and plugin source compilation are validated; Linux editor and Vulkan rendering still need runtime verification.

## Installation

Download this repository for a source installation, or a compatible UE 4.27 package for your platform from [Releases](https://github.com/Solessfir/AMD-FEM/releases), when available.

1. Close the editor and put the plugin in `<Project>/Plugins/AMD-FEM`, with `FEM.uplugin` directly inside that folder.
2. Open your UE 4.27 project. **Finite Element Material** is enabled by default. For a source installation, allow Unreal to compile the plugin when prompted.

Source installations require a C++ project. If your project is Blueprint-only, add a C++ class before installing the source plugin. A compatible binary release requires no compilation or C++ class.

Linux source installations include the FEMFX static libraries built against Unreal's libc++ and the UE 4.27 CentOS 7 sysroot. To rebuild those dependencies, see [Linux library build instructions](ThirdParty/FEMLib/FEMFXBeta/README-Linux.md). Windows builds continue to use the original Windows libraries. WSL can build and test the Linux FEMFX dependencies, but running the plugin requires a Linux UE 4.27 editor or game build.

## Supported features

- **Elastic deformation:** tetrahedral meshes bend, stretch, and compress, with configurable density, stiffness, and Poisson's ratio.
- **Plastic deformation:** permanent shape changes controlled by yield threshold, creep, and deformation limits.
- **Fracture:** splitting along tetrahedral faces, with render mesh updates and Blueprint fracture events.
- **Procedural meshes:** generate tetrahedral grids with configurable cell counts, dimensions, scale, and randomized vertices.
- **Asset import:** import FEM format 1.0 files containing tetrahedral meshes, material assignments, tags, render mesh data or FBX references, rigid bodies, and constraints.
- **Simulation controls:** kinematic tetrahedra, removable anchors, collision groups, sleeping, and multiple named FEM scenes.
- **Collision and constraints:** FEMFX mesh and rigid-body collisions, scene collision planes, glue constraints, plane constraints, and rigid-body angular constraints.
- **Blueprint integration:** collision and fracture events, tetrahedron queries, material changes, explosion forces, and resetting meshes to their rest pose.
- **Rendering:** Unreal materials applied to render meshes driven by the simulated tetrahedra.
- **CPU simulation:** configurable worker threads and scene capacities.

## Quick start

This example creates a cube that falls onto an FEM collision plane in a new UE 4.27 project.

1. Install the plugin, then open a level.
2. Place a **FEMFXScene** actor in the level. Set its `Name` property to `Default` and leave `bAllowTick` enabled. Set `minPlaneConstraint` to `(-10, 0, -10)` and `maxPlaneConstraint` to `(10, 10, 10)`. These bounds use FEMFX coordinates in meters, with **Y up**; the minimum Y plane provides a floor at Unreal Z = 0.
3. Place one **PreProcessedMeshHelper** actor in the level to cache mesh preprocessing.
4. In the Content Browser, create a **FEM Mesh** asset. In **FEM Options**, enable `Procedural Generate`, set all three cube counts to `2`, all three cube dimensions to `0.5`, and `Scale` to `1`. Leave `Randomize` off and click **Import**. This generates a cube measuring 100 cm on each side, with interior vertices for deformation and fracture.
5. Create a **Tet Mesh Parameters** asset and keep its default values.
6. Create a Blueprint with **FEMActor** as its parent class, then add a **FEMFXMeshComponent**. Configure the component:
   - Set `Name` to `TestCube` and assign the generated asset to `FEMMesh`.
   - Add a regular opaque Unreal material to `RenderMaterials` at index `0`.
   - Assign the Tet Mesh Parameters asset to the `Default` entry in `MeshParameters`.
   - Enable `AddToSimulation`; disable `Kinematic`, `PlasticityEnabled`, and `FractureEnabled` for this first test.
7. Place the Blueprint at approximately `(0, 0, 300)` in Unreal coordinates. Frame the cube in the viewport and choose **Simulate**. It should render, fall, and collide with the FEM scene's floor plane.

A normal Unreal floor mesh does not automatically become an FEMFX collider. Use the scene's collision planes for this example. `FEMActor` initializes its FEM components and finds the scene named `Default`; adding a FEM component to an ordinary Actor does not perform that setup automatically.

### Deformation and fracture

Reduce `youngsModulus` in the Tet Mesh Parameters asset to make the cube softer. Enable `PlasticityEnabled` and configure `plasticYieldThreshold`, `plasticCreep`, `plasticMin`, and `plasticMax` for permanent deformation.

For breakable objects, enable `FractureEnabled` and configure `fractureStressThreshold`. Fracture requires sufficient stress; enabling the flag alone does not break a mesh. Use collisions or `ApplyExplosionForce` to apply a load, and bind `FractureEvent` for gameplay reactions. The subdivided cube above provides interior vertices for fracture planes.

For several independent mesh assets, use distinct component `Name` values. Components sharing the same mesh can share a name and its preprocessing cache. Additional FEM scenes can be selected through the actor's `bOverride_FEMScene` and `SceneName` properties.

## Importing assets

A `.fem` file is a JSON asset file used by FEMFX, not an application. FEM format 1.0 describes tetrahedral simulation data and can reference FBX render meshes, physical materials, tags, rigid bodies, and constraints. Import it through the Content Browser. An FBX mesh alone does not supply the tetrahedral volume needed for simulation.

### Where to get `.fem` files

AMD's authoring workflow uses **Houdini**, a separate 3D application, with their FEMFX asset tools:

- [AMD FEMFX Houdini assets](https://github.com/GPUOpen-Effects/FEMFX/blob/master/houdini16.5/hda/AMD_FEM_Assets.otl), supplied for Houdini 16.5.
- [Barrel creation walkthrough](https://github.com/GPUOpen-Effects/FEMFX/blob/master/docs/FEM-my_first_barrel_walkthrough.pdf).
- [FEM_SimpleSquare.fem example](https://github.com/GPUOpen-Effects/FEMFX/blob/master/samples/FEMFXViewer/FEMFiles/FEM_SimpleSquare.fem) from AMD's viewer samples. Import this file directly; the importer accepts both the upstream `fbxFiles` spelling and the older Unreal `FbxFiles` spelling.

For the quick start above, no `.fem` file or Houdini installation is needed. The **FEM Mesh** creation dialog generates a tetrahedral grid directly in Unreal.

### Import validation

File reads, JSON structure, versions, array sizes, and referenced indices are validated before creating assets. A failed FBX import removes the temporary actor but retains any assets generated earlier in that import.

Procedural generation requires positive cell counts and finite, positive dimensions and scale. A generated grid is limited to 4096 vertices: `(NumCubesX + 1) * (NumCubesY + 1) * (NumCubesZ + 1) <= 4096`.

## Tests

With no active play session, open **Window > Developer Tools > Session Frontend > Automation**, select the **FEM** tests, and run them. Alternatively, enter this command in the editor's Output Log:

```text
Automation RunTests FEM.
```

The suite checks procedural creation, settings persistence, FEM import field compatibility and malformed files, GPU lighting and simulated render positions against native Unreal meshes, editor mesh changes, elastic deformation, fracture, and repeated PIE teardown. Rendering tests require a real graphics RHI. To test AMD's example import, launch the editor with `-FEMImportFixture="<path-to-FEM_SimpleSquare.fem>"` and run the FEM tests. Referenced FBX imports are not yet covered by automation.

## References

- [AMD FEMFX library](https://github.com/GPUOpen-Effects/FEMFX)
- [Original AMD Unreal Engine plugin](https://github.com/GPUOpenSoftware/UnrealEngine/tree/FEMFX-4.18)
- [Original Alien Pods example project](https://github.com/GPUOpenSoftware/UnrealEngine/tree/FEMFX-AlienPods)
