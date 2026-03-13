#!/bin/bash
set -e

# Build all third_party dependencies as static libraries
# Usage: ./build_deps.sh [x86|x86_64|both]
# Default: both architectures

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
THIRD_PARTY="$SCRIPT_DIR/third_party"

# If no arg or "both", build both archs by re-invoking ourselves
if [ -z "$1" ] || [ "$1" = "both" ]; then
    "$0" x86_64
    "$0" x86
    exit 0
fi

ARCH="$1"
if [ "$ARCH" = "x86_64" ] || [ "$ARCH" = "amd64" ]; then
    ARCH="x86_64"
    ARCH_FLAGS=""
    LIB_DIR="$SCRIPT_DIR/libs/x86_64"
elif [ "$ARCH" = "x86" ] || [ "$ARCH" = "i386" ] || [ "$ARCH" = "i686" ]; then
    ARCH="x86"
    ARCH_FLAGS="-m32"
    LIB_DIR="$SCRIPT_DIR/libs/x86"
else
    echo "Unknown architecture: $ARCH"
    exit 1
fi

INSTALL_DIR="$LIB_DIR"
NPROC=$(nproc 2>/dev/null || echo 4)

mkdir -p "$INSTALL_DIR"
mkdir -p "$SCRIPT_DIR/build_tmp"
BUILD_TMP="$SCRIPT_DIR/build_tmp/$ARCH"
mkdir -p "$BUILD_TMP"

export CFLAGS="-fPIC $ARCH_FLAGS"
export CXXFLAGS="-fPIC $ARCH_FLAGS"
export LDFLAGS="$ARCH_FLAGS"

echo "=== Building dependencies for $ARCH ==="
echo "Install dir: $INSTALL_DIR"

# prefix dir for cmake find_package
PREFIX_DIR="$BUILD_TMP/prefix"
mkdir -p "$PREFIX_DIR/lib" "$PREFIX_DIR/include"

# 1. zlib
echo "--- Building zlib ---"
cd "$BUILD_TMP"
rm -rf zlib-build && mkdir zlib-build && cd zlib-build
cmake "$THIRD_PARTY/zlib" \
    -DCMAKE_C_FLAGS="$CFLAGS" \
    -DCMAKE_INSTALL_PREFIX="$PREFIX_DIR" \
    -DBUILD_SHARED_LIBS=OFF \
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON
make -j"$NPROC"
make install
cp "$PREFIX_DIR/lib/libz.a" "$INSTALL_DIR/"

# 2. c-ares
echo "--- Building c-ares ---"
cd "$BUILD_TMP"
rm -rf cares-build && mkdir cares-build && cd cares-build
cmake "$THIRD_PARTY/c-ares" \
    -DCMAKE_C_FLAGS="$CFLAGS" \
    -DCMAKE_INSTALL_PREFIX="$PREFIX_DIR" \
    -DCARES_STATIC=ON \
    -DCARES_SHARED=OFF \
    -DCARES_BUILD_TOOLS=OFF \
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON
make -j"$NPROC"
make install
cp "$PREFIX_DIR/lib/libcares.a" "$INSTALL_DIR/"
# Copy generated ares_build.h so AMBuild can find it alongside the source headers
cp "$PREFIX_DIR/include/ares_build.h" "$THIRD_PARTY/c-ares/include/ares_build.h"

# 3. nghttp2
echo "--- Building nghttp2 ---"
cd "$BUILD_TMP"
rm -rf nghttp2-build && mkdir nghttp2-build && cd nghttp2-build
cmake "$THIRD_PARTY/nghttp2" \
    -DCMAKE_C_FLAGS="$CFLAGS" \
    -DCMAKE_INSTALL_PREFIX="$PREFIX_DIR" \
    -DBUILD_SHARED_LIBS=OFF \
    -DBUILD_STATIC_LIBS=ON \
    -DENABLE_LIB_ONLY=ON \
    -DBUILD_TESTING=OFF \
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON
make -j"$NPROC"
make install
cp "$PREFIX_DIR/lib/libnghttp2.a" "$INSTALL_DIR/"

# 4. brotli
echo "--- Building brotli ---"
cd "$BUILD_TMP"
rm -rf brotli-build && mkdir brotli-build && cd brotli-build
cmake "$THIRD_PARTY/brotli" \
    -DCMAKE_C_FLAGS="$CFLAGS" \
    -DCMAKE_INSTALL_PREFIX="$PREFIX_DIR" \
    -DBUILD_SHARED_LIBS=OFF \
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON
make -j"$NPROC"
make install
cp "$PREFIX_DIR/lib/libbrotlidec.a" "$INSTALL_DIR/"
cp "$PREFIX_DIR/lib/libbrotlienc.a" "$INSTALL_DIR/"
cp "$PREFIX_DIR/lib/libbrotlicommon.a" "$INSTALL_DIR/"

