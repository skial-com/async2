#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ASAN=0
DEBUG=0
ARCH=""
SM_PATH=""

# Parse args
for arg in "$@"; do
    if [ "$arg" = "--asan" ]; then
        ASAN=1
    elif [ "$arg" = "--debug" ]; then
        DEBUG=1
    elif [ -z "$ARCH" ]; then
        ARCH="$arg"
    elif [ -z "$SM_PATH" ]; then
        SM_PATH="$arg"
    fi
done

ARCH="${ARCH:-x86,x86_64}"
SM_PATH="${SM_PATH:-../sdk/sourcemod}"
SM_PATH="$(cd "$SCRIPT_DIR" && cd "$SM_PATH" && pwd)"

cd "$SCRIPT_DIR"

ASAN_FLAGS="-fsanitize=address -fno-omit-frame-pointer"

IFS=',' read -ra ARCHS <<< "$ARCH"
for arch in "${ARCHS[@]}"; do
    echo "=== Building for $arch ==="
    build_dir="build/$arch"
    mkdir -p "$build_dir" && cd "$build_dir"

    BUILD_TYPE="Release"
    if [ "$ASAN" = "1" ] || [ "$DEBUG" = "1" ]; then
        BUILD_TYPE="Debug"
    fi

    if [ "$ASAN" = "1" ]; then
        echo "(AddressSanitizer enabled)"
        cmake "$SCRIPT_DIR" -DSM_PATH="$SM_PATH" -DCMAKE_BUILD_TYPE=Debug \
            -DCMAKE_C_FLAGS="$ASAN_FLAGS" -DCMAKE_CXX_FLAGS="$ASAN_FLAGS" \
            -DCMAKE_SHARED_LINKER_FLAGS="-fsanitize=address"
    elif [ "$arch" = "x86" ]; then
        cmake "$SCRIPT_DIR" -DSM_PATH="$SM_PATH" -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
            -DCMAKE_C_FLAGS="-m32" -DCMAKE_CXX_FLAGS="-m32" -DCMAKE_SHARED_LINKER_FLAGS="-m32"
    else
        cmake "$SCRIPT_DIR" -DSM_PATH="$SM_PATH" -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
            -DCMAKE_C_FLAGS="" -DCMAKE_CXX_FLAGS="" -DCMAKE_SHARED_LINKER_FLAGS=""
    fi

    cmake --build . -j "$(nproc)"
    cd "$SCRIPT_DIR"
done

# Package
echo "=== Packaging ==="
rm -rf build/package
mkdir -p build/package/addons/sourcemod/extensions
mkdir -p build/package/addons/sourcemod/scripting/include/async2
cp sourcepawn/async2.inc build/package/addons/sourcemod/scripting/include/
cp sourcepawn/async2/*.inc build/package/addons/sourcemod/scripting/include/async2/

for arch in "${ARCHS[@]}"; do
    if [ "$arch" = "x86_64" ]; then
        mkdir -p build/package/addons/sourcemod/extensions/x64
        cp "build/$arch/async2.ext.so" build/package/addons/sourcemod/extensions/x64/
    else
        cp "build/$arch/async2.ext.so" build/package/addons/sourcemod/extensions/
    fi
done

echo "Build complete. Output in: $SCRIPT_DIR/build/package/"
