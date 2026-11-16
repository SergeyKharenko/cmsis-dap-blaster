#!/usr/bin/env bash

# Initialize submodules, build bundled dependencies, and build the project.
set -euo pipefail

root_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
build_dir="${CMSIS_DAP_BUILD_DIR:-$root_dir/build}"
build_type="${CMAKE_BUILD_TYPE:-Release}"

git -C "$root_dir" submodule update --init --recursive

checkout_dependency_tag() {
    local name="$1"
    local path="$2"
    local tag
    tag=$(git -C "$root_dir" config -f "$root_dir/.gitmodules" "submodule.$name.tag")
    if [[ -z "$tag" ]]; then
        printf 'Missing tag for submodule %s in .gitmodules\n' "$name" >&2
        exit 1
    fi
    if ! git -C "$root_dir/$path" rev-parse --verify --quiet "refs/tags/$tag" >/dev/null; then
        git -C "$root_dir/$path" fetch --tags --quiet
    fi
    git -C "$root_dir/$path" checkout --detach "$tag"
}

checkout_dependency_tag lib/hidapi lib/hidapi
checkout_dependency_tag lib/libusb lib/libusb
"$root_dir/lib/build-dependencies.sh"

cmake -S "$root_dir" -B "$build_dir" -DCMAKE_BUILD_TYPE="$build_type"
cmake --build "$build_dir" --parallel
