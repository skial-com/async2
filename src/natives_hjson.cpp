#include <cstring>
#include <cstdio>

#include "smsdk_ext.h"
#include "extension.h"
#include "data_handle.h"
#include "hjson_parse.h"
#include "natives.h"

// async2_HjsonParseString(const char[] str) -> Json
static cell_t Native_HjsonParseString(IPluginContext* pContext, const cell_t* params) {
    char* str;
    pContext->LocalToString(params[1], &str);

    DataNode* parsed = HjsonParse(str, strlen(str));
    if (!parsed)
        return 0;

    DataHandle* handle = new DataHandle(parsed);
    int id = g_handle_manager.CreateHandle(static_cast<void*>(handle), HANDLE_JSON_VALUE, pContext);
    if (id == 0) {
        delete handle;
        return 0;
    }
    return id;
}

// async2_HjsonParseFile(const char[] path) -> Json
static cell_t Native_HjsonParseFile(IPluginContext* pContext, const cell_t* params) {
    char* path;
    pContext->LocalToString(params[1], &path);

    char fullpath[PLATFORM_MAX_PATH];
    smutils->BuildPath(Path_Game, fullpath, sizeof(fullpath), "%s", path);

    FILE* f = fopen(fullpath, "rb");
    if (!f)
        return 0;

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    if (size <= 0 || size > 64 * 1024 * 1024) { // 64 MB limit
        fclose(f);
        return 0;
    }
    fseek(f, 0, SEEK_SET);

    char* buf = new char[size];
    size_t read = fread(buf, 1, size, f);
    fclose(f);

    if (static_cast<long>(read) != size) {
        delete[] buf;
        return 0;
    }

    DataNode* parsed = HjsonParse(buf, read);
    delete[] buf;

    if (!parsed)
        return 0;

    DataHandle* handle = new DataHandle(parsed);
    int id = g_handle_manager.CreateHandle(static_cast<void*>(handle), HANDLE_JSON_VALUE, pContext);
    if (id == 0) {
        delete handle;
        return 0;
    }
    return id;
}

sp_nativeinfo_t g_HjsonNatives[] = {
    {"async2_HjsonParseString", Native_HjsonParseString},
    {"async2_HjsonParseFile",   Native_HjsonParseFile},
    {nullptr,                   nullptr},
};
