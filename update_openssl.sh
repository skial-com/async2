#!/bin/bash
set -e

# Update OpenSSL submodule to the latest 3.x release.
# All 3.x versions are API-compatible, so this picks the highest 3.x.y tag
# regardless of LTS status, ensuring the latest security patches.

cd "$(dirname "${BASH_SOURCE[0]}")/third_party/openssl"

# Read version from source tree — works in shallow clones where git describe fails
eval "$(grep -E '^(MAJOR|MINOR|PATCH)=' VERSION.dat)"
current="openssl-${MAJOR}.${MINOR}.${PATCH}"

latest=$(git ls-remote --tags origin 'refs/tags/openssl-3.*' \
    | grep -v '\^{}' \
    | sed 's|.*refs/tags/||' \
    | sort -V \
    | tail -1)

if [ -z "$latest" ]; then
    echo "Could not query remote tags, keeping $current"
    exit 0
fi

if [ "$current" = "$latest" ]; then
    echo "OpenSSL $current is latest"
    exit 0
fi

git fetch --depth 1 origin tag "$latest"
git checkout "$latest"
echo "Updated OpenSSL: $current -> $latest"
echo "Run ./build_deps.sh to rebuild with the new version."