# 5. OpenSSL (submodule in third_party/openssl)
echo "--- Building OpenSSL ---"
if [ "$ARCH" = "x86_64" ]; then
    OPENSSL_TARGET="linux-x86_64"
else
    OPENSSL_TARGET="linux-x86"
fi
cd "$BUILD_TMP"
rm -rf openssl-build && mkdir openssl-build && cd openssl-build
"$THIRD_PARTY/openssl/Configure" "$OPENSSL_TARGET" no-shared \
    no-ssl3 no-tls1 no-tls1_1 no-dtls \
    no-des no-rc2 no-rc4 no-rc5 no-idea no-seed no-camellia no-aria no-bf no-cast \
    no-sm2 no-sm3 no-sm4 \
    no-engine no-fips no-legacy no-comp no-cms no-ts no-ocb \
    no-srp no-sctp no-srtp no-nextprotoneg \
    no-whirlpool no-mdc2 no-dsa no-dh \
    no-tests no-docs no-apps \
    -fPIC --prefix="$PREFIX_DIR" --libdir=lib
make -j"$NPROC"
make install_sw
cp "$PREFIX_DIR/lib/libssl.a" "$INSTALL_DIR/"
cp "$PREFIX_DIR/lib/libcrypto.a" "$INSTALL_DIR/"

# 6. curl (depends on zlib, c-ares, nghttp2, brotli, LibreSSL)
echo "--- Building curl ---"
cd "$BUILD_TMP"
rm -rf curl-build && mkdir curl-build && cd curl-build
cmake "$THIRD_PARTY/curl" \
    -DCMAKE_C_FLAGS="$CFLAGS" \
    -DCMAKE_INSTALL_PREFIX="$PREFIX_DIR" \
    -DCMAKE_PREFIX_PATH="$PREFIX_DIR" \
    -DOPENSSL_ROOT_DIR="$PREFIX_DIR" \
    -DBUILD_SHARED_LIBS=OFF \
    -DBUILD_CURL_EXE=OFF \
    -DCURL_USE_OPENSSL=ON \
    -DENABLE_WEBSOCKETS=ON \
    -DENABLE_ARES=ON \
    -DUSE_NGHTTP2=ON \
    -DCURL_BROTLI=ON \
    -DCURL_ZSTD=OFF \
    -DCURL_ZLIB=ON \
    -DCURL_DISABLE_LDAP=ON \
    -DCURL_DISABLE_LDAPS=ON \
    -DCURL_DISABLE_FTP=ON \
    -DCURL_DISABLE_FTPS=ON \
    -DCURL_DISABLE_RTSP=ON \
    -DCURL_DISABLE_DICT=ON \
    -DCURL_DISABLE_TELNET=ON \
    -DCURL_DISABLE_TFTP=ON \
    -DCURL_DISABLE_POP3=ON \
    -DCURL_DISABLE_IMAP=ON \
    -DCURL_DISABLE_SMTP=ON \
    -DCURL_DISABLE_GOPHER=ON \
    -DCURL_DISABLE_MQTT=ON \
    -DCURL_DISABLE_FILE=ON \
    -DCURL_DISABLE_SMB=ON \
    -DCURL_DISABLE_IPFS=ON \
    -DCURL_USE_LIBPSL=OFF \
    -DCURL_USE_LIBIDN2=OFF \
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON
make -j"$NPROC"
make install
cp "$PREFIX_DIR/lib/libcurl.a" "$INSTALL_DIR/"

# Generate curl enums in sourcepawn include
echo "--- Generating curl enums ---"
python3 "$SCRIPT_DIR/tools/gen_enums.py"

# 7. libuv
echo "--- Building libuv ---"
cd "$BUILD_TMP"
rm -rf libuv-build && mkdir libuv-build && cd libuv-build
cmake "$THIRD_PARTY/libuv" \
    -DCMAKE_C_FLAGS="$CFLAGS" \
    -DCMAKE_INSTALL_PREFIX="$PREFIX_DIR" \
    -DBUILD_TESTING=OFF \
    -DLIBUV_BUILD_SHARED=OFF \
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON
make -j"$NPROC"
make install
cp "$PREFIX_DIR/lib/libuv.a" "$INSTALL_DIR/"

echo "=== All dependencies built for $ARCH ==="
echo "Libraries in: $INSTALL_DIR"
ls -la "$INSTALL_DIR"
