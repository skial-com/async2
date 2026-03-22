cmake_minimum_required(VERSION 3.16)
project(async2_tests C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE OR CMAKE_BUILD_TYPE STREQUAL "")
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()
enable_testing()
set(ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")

# Shared sources
set(DATA_SRCS
    ${ROOT}/src/data/data_node.cpp
    ${ROOT}/src/data/data_handle.cpp
)
set(SIMDJSON ${ROOT}/third_party/simdjson/singleheader/simdjson.cpp)

# Common includes for most targets
set(COMMON_INCLUDES
    ${ROOT}/src
    ${ROOT}/src/data
    ${ROOT}/third_party/simdjson/singleheader
    ${ROOT}/third_party/robin-map/include
    ${ROOT}/third_party/xxhash
)

# JSON unit tests
add_executable(test_json_parser
    test_json_parser.cpp
    ${DATA_SRCS}
    ${SIMDJSON}
)
target_include_directories(test_json_parser PRIVATE ${COMMON_INCLUDES})
add_test(NAME json COMMAND test_json_parser)

# MessagePack tests
add_executable(test_msgpack
    test_msgpack.cpp
    ${DATA_SRCS}
    ${ROOT}/src/msgpack/msgpack_parse.cpp
    ${ROOT}/src/msgpack/msgpack_serialize.cpp
    ${SIMDJSON}
)
target_include_directories(test_msgpack PRIVATE ${COMMON_INCLUDES} ${ROOT}/src/msgpack)
add_test(NAME msgpack COMMAND test_msgpack)

# HJSON tests
add_executable(test_hjson
    test_hjson.cpp
    ${DATA_SRCS}
    ${ROOT}/src/hjson/hjson_parse.cpp
    ${SIMDJSON}
)
target_include_directories(test_hjson PRIVATE ${COMMON_INCLUDES} ${ROOT}/src/hjson)
add_test(NAME hjson COMMAND test_hjson "${CMAKE_CURRENT_SOURCE_DIR}/hjson_assets")

# DataNode benchmark (always available)
add_executable(bench_datanode
    bench_datanode.cpp
    ${ROOT}/src/data/data_node.cpp
    ${SIMDJSON}
)
target_include_directories(bench_datanode PRIVATE ${COMMON_INCLUDES})

# Benchmarks — library comparison (optional, requires jansson and yyjson in third_party/)
if(EXISTS "${ROOT}/third_party/jansson/CMakeLists.txt" AND EXISTS "${ROOT}/third_party/yyjson/src/yyjson.c")
    set(JANSSON_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(JANSSON_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(JANSSON_WITHOUT_TESTS ON CACHE BOOL "" FORCE)
    set(JANSSON_EXAMPLES OFF CACHE BOOL "" FORCE)
    add_subdirectory(${ROOT}/third_party/jansson ${CMAKE_CURRENT_BINARY_DIR}/jansson)
    add_executable(bench_compare
        bench_compare.cpp
        ${ROOT}/src/data/data_node.cpp
        ${ROOT}/third_party/yyjson/src/yyjson.c
        ${SIMDJSON}
    )
    target_include_directories(bench_compare PRIVATE
        ${COMMON_INCLUDES}
        ${ROOT}/third_party/yyjson/src
    )
    target_link_libraries(bench_compare PRIVATE jansson)
else()
    message(STATUS "Skipping bench_compare (requires jansson and yyjson in third_party/)")
endif()

# Benchmarks — map+hash comparison (optional, requires extra third_party dirs)
if(EXISTS "${ROOT}/third_party/robin-hood-hashing/src/include")
    add_executable(bench_maps
        bench_maps.cpp
        ${SIMDJSON}
    )
    target_include_directories(bench_maps PRIVATE
        ${ROOT}/src/data
        ${ROOT}/third_party/simdjson/singleheader
        ${ROOT}/third_party/robin-hood-hashing/src/include
        ${ROOT}/third_party/flat_hash_map
        ${ROOT}/third_party/emhash
        ${ROOT}/third_party/robin-map/include
        ${ROOT}/third_party/unordered_dense/include
        ${ROOT}/third_party/xxhash
    )
else()
    message(STATUS "Skipping bench_maps (requires extra third_party submodules)")
endif()
