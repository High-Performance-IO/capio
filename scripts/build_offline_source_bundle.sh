#!/usr/bin/env bash
set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
output=${1:-"$root/dist"}
version=$(awk '/^[[:space:]]*VERSION [0-9]/{print $2; exit}' "$root/CMakeLists.txt")
[[ $version =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || { printf 'error: could not read CAPIO version\n' >&2; exit 1; }

for command in git tar; do
    command -v "$command" >/dev/null || { printf 'error: %s is required\n' "$command" >&2; exit 1; }
done

package="capio-$version-offline-source"
work=$(mktemp -d "${TMPDIR:-/tmp}/capio-offline-source.XXXXXX")
stage="$work/$package"
trap 'rm -rf -- "$work"' EXIT
mkdir -p -- "$stage/vendor/_deps" "$output"
git -C "$root" ls-files -z | tar -C "$root" --null -T - -cf - | tar -C "$stage" -xf -
tar -C "$root" -cf - scripts/build_offline_source_bundle.sh scripts/build_offline_module.sh \
    scripts/syscall_intercept-local-capstone.patch | tar -C "$stage" -xf -

fetch() {
    local name=$1 url=$2 ref=$3 destination="$stage/vendor/_deps/$1-src"
    printf 'Fetching %s (%s)\n' "$name" "$ref"
    git -C "$stage/vendor/_deps" init -q "$name-src"
    git -C "$destination" remote add origin "$url"
    git -C "$destination" fetch -q --depth 1 origin "$ref"
    git -C "$destination" checkout -q --detach FETCH_HEAD
    rm -rf -- "$destination/.git"
}

fetch capio_cl https://github.com/High-Performance-IO/CAPIO-CL.git v1.5.2
fetch calf https://github.com/High-Performance-IO/calf.git v0.3.1
fetch args https://github.com/Taywee/args.git 6.4.7
fetch tomlplusplus https://github.com/marzer/tomlplusplus.git v3.4.0
fetch jsoncons https://github.com/danielaparker/jsoncons.git v1.8.1
fetch protobuf https://github.com/protocolbuffers/protobuf.git v31.1
fetch absl https://github.com/abseil/abseil-cpp.git 20250127.0

fetch syscall_intercept https://github.com/alpha-unito/syscall_intercept.git 7dbdf6ab9c576f96843ef2553b7efc7d15cf66b4
mv -- "$stage/vendor/_deps/syscall_intercept-src" "$stage/vendor/syscall_intercept"
fetch capstone https://github.com/capstone-engine/capstone.git accf4df62f1fba6f92cae692985d27063552601c
mv -- "$stage/vendor/_deps/capstone-src" "$stage/vendor/capstone"
git -C "$stage/vendor/syscall_intercept" apply "$stage/scripts/syscall_intercept-local-capstone.patch"

archive="$output/$package.tar.gz"
tar -C "$work" -czf "$archive" "$package"
printf '%s\n' "$archive"
