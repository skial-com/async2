@echo off
setlocal enabledelayedexpansion

REM Build all third_party dependencies as static libraries for Windows
REM Usage: build_deps.bat [x86|x86_64]
REM Default: x86_64

set SCRIPT_DIR=%~dp0
set THIRD_PARTY=%SCRIPT_DIR%third_party

set ARCH=%1
if "%ARCH%"=="" set ARCH=x86_64

if "%ARCH%"=="x86_64" (
    set LIB_DIR=%SCRIPT_DIR%libs\x86_64
    set CMAKE_ARCH=x64
) else if "%ARCH%"=="x86" (
    set LIB_DIR=%SCRIPT_DIR%libs\x86
    set CMAKE_ARCH=Win32
) else (
    echo Unknown architecture: %ARCH%
    exit /b 1
)

set INSTALL_DIR=%LIB_DIR%
set BUILD_TMP=%SCRIPT_DIR%build_tmp\%ARCH%
set PREFIX_DIR=%BUILD_TMP%\prefix

REM Force static CRT (/MT) for all deps to match the extension build
set MT_FLAGS=-DCMAKE_POLICY_DEFAULT_CMP0091=NEW -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded

mkdir "%INSTALL_DIR%" 2>nul
mkdir "%BUILD_TMP%" 2>nul
mkdir "%PREFIX_DIR%" 2>nul

echo === Building dependencies for %ARCH% ===

REM 1. zlib
echo --- Building zlib ---
cd /d "%BUILD_TMP%"
if exist zlib-build rmdir /s /q zlib-build
mkdir zlib-build && cd zlib-build
cmake "%THIRD_PARTY%\zlib" -A %CMAKE_ARCH% -DCMAKE_INSTALL_PREFIX="%PREFIX_DIR%" -DZLIB_BUILD_SHARED=OFF -DZLIB_BUILD_TESTING=OFF %MT_FLAGS%
cmake --build . --config Release -j
cmake --install . --config Release
copy "%PREFIX_DIR%\lib\zs.lib" "%PREFIX_DIR%\lib\z.lib"
copy "%PREFIX_DIR%\lib\zs.lib" "%INSTALL_DIR%\zlib.lib"

REM 2. c-ares
echo --- Building c-ares ---
cd /d "%BUILD_TMP%"
if exist cares-build rmdir /s /q cares-build
mkdir cares-build && cd cares-build
cmake "%THIRD_PARTY%\c-ares" -A %CMAKE_ARCH% -DCMAKE_INSTALL_PREFIX="%PREFIX_DIR%" -DCARES_STATIC=ON -DCARES_SHARED=OFF -DCARES_BUILD_TOOLS=OFF %MT_FLAGS%
cmake --build . --config Release -j
cmake --install . --config Release
copy "%PREFIX_DIR%\lib\cares.lib" "%INSTALL_DIR%\cares.lib"

REM 3. nghttp2
echo --- Building nghttp2 ---
cd /d "%BUILD_TMP%"
if exist nghttp2-build rmdir /s /q nghttp2-build
mkdir nghttp2-build && cd nghttp2-build
cmake "%THIRD_PARTY%\nghttp2" -A %CMAKE_ARCH% -DCMAKE_INSTALL_PREFIX="%PREFIX_DIR%" -DBUILD_SHARED_LIBS=OFF -DBUILD_STATIC_LIBS=ON -DENABLE_LIB_ONLY=ON %MT_FLAGS%
cmake --build . --config Release -j
cmake --install . --config Release
copy "%PREFIX_DIR%\lib\nghttp2.lib" "%INSTALL_DIR%\nghttp2.lib"

REM 4. brotli
echo --- Building brotli ---
cd /d "%BUILD_TMP%"
if exist brotli-build rmdir /s /q brotli-build
mkdir brotli-build && cd brotli-build
cmake "%THIRD_PARTY%\brotli" -A %CMAKE_ARCH% -DCMAKE_INSTALL_PREFIX="%PREFIX_DIR%" -DBUILD_SHARED_LIBS=OFF %MT_FLAGS%
cmake --build . --config Release -j
cmake --install . --config Release
copy "%PREFIX_DIR%\lib\brotlidec.lib" "%INSTALL_DIR%\brotlidec.lib"
copy "%PREFIX_DIR%\lib\brotlienc.lib" "%INSTALL_DIR%\brotlienc.lib"
copy "%PREFIX_DIR%\lib\brotlicommon.lib" "%INSTALL_DIR%\brotlicommon.lib"

REM 5. curl
echo --- Building curl ---
cd /d "%BUILD_TMP%"
if exist curl-build rmdir /s /q curl-build
mkdir curl-build && cd curl-build
cmake "%THIRD_PARTY%\curl" -A %CMAKE_ARCH% -DCMAKE_INSTALL_PREFIX="%PREFIX_DIR%" -DCMAKE_PREFIX_PATH="%PREFIX_DIR%" -DBUILD_SHARED_LIBS=OFF -DBUILD_CURL_EXE=OFF -DCURL_USE_SCHANNEL=ON -DENABLE_WEBSOCKETS=ON -DENABLE_ARES=ON -DUSE_NGHTTP2=ON -DCURL_BROTLI=ON -DCURL_ZSTD=OFF -DCURL_ZLIB=ON -DCURL_DISABLE_LDAP=ON -DCURL_DISABLE_LDAPS=ON -DCURL_DISABLE_FTP=ON -DCURL_DISABLE_FTPS=ON -DCURL_DISABLE_RTSP=ON -DCURL_DISABLE_DICT=ON -DCURL_DISABLE_TELNET=ON -DCURL_DISABLE_TFTP=ON -DCURL_DISABLE_POP3=ON -DCURL_DISABLE_IMAP=ON -DCURL_DISABLE_SMTP=ON -DCURL_DISABLE_GOPHER=ON -DCURL_DISABLE_MQTT=ON -DCURL_DISABLE_FILE=ON -DCURL_DISABLE_SMB=ON -DCURL_DISABLE_IPFS=ON -DCURL_USE_LIBPSL=OFF -DCURL_USE_LIBIDN2=OFF "-DCMAKE_C_FLAGS=/DNGHTTP2_STATICLIB" %MT_FLAGS%
cmake --build . --config Release -j
cmake --install . --config Release
copy "%PREFIX_DIR%\lib\libcurl.lib" "%INSTALL_DIR%\libcurl.lib"

REM Generate curl enums in sourcepawn include
echo --- Generating curl enums ---
python "%SCRIPT_DIR%tools\gen_enums.py"

REM 6. libuv
echo --- Building libuv ---
cd /d "%BUILD_TMP%"
if exist libuv-build rmdir /s /q libuv-build
mkdir libuv-build && cd libuv-build
cmake "%THIRD_PARTY%\libuv" -A %CMAKE_ARCH% -DCMAKE_INSTALL_PREFIX="%PREFIX_DIR%" -DBUILD_TESTING=OFF -DLIBUV_BUILD_SHARED=OFF %MT_FLAGS%
cmake --build . --config Release -j
cmake --install . --config Release
copy "%PREFIX_DIR%\lib\libuv.lib" "%INSTALL_DIR%\uv_a.lib"

echo === All dependencies built for %ARCH% ===
echo Libraries in: %INSTALL_DIR%
dir "%INSTALL_DIR%"
