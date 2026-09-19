#!/usr/bin/env bash
set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
output=${1:-"$root/dist"}
build=${BUILD_DIR:-"$root/dist/module-bundle"}
jobs=${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || printf '1')}
cmake=${CMAKE_COMMAND:-cmake}

for command in "$cmake" git tar; do
    command -v "$command" >/dev/null || { printf 'error: %s is required\n' "$command" >&2; exit 1; }
done

if [[ ${PACKAGE_ONLY:-0} != 1 ]]; then
    "$cmake" -S "$root" -B "$build" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCAPIO_BUILD_TESTS=OFF \
        -DCMAKE_BUILD_WITH_INSTALL_RPATH=ON \
        -DCMAKE_INSTALL_RPATH='$ORIGIN/../lib64;$ORIGIN/../lib' \
        -DMPI_CXX_LINK_FLAGS=
    "$cmake" --build "$build" --parallel "$jobs"
fi

version=$(awk '/^[[:space:]]*VERSION [0-9]/{print $2; exit}' "$root/CMakeLists.txt")
[[ $version =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || { printf 'error: could not read CAPIO version\n' >&2; exit 1; }

package="capio-$version"
prefix="$build/package/$package"
rm -rf -- "$prefix"

"$cmake" --install "$build" --prefix "$prefix" --strip
rm -rf -- "$prefix/include" "$prefix/share" "$prefix/lib/pkgconfig" "$prefix/lib64/pkgconfig" "$prefix/lib/cmake" "$prefix/lib64/cmake"
rm -f -- "$prefix"/lib/*.a "$prefix"/lib64/*.a
rm -f -- "$prefix"/bin/capio_*tests
install -Dm644 "$root/scripts/capio.module" "$prefix/modulefiles/capio/$version"

mkdir -p -- "$output"
archive="$output/$package-linux-$(uname -m).tar.gz"
tar -C "$build/package" -czf "$archive" "$package"

printf '%s\n' "$archive"
