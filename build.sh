#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ASAN=0
DEBUG=0
ARCH=""
# SourceMod path: --sm-path takes precedence, then SM_PATH env var,
# then second positional arg, then the default below.
SM_PATH="${SM_PATH:-}"
# Staging dir: --stage-dir takes precedence, then STAGE_DIR env var,
# then the default below. Binaries are only staged if it already exists.
STAGE_DIR="${STAGE_DIR:-../../dist/extensions}"

usage() {
    cat <<'EOF'
Usage: ./build.sh [ARCH] [SM_PATH] [options]

  ARCH        x86 | x86_64 | all  (default: current platform arch)
  SM_PATH     Path to the SourceMod SDK (default: ../../sdk/sourcemod)

Options:
  --sm-path PATH      Path to the SourceMod SDK (overrides positional/env)
  --sm-path=PATH      Same as above
  --stage-dir PATH    Dir to stage built .so binaries into (overrides env)
  --stage-dir=PATH    Same as above
  --asan              Build with AddressSanitizer (implies Debug)
  --debug             Build in Debug mode
  -h, --help          Show this help

The SourceMod path may also be set via the SM_PATH environment variable.
The staging dir may also be set via the STAGE_DIR environment variable; it
defaults to ../../dist/extensions and binaries are only staged there if the
directory already exists.
EOF
}

# Parse args
while [ $# -gt 0 ]; do
    arg="$1"
    case "$arg" in
        --asan) ASAN=1 ;;
        --debug) DEBUG=1 ;;
        -h|--help) usage; exit 0 ;;
        --sm-path) SM_PATH="$2"; shift ;;
        --sm-path=*) SM_PATH="${arg#--sm-path=}" ;;
        --stage-dir) STAGE_DIR="$2"; shift ;;
        --stage-dir=*) STAGE_DIR="${arg#--stage-dir=}" ;;
        *)
            if [ -z "$ARCH" ]; then
                ARCH="$arg"
            elif [ -z "$SM_PATH" ]; then
                SM_PATH="$arg"
            fi
            ;;
    esac
    shift
done

# Default to current platform arch; "all" builds both x86 and x86_64
if [ -z "$ARCH" ]; then
    ARCH="$(uname -m)"
    [ "$ARCH" = "x86_64" ] || ARCH="x86"
elif [ "$ARCH" = "all" ]; then
    ARCH="x86,x86_64"
fi
SM_PATH="${SM_PATH:-../../sdk/sourcemod}"
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

# Stage binaries into STAGE_DIR/{,x64}, only if STAGE_DIR already exists
if [ -d "$STAGE_DIR" ]; then
    STAGE_DIR="$(cd "$STAGE_DIR" && pwd)"
    echo "=== Staging binaries into $STAGE_DIR ==="
    mkdir -p "$STAGE_DIR/x64"
    shopt -s nullglob
    for f in build/package/addons/sourcemod/extensions/*.so; do
        cp -u "$f" "$STAGE_DIR/"
    done
    for f in build/package/addons/sourcemod/extensions/x64/*.so; do
        cp -u "$f" "$STAGE_DIR/x64/"
    done
else
    echo "Staging dir '$STAGE_DIR' does not exist; skipping binary staging."
fi
