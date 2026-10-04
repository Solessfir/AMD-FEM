# FEMFX Linux dependencies

The vendored implementation is from [GPUOpen-Effects/FEMFX](https://github.com/GPUOpen-Effects/FEMFX/tree/95cf656731a2465ae1d400138170af2d6575b5bb), revision `95cf656731a2465ae1d400138170af2d6575b5bb` (AMD FEMFX 0.1.0). The existing plugin API headers match that revision, apart from the existing Unreal SSE math integration and whitespace. `LICENSE.txt` and `NOTICES.txt` retain the upstream license notices.

Added upstream files are the 40 implementation `.cpp` files and their internal headers under `amd_femfx/src`, `amd_femfx/inc/Vectormath/sse_mathfun.h`, and the rendering sample and task interface under `samples`. `SampleTaskSystemLinux.cpp` implements that task interface with the C++ standard library. The rendering sample is used only by the smoke test; the Unreal module already contains its own adapted rendering implementation.

Linux changes provide alignment and thread-local macros, SSE2 register access and unsigned comparisons, platform atomics and bit counts, portable file opening, correct include case, and standard C++ template declarations. Windows libraries and Windows SIMD configuration are unchanged. Public `FmVector3` remains the 12-byte scalar type; Linux internal batches use four SSE lanes without AVX/FMA requirements.

## Build

Use Bash, Clang, `ar`, and an Unreal Engine 4.27 checkout with its bundled Linux libc++ headers and archives. No CMake or package installation is needed. Use the same compiler and sysroot as the Unreal Linux build; UE4.27 normally uses the v19 Clang 11.0.1 CentOS 7 toolchain.

```bash
UE_ROOT=/path/to/UnrealEngine \
CXX=/path/to/v19_clang-11.0.1-centos7/x86_64-unknown-linux-gnu/bin/clang++ \
SYSROOT=/path/to/v19_clang-11.0.1-centos7/x86_64-unknown-linux-gnu \
bash build_linux.sh --smoke
```

When validating from WSL with the Windows cross toolchain's sysroot, use the installed WSL `clang++` as `CXX`; a Windows `.exe` compiler cannot consume Linux paths. Use `/mnt/c/...` paths for `UE_ROOT` and `SYSROOT`.

Outputs:

```text
amd_femfx/lib/Linux/x86_64-unknown-linux-gnu/libAMD_FEMFX.a
amd_femfx/lib/Linux/x86_64-unknown-linux-gnu/libsample_task_system.a
```

The archives use C++14, position-independent code, Unreal's libc++ ABI, disabled exceptions/RTTI, and SSE2. The application must link `pthread` and supply the existing global `FmAlignedMalloc`/`FmAlignedFree` callbacks. Those callbacks intentionally remain unresolved in the archive. Do not link a second C++ standard library into Unreal.

Unreal modules define `FEMFX_USE_UNREAL_MATH=1` to reuse the engine's SSE constants without redeclaring them. Standalone vendor builds use the bundled upstream SSE header; both paths use the same FEMFX math implementations.

`SYSROOT` is optional for local smoke checks; use the engine toolchain sysroot for distributable builds so the objects target the engine's glibc baseline. `JOBS` controls parallel compilation (default 4). `BUILD_DIR` selects the object/test directory; otherwise the script creates a temporary directory under `/var/tmp`. Only the final archives are written into this source tree.

The smoke test links every core object and uses the matching Unreal libc++ archives. It checks worker indices, multiple task-system owners, nested task submission, synchronization events, render tetrahedron assignments, and tetrahedron deformation under gravity with a fixed vertex using both one and four workers. This validates the vendor libraries independently; Unreal editor/runtime verification remains separate.

The Linux task system is shared by active scenes: each `SampleInitTaskSystem` must have a matching `SampleDestroyTaskSystem`. The first owner selects the worker count, and subsequent owners reuse that count until the final owner releases the pool. Nonworker threads return index `-1`, matching the upstream API; only worker threads execute queued simulation tasks. Scene teardown must wait for its simulation before releasing its task-system ownership.
