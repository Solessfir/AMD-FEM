#!/usr/bin/env bash
set -euo pipefail

root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
: "${UE_ROOT:?Set UE_ROOT to the Unreal Engine 4.27 checkout or installation}"
libcxx="$UE_ROOT/Engine/Source/ThirdParty/Linux/LibCxx"
arch=x86_64-unknown-linux-gnu
[[ "$(uname -m)" == x86_64 ]] || { echo "Only Linux x86_64 is supported" >&2; exit 1; }
[[ -f "$libcxx/include/c++/v1/vector" ]] || { echo "Missing Unreal libc++ headers: $libcxx" >&2; exit 1; }
[[ -f "$libcxx/lib/Linux/$arch/libc++.a" ]] || { echo "Missing Unreal libc++ archives: $libcxx" >&2; exit 1; }

cxx="${CXX:-clang++}"
ar="${AR:-ar}"
build_dir="${BUILD_DIR:-$(mktemp -d -p "${TMPDIR:-/var/tmp}" femfx-build.XXXXXX)}"
output="$root/amd_femfx/lib/Linux/$arch"
mkdir -p "$build_dir/core" "$build_dir/tasks" "$output"
flags=(-std=c++14 -O2 -DNDEBUG -fPIC -msse2 -mno-avx -mno-fma -pthread
       -fno-exceptions -fno-rtti -nostdinc++ --target="$arch"
       -isystem "$libcxx/include/c++/v1" -I "$root/amd_femfx/inc" -I "$root/amd_femfx/inc/Vectormath")
if [[ -n "${SYSROOT:-}" ]]; then
    flags+=(--sysroot="$SYSROOT")
fi
for directory in "$root"/amd_femfx/src/*; do
    flags+=(-I "$directory")
done

jobs="${JOBS:-4}"
pids=()
objects=()
for source in "$root"/amd_femfx/src/*/*.cpp; do
    object="$build_dir/core/$(basename "${source%.cpp}").o"
    objects+=("$object")
    "$cxx" "${flags[@]}" -c "$source" -o "$object" &
    pids+=("$!")
    if (( ${#pids[@]} >= jobs )); then
        for pid in "${pids[@]}"; do wait "$pid"; done
        pids=()
    fi
done
for pid in "${pids[@]}"; do wait "$pid"; done
"$cxx" "${flags[@]}" -c "$root/samples/sample_task_system/SampleTaskSystemLinux.cpp" -o "$build_dir/tasks/SampleTaskSystemLinux.o"
rm -f "$build_dir/libAMD_FEMFX.a" "$build_dir/libsample_task_system.a"
"$ar" rcs "$build_dir/libAMD_FEMFX.a" "${objects[@]}"
"$ar" rcs "$build_dir/libsample_task_system.a" "$build_dir/tasks/SampleTaskSystemLinux.o"
mv "$build_dir/libAMD_FEMFX.a" "$build_dir/libsample_task_system.a" "$output/"
echo "Built FEMFX libraries: $output"
echo "Compiler: $("$cxx" --version | head -n 1)"
echo "Build objects: $build_dir"

if [[ "${1:-}" == --smoke ]]; then
    "$cxx" "${flags[@]}" -I "$root/samples/common" -I "$root/samples/sample_task_system" \
        "$root/tests/LinuxSmoke.cpp" "$root/samples/common/RenderTetAssignment.cpp" \
        -Wl,--whole-archive "$output/libAMD_FEMFX.a" \
        "$output/libsample_task_system.a" -Wl,--no-whole-archive -nostdlib++ \
        "$libcxx/lib/Linux/$arch/libc++.a" "$libcxx/lib/Linux/$arch/libc++abi.a" \
        -lm -ldl -pthread -o "$build_dir/LinuxSmoke"
    "$build_dir/LinuxSmoke"
fi
