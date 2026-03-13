// This file is intentionally separate so that touching it only recompiles
// this one translation unit. build_timestamp.h is regenerated every build.

#include <uv/version.h>
#include <simdjson.h>
#include <xxhash.h>
#include <tsl/robin_growth_policy.h>
#include "smsdk_config.h"

#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)

#include "build_timestamp.h"
const char* g_compile_time = BUILD_TIMESTAMP;
const char* g_build_info = "async2/" SMEXT_CONF_VERSION;
const char* g_deps_version =
    "libuv/" STR(UV_VERSION_MAJOR) "." STR(UV_VERSION_MINOR) "." STR(UV_VERSION_PATCH)
    " simdjson/" SIMDJSON_VERSION
    " xxhash/" STR(XXH_VERSION_MAJOR) "." STR(XXH_VERSION_MINOR) "." STR(XXH_VERSION_RELEASE)
    " robin-map/" STR(TSL_RH_VERSION_MAJOR) "." STR(TSL_RH_VERSION_MINOR) "." STR(TSL_RH_VERSION_PATCH);
