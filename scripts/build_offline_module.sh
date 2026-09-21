#!/usr/bin/env bash
set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
build=${BUILD_DIR:-"$root/dist/module-bundle"}
jobs=${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || printf '1')}

for source in \
    vendor/_deps/{absl,args,calf,capio_cl,jsoncons,protobuf,tomlplusplus}-src \
    vendor/{syscall_intercept,capstone}; do
    [[ -d "$root/$source" ]] || { printf 'error: vendored source is missing: %s\n' "$source" >&2; exit 1; }
done

cmake -S "$root" -B "$build" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCAPIO_BUILD_TESTS=OFF \
    -DCALF_TESTS=OFF \
    -DCALF_PYTHON_TESTS=OFF \
    -DCALF_BUILD_PYTHON_BINDINGS=OFF \
    -DCALF_PROTOBUF_FORCE_FETCH=ON \
    -Dprotobuf_BUILD_TESTS=OFF \
    -Dprotobuf_FORCE_FETCH_DEPENDENCIES=ON \
    -DABSL_BUILD_TESTING=OFF \
    -DFETCHCONTENT_FULLY_DISCONNECTED=ON \
    -DFETCHCONTENT_SOURCE_DIR_PROTOBUF="$root/vendor/_deps/protobuf-src" \
    -DFETCHCONTENT_SOURCE_DIR_ABSL="$root/vendor/_deps/absl-src" \
    -DCMAKE_BUILD_WITH_INSTALL_RPATH=ON \
    -DCMAKE_INSTALL_RPATH='$ORIGIN/../lib64;$ORIGIN/../lib' \
    -DMPI_CXX_LINK_FLAGS=
cmake --build "$build" --target module_bundle --parallel "$jobs"
