cmake_minimum_required(VERSION 3.16)
project(async2 C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# --- Options ---
set(SM_PATH "" CACHE PATH "Path to SourceMod source tree")
set(EXT_VERSION "" CACHE STRING "Extension version (e.g., v1.5). Defaults to 'dev'.")

if(NOT SM_PATH)
    # Walk up directories looking for sourcemod/
    set(_head "${CMAKE_SOURCE_DIR}")
    while(TRUE)
        foreach(_name sourcemod sourcemod-1.12)
            if(IS_DIRECTORY "${_head}/${_name}")
                set(SM_PATH "${_head}/${_name}")
                break()
            endif()
        endforeach()
        if(SM_PATH)
            break()
        endif()
        get_filename_component(_parent "${_head}" DIRECTORY)
        if(_parent STREQUAL _head)
            break()
        endif()
        set(_head "${_parent}")
    endwhile()
endif()

if(NOT SM_PATH OR NOT IS_DIRECTORY "${SM_PATH}")
    message(FATAL_ERROR "Could not find SourceMod. Set -DSM_PATH=<path>")
endif()
message(STATUS "SourceMod: ${SM_PATH}")

# --- Architecture detection ---
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(ARCH "x86_64")
else()
    set(ARCH "x86")
endif()

set(LIB_DIR "${CMAKE_SOURCE_DIR}/libs/${ARCH}")
if(NOT IS_DIRECTORY "${LIB_DIR}")
    message(FATAL_ERROR "Pre-built libraries not found at ${LIB_DIR}. Run build_deps first.")
endif()

# --- Source files ---
set(SOURCES
    src/extension.cpp
    src/event_loop.cpp
    src/http_request.cpp
    src/handle_manager.cpp
    src/compression.cpp
    src/data/data_node.cpp
    src/data/data_handle.cpp
    src/natives_http.cpp
    src/natives_json.cpp
    src/natives_curl.cpp
    src/natives_msgpack.cpp
    src/natives_tcp.cpp
    src/natives_udp.cpp
    src/natives_dns.cpp
    src/natives_crypto.cpp
    src/dns_resolver.cpp
    src/tcp_socket.cpp
    src/ws_connection.cpp
    src/natives_ws.cpp
    src/linked_list.cpp
    src/natives_linkedlist.cpp
    src/natives_url.cpp
    src/natives_time.cpp
    src/natives_hjson.cpp
    src/hjson/hjson_parse.cpp
    src/msgpack/msgpack_parse.cpp
    src/msgpack/msgpack_serialize.cpp
    src/compile_time.cpp
    third_party/simdjson/singleheader/simdjson.cpp
)

# SourceMod SDK ext source
if(EXISTS "${CMAKE_SOURCE_DIR}/sdk/smsdk_ext.cpp")
    list(APPEND SOURCES sdk/smsdk_ext.cpp)
else()
    list(APPEND SOURCES "${SM_PATH}/public/smsdk_ext.cpp")
endif()

# Linux stdcxx compat
if(UNIX AND NOT APPLE)
    list(APPEND SOURCES "${SM_PATH}/public/amtl/compat/stdcxx.cpp")
endif()

# --- Library target ---
add_library(async2 SHARED ${SOURCES})

# Generate compile timestamp with timezone before every build.
# Only compile_time.cpp recompiles — all other sources use incremental compilation.
add_custom_target(touch_compile_time ALL
    COMMAND ${CMAKE_COMMAND}
        -DOUTPUT_FILE="${CMAKE_BINARY_DIR}/build_timestamp.h"
        -DSOURCE_FILE="${CMAKE_SOURCE_DIR}/src/compile_time.cpp"
        -P "${CMAKE_SOURCE_DIR}/cmake/update_timestamp.cmake"
)
target_include_directories(async2 PRIVATE "${CMAKE_BINARY_DIR}")
add_dependencies(async2 touch_compile_time)

execute_process(
    COMMAND git rev-parse --short HEAD
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    OUTPUT_VARIABLE _git_hash
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(EXT_VERSION)
    # Strip leading 'v' if present (v1.5 -> 1.5)
    string(REGEX REPLACE "^v" "" _version "${EXT_VERSION}")
    if(_git_hash)
        set(_version "${_version} commit ${_git_hash}")
    endif()
    target_compile_definitions(async2 PRIVATE "SMEXT_CONF_VERSION=\"${_version}\"")
elseif(_git_hash)
    target_compile_definitions(async2 PRIVATE "SMEXT_CONF_VERSION=\"dev commit ${_git_hash}\"")
endif()

set_target_properties(async2 PROPERTIES
    OUTPUT_NAME "async2.ext"
    PREFIX ""
)

# --- Include paths ---
target_include_directories(async2 PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/data
    ${CMAKE_SOURCE_DIR}/src/msgpack
    ${CMAKE_SOURCE_DIR}/src/hjson
    ${CMAKE_SOURCE_DIR}/sdk
    ${CMAKE_SOURCE_DIR}/public
    ${CMAKE_SOURCE_DIR}/build_tmp/${ARCH}/prefix/include
    ${CMAKE_SOURCE_DIR}/third_party/libuv/include
    ${CMAKE_SOURCE_DIR}/third_party/curl/include
    ${CMAKE_SOURCE_DIR}/third_party/c-ares/include
    ${CMAKE_SOURCE_DIR}/third_party/zlib
    ${CMAKE_SOURCE_DIR}/third_party/simdjson/singleheader
    ${CMAKE_SOURCE_DIR}/third_party/robin-map/include
    ${CMAKE_SOURCE_DIR}/third_party/xxhash
    ${SM_PATH}/public
    ${SM_PATH}/public/extensions
    ${SM_PATH}/sourcepawn/include
    ${SM_PATH}/public/amtl/amtl
    ${SM_PATH}/public/amtl
)

# --- Platform configuration ---
if(MSVC)
    target_compile_definitions(async2 PRIVATE
        WIN32 _WINDOWS NOMINMAX
        _CRT_SECURE_NO_DEPRECATE
        _CRT_SECURE_NO_WARNINGS
        _CRT_NONSTDC_NO_DEPRECATE
        _ITERATOR_DEBUG_LEVEL=0
        CURL_STATICLIB CARES_STATICLIB NGHTTP2_STATICLIB
    )
    if(ARCH STREQUAL "x86_64")
        target_compile_definitions(async2 PRIVATE WIN64)
    endif()

    target_compile_options(async2 PRIVATE
        /W3 /MT /Oy-
        $<$<CONFIG:Release>:/O2 /Zi>
        $<$<CONFIG:Debug>:/Od /RTC1 /MTd>
    )
    target_compile_options(async2 PRIVATE
        $<$<COMPILE_LANGUAGE:CXX>:/EHsc /GR- /TP /std:c++17>
    )

    target_link_libraries(async2 PRIVATE
        ${LIB_DIR}/libcurl.lib
        ${LIB_DIR}/nghttp2.lib
        ${LIB_DIR}/cares.lib
        ${LIB_DIR}/brotlidec.lib
        ${LIB_DIR}/brotlienc.lib
        ${LIB_DIR}/brotlicommon.lib
        ${LIB_DIR}/zlib.lib
        ${LIB_DIR}/uv_a.lib
        ws2_32 iphlpapi psapi userenv bcrypt crypt32 normaliz wldap32 secur32 dbghelp
        kernel32 user32 gdi32 winspool comdlg32 advapi32 shell32
        ole32 oleaut32 uuid odbc32 odbccp32
    )
    target_link_options(async2 PRIVATE
        /SUBSYSTEM:WINDOWS
        $<$<CONFIG:Release>:/OPT:ICF /OPT:REF>
        $<$<CONFIG:Debug>:/NODEFAULTLIB:libcmt>
    )

elseif(UNIX AND NOT APPLE)
    target_compile_definitions(async2 PRIVATE
        LINUX _LINUX POSIX _FILE_OFFSET_BITS=64
        stricmp=strcasecmp _stricmp=strcasecmp
        _snprintf=snprintf _vsnprintf=vsnprintf
        HAVE_STDINT_H GNUC
    )

    target_compile_options(async2 PRIVATE
        -pipe -fno-strict-aliasing -Wall
        -Wno-unused -Wno-switch -Wno-array-bounds
        -fvisibility=hidden -msse -fno-omit-frame-pointer
        $<$<CONFIG:Release>:-O2 -g>
        $<$<CONFIG:Debug>:-Og -g3>
    )
    target_compile_options(async2 PRIVATE
        $<$<COMPILE_LANGUAGE:CXX>:
            -fno-threadsafe-statics -Wno-non-virtual-dtor
            -Wno-overloaded-virtual -Wno-register
            -fvisibility-inlines-hidden -Wno-delete-non-virtual-dtor
        >
    )

    # GCC-specific
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_compile_options(async2 PRIVATE -mfpmath=sse -Wno-maybe-uninitialized)
        target_compile_options(async2 PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-Wno-class-memaccess -Wno-packed-not-aligned>)
        target_link_options(async2 PRIVATE -static-libgcc)
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        target_compile_options(async2 PRIVATE
            -Wno-sometimes-uninitialized -Wno-narrowing
            -Wno-implicit-int-float-conversion -Wno-tautological-overlap-compare
        )
        target_compile_options(async2 PRIVATE $<$<COMPILE_LANGUAGE:CXX>:
            -Wno-expansion-to-defined -Wno-inconsistent-missing-override
            -Wno-null-dereference -Wno-implicit-exception-spec-mismatch
            -Wno-deprecated-register
        >)
        target_link_libraries(async2 PRIVATE gcc_eh)
    endif()

    target_link_options(async2 PRIVATE -static-libstdc++)
    # Hide all symbols except GetSMExtAPI. Prevents STB_GNU_UNIQUE symbols
    # from -static-libstdc++ from pinning the .so (breaks dlclose hot-reload).
    target_link_options(async2 PRIVATE "-Wl,--version-script=${CMAKE_SOURCE_DIR}/version_script.lds")

    # Static link all deps — use full paths to .a files because cmake
    # reorders -Wl,-Bstatic/-Bdynamic away from -l flags, breaking static linking.
    target_link_libraries(async2 PRIVATE
        ${LIB_DIR}/libcurl.a
        ${LIB_DIR}/libnghttp2.a
        ${LIB_DIR}/libssl.a
        ${LIB_DIR}/libcrypto.a
        ${LIB_DIR}/libcares.a
        ${LIB_DIR}/libbrotlidec.a
        ${LIB_DIR}/libbrotlienc.a
        ${LIB_DIR}/libbrotlicommon.a
        ${LIB_DIR}/libz.a
        ${LIB_DIR}/libuv.a
        pthread dl rt m
    )

elseif(APPLE)
    target_compile_definitions(async2 PRIVATE OSX _OSX POSIX)
    target_compile_options(async2 PRIVATE -mmacosx-version-min=10.15)
    target_compile_options(async2 PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-stdlib=libc++>)
    target_link_options(async2 PRIVATE -mmacosx-version-min=10.15 -stdlib=libc++)
    target_link_libraries(async2 PRIVATE c++)
endif()

# --- Install / Package ---
if(ARCH STREQUAL "x86_64")
    set(EXT_INSTALL_DIR "addons/sourcemod/extensions/x64")
else()
    set(EXT_INSTALL_DIR "addons/sourcemod/extensions")
endif()

install(TARGETS async2 LIBRARY DESTINATION ${EXT_INSTALL_DIR} RUNTIME DESTINATION ${EXT_INSTALL_DIR})
install(FILES sourcepawn/async2.inc sourcepawn/async2_lru.inc DESTINATION addons/sourcemod/scripting/include)
install(DIRECTORY sourcepawn/async2/ DESTINATION addons/sourcemod/scripting/include/async2 FILES_MATCHING PATTERN "*.inc")
