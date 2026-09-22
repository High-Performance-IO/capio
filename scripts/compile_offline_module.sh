#!/usr/bin/env bash
set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
build=${BUILD_DIR:-"$root/build"}
jobs=${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || printf '1')}

cache="$root/offline-source.cmake"
[[ -f "$cache" ]] || { printf 'error: offline source settings are missing: %s\n' "$cache" >&2; exit 1; }

cmake -C "$cache" -S "$root" -B "$build" \
    -DCALF_TESTS=OFF \
    -DCALF_PYTHON_TESTS=OFF \
    -DCALF_BUILD_PYTHON_BINDINGS=OFF \
    -DCALF_PROTOBUF_FORCE_FETCH=ON \
    -Dprotobuf_BUILD_TESTS=OFF \
    -Dprotobuf_FORCE_FETCH_DEPENDENCIES=ON \
    -DABSL_BUILD_TESTING=OFF \
    -DFETCHCONTENT_FULLY_DISCONNECTED=ON \
    -DCMAKE_INSTALL_RPATH='$ORIGIN/../lib64;$ORIGIN/../lib' \
    -DMPI_CXX_LINK_FLAGS=
cmake --build "$build" --parallel "$jobs"
cmake --build "$build" --target module_bundle --parallel "$jobs"
