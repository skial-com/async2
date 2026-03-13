#!/bin/bash
set -e

# Build async2 Linux extension inside Docker
# Usage: ./docker/build.sh [x86|x86_64|x86,x86_64]
# Requires: Docker with Linux containers
# Output: build/package/

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
ARCH="${1:-x86_64}"

docker build -t async2-builder "$SCRIPT_DIR"

docker run --rm \
    -v "$PROJECT_DIR:/src" \
    -w /src \
    async2-builder \
    bash -c "./build_deps.sh ${ARCH%%,*} && ./build.sh $ARCH /tmp/sourcemod"

echo "Build complete. Output in: $PROJECT_DIR/build/package/"
