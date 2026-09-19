#!/usr/bin/env bash
set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
build=${BUILD_DIR:-"$root/dist/module-bundle"}
output=${1:-"$root/dist"}
version=$(awk '/^[[:space:]]*VERSION [0-9]/{print $2; exit}' "$root/CMakeLists.txt")
[[ $version =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || { printf 'error: could not read CAPIO version\n' >&2; exit 1; }

package="capio-$version-offline-source"
stage="$build/offline-source/$package"
deps=(absl args calf capio_cl jsoncons protobuf tomlplusplus)

rm -rf -- "$stage"
mkdir -p -- "$stage/vendor/_deps" "$output"
tar -C "$root" -cf - CMakeLists.txt LICENSE README.md capio scripts | tar -C "$stage" -xf -

for dep in "${deps[@]}"; do
    source="$build/_deps/$dep-src"
    [[ -d $source ]] || { printf 'error: dependency source is missing: %s\n' "$source" >&2; exit 1; }
    cp -a -- "$source" "$stage/vendor/_deps/$dep-src"
    rm -rf -- "$stage/vendor/_deps/$dep-src/.git"
done

syscall_source="$build/capio/posix/syscall_intercept/src/syscall_intercept"
[[ -d $syscall_source ]] || { printf 'error: dependency source is missing: %s\n' "$syscall_source" >&2; exit 1; }
cp -a -- "$syscall_source" "$stage/vendor/syscall_intercept"
rm -rf -- "$stage/vendor/syscall_intercept/.git"

archive="$output/$package.tar.gz"
tar -C "$build/offline-source" -czf "$archive" "$package"
printf '%s\n' "$archive"
