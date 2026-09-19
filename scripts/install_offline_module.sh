#!/usr/bin/env bash
set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
build=${BUILD_DIR:-"$root/dist/module-bundle"}
jobs=${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || printf '1')}

cmake -S "$root" -B "$build" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCAPIO_BUILD_TESTS=OFF \
    -DFETCHCONTENT_FULLY_DISCONNECTED=ON \
    -DFETCHCONTENT_SOURCE_DIR_ABSL="$root/vendor/_deps/absl-src" \
    -DFETCHCONTENT_SOURCE_DIR_ARGS="$root/vendor/_deps/args-src" \
    -DFETCHCONTENT_SOURCE_DIR_CALF="$root/vendor/_deps/calf-src" \
    -DFETCHCONTENT_SOURCE_DIR_CAPIO_CL="$root/vendor/_deps/capio_cl-src" \
    -DFETCHCONTENT_SOURCE_DIR_JSONCONS="$root/vendor/_deps/jsoncons-src" \
    -DFETCHCONTENT_SOURCE_DIR_PROTOBUF="$root/vendor/_deps/protobuf-src" \
    -DFETCHCONTENT_SOURCE_DIR_TOMLPLUSPLUS="$root/vendor/_deps/tomlplusplus-src" \
    -DCAPIO_SYSCALL_INTERCEPT_SOURCE_DIR="$root/vendor/syscall_intercept" \
    -DCMAKE_BUILD_WITH_INSTALL_RPATH=ON \
    -DCMAKE_INSTALL_RPATH='$ORIGIN/../lib64;$ORIGIN/../lib' \
    -DMPI_CXX_LINK_FLAGS=
cmake --build "$build" --target module_bundle --parallel "$jobs"
