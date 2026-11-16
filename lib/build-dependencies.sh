#!/usr/bin/env bash

# Build the pinned static hidapi and libusb dependencies for Linux x86_64.
set -euo pipefail

lib_root=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
prefix="$lib_root/linux-x86_64"
build_root="$lib_root/.build/linux-x86_64"
hidapi_source="$lib_root/hidapi"
libusb_source="$lib_root/libusb"

for path in "$hidapi_source" "$libusb_source"; do
    if [[ ! -e "$path/.git" ]]; then
        printf 'Missing dependency submodule at %s. Run: git submodule update --init --recursive\n' "$path" >&2
        exit 1
    fi
done

command -v cmake >/dev/null
command -v autoreconf >/dev/null
command -v make >/dev/null
command -v pkg-config >/dev/null
mkdir -p "$prefix" "$build_root"

(cd "$libusb_source" && NOCONFIGURE=1 ./autogen.sh)
mkdir -p "$build_root/libusb"
(
    cd "$build_root/libusb"
    CFLAGS="${CFLAGS:-} -fPIC" "$libusb_source/configure" \
        --prefix="$prefix" \
        --with-pic \
        --enable-static \
        --disable-shared \
        --disable-udev \
        --disable-examples-build \
        --disable-tests-build
    make -j"$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 1)"
    make install
)

PKG_CONFIG_PATH="$prefix/lib/pkgconfig:${PKG_CONFIG_PATH:-}" \
cmake -S "$hidapi_source" -B "$build_root/hidapi" \
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
    -DBUILD_SHARED_LIBS=OFF \
    -DHIDAPI_WITH_HIDRAW=OFF \
    -DHIDAPI_WITH_LIBUSB=ON \
    -DHIDAPI_WITH_TESTS=OFF \
    -DHIDAPI_BUILD_HIDTEST=OFF \
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
    -DCMAKE_INSTALL_LIBDIR=lib \
    -DCMAKE_INSTALL_PREFIX="$prefix" \
    -DCMAKE_BUILD_TYPE=Release
cmake --build "$build_root/hidapi" --parallel
cmake --install "$build_root/hidapi"

printf 'Static dependencies installed in %s\n' "$prefix"
