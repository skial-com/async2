#include <cstring>
#include <string>

#include "smsdk_ext.h"
#include "extension.h"
#include "http_request.h"
#include "data_handle.h"
#include "data/data_iterator.h"
#include "natives.h"

// Path error state (game thread only)
static bool g_path_failed = false;
static std::string g_path_error;

// Int64 read/write via memcpy — matches SourcePawn's int64 memory layout
// (two contiguous cells, low first). Works with int[2] today and native
// int64 in the future with no C++ changes.
static int64_t ReadInt64(cell_t* addr) {
    int64_t val;
    memcpy(&val, addr, sizeof(val));
    return val;
}

static void WriteInt64(cell_t* addr, int64_t val) {
    memcpy(addr, &val, sizeof(val));
}

static const char* DataTypeName(DataType type) {
    switch (type) {
        case DataType::Null:   return "null";
        case DataType::Bool:   return "bool";
        case DataType::Int:    return "int";
        case DataType::Float:  return "float";
        case DataType::String: return "string";
        case DataType::Array:  return "array";
        case DataType::Object: return "object";
        case DataType::IntMap:  return "intobject";
        case DataType::Binary: return "binary";
    }
    return "unknown";
}

// Max key length when reading vararg path elements. Caps the read if an integer
// is accidentally passed where a string key is expected (SourcePawn any... erases
// type info, so an int's bytes get interpreted as a string, potentially reading
// far into the plugin's memory). 256 is generous for real JSON keys.
static constexpr size_t kMaxPathKeyLen = 256;

// Rebuild the path string from the varargs on failure only. Reads each arg the
// same way the main loop does (string for objects, int for arrays) up to the
// failing step, so the cost is only paid on error.
static std::string BuildPathString(IPluginContext* pContext, DataNode* root,
                                   const cell_t* params, int start, int steps) {
    std::string path;
    DataNode* node = root;
    char key_buf[kMaxPathKeyLen + 1];

    for (int i = 0; i < steps; i++) {
        int idx = start + i;

        if (node->type == DataType::Object) {
            char* key;
            if (pContext->LocalToString(params[idx], &key) != SP_ERROR_NONE)
                break;
            strncpy(key_buf, key, kMaxPathKeyLen);
            key_buf[kMaxPathKeyLen] = '\0';
            if (!path.empty()) path += ".";
            path += key_buf;
            DataNode* child = node->ObjFind(key_buf);
            if (!child) break;
            node = child;
        } else if (node->type == DataType::IntMap) {
            cell_t* phys_addr;
            if (pContext->LocalToPhysAddr(params[idx], &phys_addr) != SP_ERROR_NONE)
                break;
            int64_t key = static_cast<int64_t>(*phys_addr);
            if (!path.empty()) path += ".";
            path += std::to_string(key);
            DataNode* child = node->IntMapFind(key);
            if (!child) break;
            node = child;
        } else if (node->type == DataType::Array) {
            cell_t* phys_addr;
            if (pContext->LocalToPhysAddr(params[idx], &phys_addr) != SP_ERROR_NONE)
                break;
            int index = static_cast<int>(*phys_addr);
            path += "[";
            path += std::to_string(index);
            path += "]";
            if (index < 0 || static_cast<size_t>(index) >= node->Arr().size()) break;
            node = node->Arr()[index];
        } else {
            break;
        }
    }

    return path;
}

static void SetPathError(IPluginContext* pContext, DataNode* root,
                         const cell_t* params, int start, int step,
                         const char* reason) {
    g_path_failed = true;
    std::string path = BuildPathString(pContext, root, params, start, step);
    if (path.empty())
        g_path_error = "<root> — ";
    else
        g_path_error = path + " — ";
    g_path_error += reason;
}

// Walk a vararg path through nested data. Each step infers string key vs int index
// from the current node's type. Sets g_path_failed/g_path_error on failure.
//
// Note: SourcePawn "any ..." varargs erase type information — all arguments are
// passed as local addresses. When the current node is an object, the argument is
// read as a string key; when it's an array, as an integer index. If the caller
// passes the wrong type (e.g. an integer for an object key), the bytes will be
// misinterpreted. The key read is capped at kMaxPathKeyLen to limit garbage reads.
static DataNode* ResolveJsonPath(IPluginContext* pContext, DataNode* node,
                                 const cell_t* params, int start, int count) {
    g_path_failed = false;
    g_path_error.clear();

    if (!node) {
        g_path_failed = true;
        g_path_error = "<root> — null node";
        return nullptr;
    }

    DataNode* root = node;
    char key_buf[kMaxPathKeyLen + 1];

    for (int i = 0; i < count; i++) {
        int idx = start + i;

        if (node->type == DataType::Object) {
            char* key;
            if (pContext->LocalToString(params[idx], &key) != SP_ERROR_NONE) {
                SetPathError(pContext, root, params, start, i, "expected string key for object");
                return nullptr;
            }

            // Cap the key length to prevent reading far into plugin memory
            // when an integer is accidentally passed as a path element.
            strncpy(key_buf, key, kMaxPathKeyLen);
            key_buf[kMaxPathKeyLen] = '\0';

            DataNode* child = node->ObjFind(key_buf);
            if (!child) {
                SetPathError(pContext, root, params, start, i + 1, "key not found");
                return nullptr;
            }
            node = child;
        } else if (node->type == DataType::IntMap) {
            cell_t* phys_addr;
            if (pContext->LocalToPhysAddr(params[idx], &phys_addr) != SP_ERROR_NONE) {
                SetPathError(pContext, root, params, start, i, "expected integer key for intmap");
                return nullptr;
            }
            int64_t key = static_cast<int64_t>(*phys_addr);
            DataNode* child = node->IntMapFind(key);
            if (!child) {
                SetPathError(pContext, root, params, start, i + 1, "key not found");
                return nullptr;
            }
            node = child;
        } else if (node->type == DataType::Array) {
            cell_t* phys_addr;
            if (pContext->LocalToPhysAddr(params[idx], &phys_addr) != SP_ERROR_NONE) {
                SetPathError(pContext, root, params, start, i, "expected integer index for array");
                return nullptr;
            }
            int index = static_cast<int>(*phys_addr);

            if (index < 0 || static_cast<size_t>(index) >= node->Arr().size()) {
                char reason[64];
                snprintf(reason, sizeof(reason), "index %d out of bounds (length %zu)",
                         index, node->Arr().size());
                SetPathError(pContext, root, params, start, i + 1, reason);
                return nullptr;
            }
            node = node->Arr()[index];
        } else {
            char reason[64];
            snprintf(reason, sizeof(reason), "cannot index into %s", DataTypeName(node->type));
            SetPathError(pContext, root, params, start, i, reason);
            return nullptr;
        }
    }

    return node;
}

#define GET_JSON_HANDLE() \
    DataHandle* json = g_handle_manager.GetDataHandle(params[1]); \
    if (!json) \
        return 0;

// Wrap a DataNode* into a new handle with an incremented refcount.
static cell_t WrapChildNode(IPluginContext* pContext, DataNode* val) {
    if (!val) return 0;
    val->Incref();
    DataHandle* child = new DataHandle(val);
    int handle = g_handle_manager.CreateHandle(static_cast<void*>(child), HANDLE_JSON_VALUE, pContext);
    if (handle == 0) { delete child; return 0; }
    return handle;
}

// Read an int64 from a SourcePawn int[2] parameter.
static int64_t ReadInt64Param(IPluginContext* pContext, cell_t param) {
    cell_t* addr;
    pContext->LocalToPhysAddr(param, &addr);
    return ReadInt64(addr);
}

static cell_t Native_JsonParse(IPluginContext* pContext, const cell_t* params) {
    HttpRequest* request = g_handle_manager.GetHttpRequest(params[1]);
    if (!request || request->in_event_thread)
        return 0;

    DataNode* node = nullptr;
    if (request->response_node) {
        // Pre-parsed on event thread — take ownership (one-shot)
        node = request->response_node;
        request->response_node = nullptr;
    } else if (!request->response_body.empty()) {
        // No pre-parse (flag not set, or parse failed) — parse on game thread
        node = DataParseJson(request->response_body.data(), request->response_body.size());
    }

    if (!node)
        return 0;

    DataHandle* json = new DataHandle(node);
    int handle = g_handle_manager.CreateHandle(static_cast<void*>(json), HANDLE_JSON_VALUE, pContext);
    if (handle == 0) {
        delete json;
        return 0;
    }
    return handle;
}

static cell_t Native_JsonParseString(IPluginContext* pContext, const cell_t* params) {
    char* str;
    pContext->LocalToString(params[1], &str);

    DataHandle* json = DataHandle::ParseString(str);
    if (!json)
        return 0;

    int handle = g_handle_manager.CreateHandle(static_cast<void*>(json), HANDLE_JSON_VALUE, pContext);
    if (handle == 0) {
        delete json;
        return 0;
    }
    return handle;
}

// async2_JsonParseFile(const char[] path) -> Json
static cell_t Native_JsonParseFile(IPluginContext* pContext, const cell_t* params) {
    char* path;
    pContext->LocalToString(params[1], &path);

    char fullpath[PLATFORM_MAX_PATH];
    smutils->BuildPath(Path_Game, fullpath, sizeof(fullpath), "%s", path);

    FILE* f = fopen(fullpath, "rb");
    if (!f)
        return 0;

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    if (size <= 0 || size > 64 * 1024 * 1024) {
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

    DataHandle* json = DataHandle::Parse(buf, read);
    delete[] buf;

    if (!json)
        return 0;

    int handle = g_handle_manager.CreateHandle(static_cast<void*>(json), HANDLE_JSON_VALUE, pContext);
    if (handle == 0) {
        delete json;
        return 0;
    }
    return handle;
}

static cell_t Native_JsonCreateObject(IPluginContext* pContext, const cell_t* params) {
    DataHandle* json = DataHandle::CreateObject();
    if (!json)
        return 0;

    int handle = g_handle_manager.CreateHandle(static_cast<void*>(json), HANDLE_JSON_VALUE, pContext);
    if (handle == 0) {
        delete json;
        return 0;
    }
    return handle;
}

static cell_t Native_JsonCreateArray(IPluginContext* pContext, const cell_t* params) {
    DataHandle* json = DataHandle::CreateArray();
    if (!json)
        return 0;

    int handle = g_handle_manager.CreateHandle(static_cast<void*>(json), HANDLE_JSON_VALUE, pContext);
    if (handle == 0) {
        delete json;
        return 0;
    }
    return handle;
}

static cell_t Native_JsonClose(IPluginContext* pContext, const cell_t* params) {
    DataHandle* json = g_handle_manager.GetDataHandle(params[1]);
    if (!json)
        return 0;
    g_handle_manager.FreeHandle(params[1]);
    return 0;
}

static cell_t Native_JsonGetType(IPluginContext* pContext, const cell_t* params) {
    GET_JSON_HANDLE()
    return json->GetType();
}

static cell_t Native_JsonObjectSize(IPluginContext* pContext, const cell_t* params) {
    GET_JSON_HANDLE()
    return json->ObjectSize();
}

static cell_t Native_JsonArrayLength(IPluginContext* pContext, const cell_t* params) {
    GET_JSON_HANDLE()
    return json->ArrayLength();
}

static cell_t Native_JsonHasKey(IPluginContext* pContext, const cell_t* params) {
    GET_JSON_HANDLE()
    char* key;
    pContext->LocalToString(params[2], &key);
    return json->HasKey(key) ? 1 : 0;
}

static cell_t Native_JsonGetString(IPluginContext* pContext, const cell_t* params) {
    GET_JSON_HANDLE()
    char* key;
    pContext->LocalToString(params[2], &key);
    const char* val = json->GetString(key);
    pContext->StringToLocal(params[3], params[4], val);
    return 0;
}

static cell_t Native_JsonGetInt(IPluginContext* pContext, const cell_t* params) {
    GET_JSON_HANDLE()
    char* key;
    pContext->LocalToString(params[2], &key);
    return static_cast<cell_t>(json->GetInt(key));
}

static cell_t Native_JsonGetFloat(IPluginContext* pContext, const cell_t* params) {
    GET_JSON_HANDLE()
    char* key;
    pContext->LocalToString(params[2], &key);
    float val = static_cast<float>(json->GetFloat(key));
    return sp_ftoc(val);
}

static cell_t Native_JsonGetBool(IPluginContext* pContext, const cell_t* params) {
    GET_JSON_HANDLE()
    char* key;
    pContext->LocalToString(params[2], &key);
    return json->GetBool(key) ? 1 : 0;
}

static cell_t Native_JsonGetInt64(IPluginContext* pContext, const cell_t* params) {
    GET_JSON_HANDLE()
    char* key;
    pContext->LocalToString(params[2], &key);
    int64_t val = json->GetInt(key);
    cell_t* out;
    pContext->LocalToPhysAddr(params[3], &out);
    WriteInt64(out, val);
    return 0;
}

static cell_t Native_JsonGetObject(IPluginContext* pContext, const cell_t* params) {
    GET_JSON_HANDLE()
    char* key;
    pContext->LocalToString(params[2], &key);
    DataNode* val = json->GetObjectNode(key);
    return WrapChildNode(pContext,val);
}

static cell_t Native_JsonGetArray(IPluginContext* pContext, const cell_t* params) {
    GET_JSON_HANDLE()
    char* key;
    pContext->LocalToString(params[2], &key);
    DataNode* val = json->GetArrayNode(key);
    return WrapChildNode(pContext,val);
}

// Array getters
static cell_t Native_JsonArrayGetInt64(IPluginContext* pContext, const cell_t* params) {
    GET_JSON_HANDLE()
    int64_t val = json->ArrayGetInt(params[2]);
    cell_t* out;
    pContext->LocalToPhysAddr(params[3], &out);
    WriteInt64(out, val);
    return 0;
}

static cell_t Native_JsonArrayGetString(IPluginContext* pContext, const cell_t* params) {
    GET_JSON_HANDLE()
    int index = params[2];
    const char* val = json->ArrayGetString(index);
    pContext->StringToLocal(params[3], params[4], val);
    return 0;
}

static cell_t Native_JsonArrayGetInt(IPluginContext* pContext, const cell_t* params) {
    GET_JSON_HANDLE()
    return static_cast<cell_t>(json->ArrayGetInt(params[2]));
}

static cell_t Native_JsonArrayGetFloat(IPluginContext* pContext, const cell_t* params) {
    GET_JSON_HANDLE()
    float val = static_cast<float>(json->ArrayGetFloat(params[2]));
    return sp_ftoc(val);
}

static cell_t Native_JsonArrayGetBool(IPluginContext* pContext, const cell_t* params) {
    GET_JSON_HANDLE()
    return json->ArrayGetBool(params[2]) ? 1 : 0;
}

static cell_t Native_JsonArrayGetObject(IPluginContext* pContext, const cell_t* params) {
    GET_JSON_HANDLE()
    DataNode* val = json->ArrayGetNode(params[2]);
    return WrapChildNode(pContext,val);
}

// Iterator creation
static cell_t Native_ObjectIterCreate(IPluginContext* pContext, const cell_t* params) {
    DataHandle* json = g_handle_manager.GetDataHandle(params[1]);
    if (!json || !json->node || json->node->type != DataType::Object)
        return 0;
    DataIterator* iter = DataIterator::CreateObject(json->node);
    if (!iter) return 0;
    int handle = g_handle_manager.CreateHandle(static_cast<void*>(iter), HANDLE_ITERATOR, pContext);
    if (handle == 0) { delete iter; return 0; }
    return handle;
}

static cell_t Native_IntMapIterCreate(IPluginContext* pContext, const cell_t* params) {
    DataHandle* json = g_handle_manager.GetDataHandle(params[1]);
    if (!json || !json->node || json->node->type != DataType::IntMap)
        return 0;
    DataIterator* iter = DataIterator::CreateIntMap(json->node);
    if (!iter) return 0;
    int handle = g_handle_manager.CreateHandle(static_cast<void*>(iter), HANDLE_ITERATOR, pContext);
    if (handle == 0) { delete iter; return 0; }
    return handle;
}

static cell_t Native_IterNext(IPluginContext* pContext, const cell_t* params) {
    DataIterator* iter = g_handle_manager.GetDataIterator(params[1]);
    if (!iter || iter->Type() != IteratorType::Object) return 0;
    if (!iter->Next()) return 0;
    pContext->StringToLocal(params[2], params[3], iter->ObjectKey());
    return 1;
}

static cell_t Native_IntMapIterNext(IPluginContext* pContext, const cell_t* params) {
    DataIterator* iter = g_handle_manager.GetDataIterator(params[1]);
    if (!iter || iter->Type() != IteratorType::IntMap) return 0;
    if (!iter->Next()) return 0;
    cell_t* out;
    pContext->LocalToPhysAddr(params[2], &out);
    *out = static_cast<cell_t>(iter->IntMapKey());
    return 1;
}

static cell_t Native_IntMapIterNext64(IPluginContext* pContext, const cell_t* params) {
    DataIterator* iter = g_handle_manager.GetDataIterator(params[1]);
    if (!iter || iter->Type() != IteratorType::IntMap) return 0;
    if (!iter->Next()) return 0;
    cell_t* out;
    pContext->LocalToPhysAddr(params[2], &out);
    WriteInt64(out, iter->IntMapKey());
    return 1;
}

static cell_t Native_IterClose(IPluginContext* pContext, const cell_t* params) {
    DataIterator* iter = g_handle_manager.GetDataIterator(params[1]);
    if (!iter) return 0;
    g_handle_manager.FreeHandle(params[1]);
    return 0;
}

// Iterator value access
static cell_t Native_IterGetType(IPluginContext* pContext, const cell_t* params) {
    DataIterator* iter = g_handle_manager.GetDataIterator(params[1]);
    if (!iter || !iter->Value()) return 0;
    return NodeToType(iter->Value());
}

static cell_t Native_IterGetInt(IPluginContext* pContext, const cell_t* params) {
    DataIterator* iter = g_handle_manager.GetDataIterator(params[1]);
    if (!iter || !iter->Value()) return 0;
    DataNode* v = iter->Value();
    if (v->type == DataType::Int) return static_cast<cell_t>(v->int_val);
    if (v->type == DataType::Float) return static_cast<cell_t>(v->float_val);
    return 0;
}

static cell_t Native_IterGetInt64(IPluginContext* pContext, const cell_t* params) {
    DataIterator* iter = g_handle_manager.GetDataIterator(params[1]);
    if (!iter || !iter->Value()) return 0;
    DataNode* v = iter->Value();
    int64_t val = 0;
    if (v->type == DataType::Int) val = v->int_val;
    else if (v->type == DataType::Float) val = static_cast<int64_t>(v->float_val);
    cell_t* out;
    pContext->LocalToPhysAddr(params[2], &out);
    WriteInt64(out, val);
    return 0;
}

static cell_t Native_IterGetFloat(IPluginContext* pContext, const cell_t* params) {
    DataIterator* iter = g_handle_manager.GetDataIterator(params[1]);
    if (!iter || !iter->Value()) return 0;
    DataNode* v = iter->Value();
    float result = 0.0f;
    if (v->type == DataType::Float) result = static_cast<float>(v->float_val);
    else if (v->type == DataType::Int) result = static_cast<float>(v->int_val);
    return sp_ftoc(result);
}

static cell_t Native_IterGetBool(IPluginContext* pContext, const cell_t* params) {
    DataIterator* iter = g_handle_manager.GetDataIterator(params[1]);
    if (!iter || !iter->Value()) return 0;
    DataNode* v = iter->Value();
    if (v->type == DataType::Bool) return v->bool_val ? 1 : 0;
    return 0;
}

static cell_t Native_IterGetString(IPluginContext* pContext, const cell_t* params) {
    DataIterator* iter = g_handle_manager.GetDataIterator(params[1]);
    if (!iter || !iter->Value()) {
        pContext->StringToLocal(params[2], params[3], "");
        return 0;
    }
    DataNode* v = iter->Value();
    if (v->type == DataType::String) {
        pContext->StringToLocal(params[2], params[3], v->Str().c_str());
        return 0;
    }
    pContext->StringToLocal(params[2], params[3], "");
    return 0;
}

static cell_t Native_IterGetObject(IPluginContext* pContext, const cell_t* params) {
    DataIterator* iter = g_handle_manager.GetDataIterator(params[1]);
    if (!iter || !iter->Value()) return 0;
    DataNode* v = iter->Value();
    if (v->type != DataType::Object && v->type != DataType::Array &&
        v->type != DataType::IntMap) return 0;
    return WrapChildNode(pContext, v);
}

// Object setters
static cell_t Native_JsonSetString(IPluginContext* pContext, const cell_t* params) {
    GET_JSON_HANDLE()
    char* key;
    char* value;
    pContext->LocalToString(params[2], &key);
    pContext->LocalToString(params[3], &value);
    json->SetString(key, value);
    return 0;
}

static cell_t Native_JsonSetInt(IPluginContext* pContext, const cell_t* params) {
    GET_JSON_HANDLE()
    char* key;
    pContext->LocalToString(params[2], &key);
    json->SetInt(key, static_cast<int64_t>(params[3]));
    return 0;
}

static cell_t Native_JsonSetInt64(IPluginContext* pContext, const cell_t* params) {
    GET_JSON_HANDLE()
    char* key;
    pContext->LocalToString(params[2], &key);
    cell_t* in;
    pContext->LocalToPhysAddr(params[3], &in);
    json->SetInt(key, ReadInt64(in));
    return 0;
}

static cell_t Native_JsonSetFloat(IPluginContext* pContext, const cell_t* params) {
    GET_JSON_HANDLE()
    char* key;
    pContext->LocalToString(params[2], &key);
    json->SetFloat(key, static_cast<double>(sp_ctof(params[3])));
    return 0;
}

static cell_t Native_JsonSetBool(IPluginContext* pContext, const cell_t* params) {
    GET_JSON_HANDLE()
    char* key;
    pContext->LocalToString(params[2], &key);
    json->SetBool(key, params[3] != 0);
    return 0;
}

static cell_t Native_JsonSetNull(IPluginContext* pContext, const cell_t* params) {
    GET_JSON_HANDLE()
    char* key;
    pContext->LocalToString(params[2], &key);
    json->SetNull(key);
    return 0;
}

static cell_t Native_JsonSetObject(IPluginContext* pContext, const cell_t* params) {
    GET_JSON_HANDLE()
    char* key;
    pContext->LocalToString(params[2], &key);

    DataHandle* child = g_handle_manager.GetDataHandle(params[3]);
    if (!child)
        return 0;
    if (json->node == child->node)
        return pContext->ThrowNativeError("Cannot SetObject with self as child");
    child->node->Incref();
    json->node->ObjInsert(key, child->node);
    g_handle_manager.FreeHandle(params[3]);
    return 0;
}

// Object mutation
static cell_t Native_JsonRemoveKey(IPluginContext* pContext, const cell_t* params) {
    GET_JSON_HANDLE()
    char* key;
    pContext->LocalToString(params[2], &key);
    return json->RemoveKey(key) ? 1 : 0;
}

static cell_t Native_JsonObjectClear(IPluginContext* pContext, const cell_t* params) {
    GET_JSON_HANDLE()
    json->ObjectClear();
    return 0;
}

static cell_t Native_JsonObjectMerge(IPluginContext* pContext, const cell_t* params) {
    GET_JSON_HANDLE()
    DataHandle* other = g_handle_manager.GetDataHandle(params[2]);
    if (!other)
        return 0;
    if (json->node == other->node)
        return 0;  // self-merge is a no-op
    json->ObjectMerge(other->node, params[3] != 0);
    return 0;
}

// Array mutation
static cell_t Native_JsonArrayRemove(IPluginContext* pContext, const cell_t* params) {
    GET_JSON_HANDLE()
    return json->ArrayRemove(params[2]) ? 1 : 0;
}

static cell_t Native_JsonArraySetString(IPluginContext* pContext, const cell_t* params) {
    GET_JSON_HANDLE()
    char* value;
    pContext->LocalToString(params[3], &value);
    json->ArraySetString(params[2], value);
    return 0;
}

static cell_t Native_JsonArraySetInt(IPluginContext* pContext, const cell_t* params) {
    GET_JSON_HANDLE()
    json->ArraySetInt(params[2], static_cast<int64_t>(params[3]));
    return 0;
}

static cell_t Native_JsonArraySetInt64(IPluginContext* pContext, const cell_t* params) {
    GET_JSON_HANDLE()
    cell_t* in;
    pContext->LocalToPhysAddr(params[3], &in);
    json->ArraySetInt(params[2], ReadInt64(in));
    return 0;
}

static cell_t Native_JsonArraySetFloat(IPluginContext* pContext, const cell_t* params) {
    GET_JSON_HANDLE()
    json->ArraySetFloat(params[2], static_cast<double>(sp_ctof(params[3])));
    return 0;
}

static cell_t Native_JsonArraySetBool(IPluginContext* pContext, const cell_t* params) {
    GET_JSON_HANDLE()
    json->ArraySetBool(params[2], params[3] != 0);
    return 0;
}

static cell_t Native_JsonArraySetNull(IPluginContext* pContext, const cell_t* params) {
    GET_JSON_HANDLE()
    json->ArraySetNull(params[2]);
    return 0;
}

static cell_t Native_JsonArraySetObject(IPluginContext* pContext, const cell_t* params) {
    GET_JSON_HANDLE()
    DataHandle* child = g_handle_manager.GetDataHandle(params[3]);
    if (!child)
        return 0;
    if (json->node == child->node)
        return pContext->ThrowNativeError("Cannot ArraySetObject with self as child");
    child->node->Incref();
    json->node->ArrSet(params[2], child->node);
    g_handle_manager.FreeHandle(params[3]);
    return 0;
}

static cell_t Native_JsonArrayClear(IPluginContext* pContext, const cell_t* params) {
    GET_JSON_HANDLE()
    json->ArrayClear();
    return 0;
}

static cell_t Native_JsonArrayExtend(IPluginContext* pContext, const cell_t* params) {
    GET_JSON_HANDLE()
    DataHandle* other = g_handle_manager.GetDataHandle(params[2]);
    if (!other)
        return 0;
    if (json->node == other->node)
        return 0;  // self-extend is a no-op
    json->ArrayExtend(other->node);
    return 0;
}

// Array append
static cell_t Native_JsonArrayAppendString(IPluginContext* pContext, const cell_t* params) {
    GET_JSON_HANDLE()
    char* value;
    pContext->LocalToString(params[2], &value);
    json->ArrayAppendString(value);
    return 0;
}

static cell_t Native_JsonArrayAppendInt(IPluginContext* pContext, const cell_t* params) {
    GET_JSON_HANDLE()
    json->ArrayAppendInt(static_cast<int64_t>(params[2]));
    return 0;
}

static cell_t Native_JsonArrayAppendInt64(IPluginContext* pContext, const cell_t* params) {
    GET_JSON_HANDLE()
    cell_t* in;
    pContext->LocalToPhysAddr(params[2], &in);
    json->ArrayAppendInt(ReadInt64(in));
    return 0;
}

static cell_t Native_JsonArrayAppendFloat(IPluginContext* pContext, const cell_t* params) {
    GET_JSON_HANDLE()
    json->ArrayAppendFloat(static_cast<double>(sp_ctof(params[2])));
    return 0;
}

static cell_t Native_JsonArrayAppendBool(IPluginContext* pContext, const cell_t* params) {
    GET_JSON_HANDLE()
    json->ArrayAppendBool(params[2] != 0);
    return 0;
}

static cell_t Native_JsonArrayAppendNull(IPluginContext* pContext, const cell_t* params) {
    GET_JSON_HANDLE()
    json->ArrayAppendNull();
    return 0;
}

static cell_t Native_JsonArrayAppendObject(IPluginContext* pContext, const cell_t* params) {
    GET_JSON_HANDLE()
    DataHandle* child = g_handle_manager.GetDataHandle(params[2]);
    if (!child)
        return 0;
    if (json->node == child->node)
        return pContext->ThrowNativeError("Cannot ArrayAppendObject with self as child");
    child->node->Incref();
    json->node->Arr().push_back(child->node);
    g_handle_manager.FreeHandle(params[2]);
    return 0;
}

static cell_t Native_JsonEquals(IPluginContext* pContext, const cell_t* params) {
    GET_JSON_HANDLE()
    DataHandle* other = g_handle_manager.GetDataHandle(params[2]);
    if (!other)
        return 0;
    return json->node->Equals(other->node) ? 1 : 0;
}

static cell_t Native_JsonRef(IPluginContext* pContext, const cell_t* params) {
    GET_JSON_HANDLE()
    return WrapChildNode(pContext,json->node);
}

static cell_t Native_JsonCopy(IPluginContext* pContext, const cell_t* params) {
    GET_JSON_HANDLE()
    DataNode* copy = json->node->DeepCopy();
    if (!copy)
        return 0;

    DataHandle* handle = new DataHandle(copy);
    int id = g_handle_manager.CreateHandle(static_cast<void*>(handle), HANDLE_JSON_VALUE, pContext);
    if (id == 0) {
        delete handle;
        return 0;
    }
    return id;
}

static cell_t Native_JsonSerialize(IPluginContext* pContext, const cell_t* params) {
    GET_JSON_HANDLE()
    char* buffer;
    pContext->LocalToString(params[2], &buffer);
    int maxlen = params[3];
    bool pretty = (params[0] >= 4) ? (params[4] != 0) : false;
    if (!json->Serialize(buffer, maxlen, pretty))
        return 0;
    return 1;
}

// Path getters — vararg traversal, silent failure
static cell_t Native_JsonPathGetInt(IPluginContext* pContext, const cell_t* params) {
    GET_JSON_HANDLE()
    int path_count = params[0] - 1;
    DataNode* target = ResolveJsonPath(pContext, json->node, params, 2, path_count);
    if (!target) return 0;
    if (target->type == DataType::Int) return static_cast<cell_t>(target->int_val);
    if (target->type == DataType::Float) return static_cast<cell_t>(target->float_val);
    return 0;
}

// params: handle, val[2], path...
static cell_t Native_JsonPathGetInt64(IPluginContext* pContext, const cell_t* params) {
    GET_JSON_HANDLE()
    int path_count = params[0] - 2;
    DataNode* target = ResolveJsonPath(pContext, json->node, params, 3, path_count);
    int64_t val = 0;
    if (target) {
        if (target->type == DataType::Int) val = target->int_val;
        else if (target->type == DataType::Float) val = static_cast<int64_t>(target->float_val);
    }
    cell_t* out;
    pContext->LocalToPhysAddr(params[2], &out);
    WriteInt64(out, val);
    return 0;
}

static cell_t Native_JsonPathGetFloat(IPluginContext* pContext, const cell_t* params) {
    GET_JSON_HANDLE()
    int path_count = params[0] - 1;
    DataNode* target = ResolveJsonPath(pContext, json->node, params, 2, path_count);
    if (!target) return sp_ftoc(0.0f);
    float val = 0.0f;
    if (target->type == DataType::Float) val = static_cast<float>(target->float_val);
    else if (target->type == DataType::Int) val = static_cast<float>(target->int_val);
    return sp_ftoc(val);
}

static cell_t Native_JsonPathGetBool(IPluginContext* pContext, const cell_t* params) {
    GET_JSON_HANDLE()
    int path_count = params[0] - 1;
    DataNode* target = ResolveJsonPath(pContext, json->node, params, 2, path_count);
    if (!target) return 0;
    if (target->type == DataType::Bool) return target->bool_val ? 1 : 0;
    return 0;
}

// params: handle, buf, maxlen, path...
static cell_t Native_JsonPathGetString(IPluginContext* pContext, const cell_t* params) {
    GET_JSON_HANDLE()
    int path_count = params[0] - 3;
    DataNode* target = ResolveJsonPath(pContext, json->node, params, 4, path_count);
    if (!target || target->type != DataType::String) {
        pContext->StringToLocal(params[2], params[3], "");
        return 0;
    }
    pContext->StringToLocal(params[2], params[3], target->Str().c_str());
    return 0;
}

static cell_t Native_JsonPathGet(IPluginContext* pContext, const cell_t* params) {
    GET_JSON_HANDLE()
    int path_count = params[0] - 1;
    DataNode* target = ResolveJsonPath(pContext, json->node, params, 2, path_count);
    return WrapChildNode(pContext,target);
}

static cell_t Native_JsonPathGetType(IPluginContext* pContext, const cell_t* params) {
    GET_JSON_HANDLE()
    int path_count = params[0] - 1;
    DataNode* target = ResolveJsonPath(pContext, json->node, params, 2, path_count);
    if (!target) return JSON_TYPE_NONE;
    switch (target->type) {
        case DataType::Null:   return JSON_TYPE_NULL;
        case DataType::Bool:   return JSON_TYPE_BOOL;
        case DataType::Int:
        case DataType::Float:  return JSON_TYPE_NUMBER;
        case DataType::String: return JSON_TYPE_STRING;
        case DataType::Array:  return JSON_TYPE_ARRAY;
        case DataType::Object: return JSON_TYPE_OBJECT;
        case DataType::IntMap: return JSON_TYPE_INTOBJECT;
        case DataType::Binary: return JSON_TYPE_BINARY;
    }
    return JSON_TYPE_NONE;
}

static cell_t Native_JsonPathFailed(IPluginContext* pContext, const cell_t* params) {
    return g_path_failed ? 1 : 0;
}

static cell_t Native_JsonPathError(IPluginContext* pContext, const cell_t* params) {
    pContext->StringToLocal(params[1], params[2], g_path_error.c_str());
    return 0;
}

// async2_JsonMemorySize(Json handle) -> int (estimated bytes)
static cell_t Native_JsonMemorySize(IPluginContext* pContext, const cell_t* params) {
    GET_JSON_HANDLE()
    if (!json->node)
        return 0;
    return static_cast<cell_t>(json->node->EstimateBytes());
}

// async2_JsonPoolStats(&total, &in_use, &block_size) -> void
static cell_t Native_JsonPoolStats(IPluginContext* pContext, const cell_t* params) {
    size_t total, free_blocks, block_size;
    DataPoolStats(total, free_blocks, block_size);

    cell_t* addr;
    pContext->LocalToPhysAddr(params[1], &addr);
    *addr = static_cast<cell_t>(total);
    pContext->LocalToPhysAddr(params[2], &addr);
    *addr = static_cast<cell_t>(total - free_blocks);
    pContext->LocalToPhysAddr(params[3], &addr);
    *addr = static_cast<cell_t>(block_size);
    return 0;
}

// ============================================================================
// IntMap natives
// ============================================================================

static cell_t Native_IntMapCreate(IPluginContext* pContext, const cell_t* params) {
    DataHandle* json = DataHandle::CreateIntMap();
    if (!json)
        return 0;

    int handle = g_handle_manager.CreateHandle(static_cast<void*>(json), HANDLE_JSON_VALUE, pContext);
    if (handle == 0) {
        delete json;
        return 0;
    }
    return handle;
}

// 32-bit key getters
static cell_t Native_IntMapGetString(IPluginContext* pContext, const cell_t* params) {
    GET_JSON_HANDLE()
    int64_t key = static_cast<int64_t>(params[2]);
    const char* val = json->IntMapGetString(key);
    pContext->StringToLocal(params[3], params[4], val);
    return 0;
}

static cell_t Native_IntMapGetInt(IPluginContext* pContext, const cell_t* params) {
    GET_JSON_HANDLE()
    int64_t key = static_cast<int64_t>(params[2]);
    return static_cast<cell_t>(json->IntMapGetInt(key));
}

static cell_t Native_IntMapGetFloat(IPluginContext* pContext, const cell_t* params) {
    GET_JSON_HANDLE()
    int64_t key = static_cast<int64_t>(params[2]);
    float val = static_cast<float>(json->IntMapGetFloat(key));
    return sp_ftoc(val);
}

static cell_t Native_IntMapGetBool(IPluginContext* pContext, const cell_t* params) {
    GET_JSON_HANDLE()
    int64_t key = static_cast<int64_t>(params[2]);
    return json->IntMapGetBool(key) ? 1 : 0;
}

static cell_t Native_IntMapGetObject(IPluginContext* pContext, const cell_t* params) {
    GET_JSON_HANDLE()
    int64_t key = static_cast<int64_t>(params[2]);
    DataNode* val = json->IntMapGetObjectNode(key);
    return WrapChildNode(pContext,val);
}

static cell_t Native_IntMapGetArray(IPluginContext* pContext, const cell_t* params) {
    GET_JSON_HANDLE()
    int64_t key = static_cast<int64_t>(params[2]);
    DataNode* val = json->IntMapGetArrayNode(key);
    return WrapChildNode(pContext,val);
}

// 64-bit key getters
static cell_t Native_IntMapGetString64(IPluginContext* pContext, const cell_t* params) {
    GET_JSON_HANDLE()
    int64_t key = ReadInt64Param(pContext, params[2]);
    const char* val = json->IntMapGetString(key);
    pContext->StringToLocal(params[3], params[4], val);
    return 0;
}

static cell_t Native_IntMapGetInt64Key(IPluginContext* pContext, const cell_t* params) {
    GET_JSON_HANDLE()
    int64_t key = ReadInt64Param(pContext, params[2]);
    return static_cast<cell_t>(json->IntMapGetInt(key));
}

static cell_t Native_IntMapGetInt64KeyValue(IPluginContext* pContext, const cell_t* params) {
    GET_JSON_HANDLE()
    int64_t key = ReadInt64Param(pContext, params[2]);
    int64_t val = json->IntMapGetInt(key);
    cell_t* out;
    pContext->LocalToPhysAddr(params[3], &out);
    WriteInt64(out, val);
    return 0;
}

static cell_t Native_IntMapGetFloat64(IPluginContext* pContext, const cell_t* params) {
    GET_JSON_HANDLE()
    int64_t key = ReadInt64Param(pContext, params[2]);
    float val = static_cast<float>(json->IntMapGetFloat(key));
    return sp_ftoc(val);
}

static cell_t Native_IntMapGetBool64(IPluginContext* pContext, const cell_t* params) {
    GET_JSON_HANDLE()
    int64_t key = ReadInt64Param(pContext, params[2]);
    return json->IntMapGetBool(key) ? 1 : 0;
}

static cell_t Native_IntMapGetObject64(IPluginContext* pContext, const cell_t* params) {
    GET_JSON_HANDLE()
    int64_t key = ReadInt64Param(pContext, params[2]);
    DataNode* val = json->IntMapGetObjectNode(key);
    return WrapChildNode(pContext,val);
}

static cell_t Native_IntMapGetArray64(IPluginContext* pContext, const cell_t* params) {
    GET_JSON_HANDLE()
    int64_t key = ReadInt64Param(pContext, params[2]);
    DataNode* val = json->IntMapGetArrayNode(key);
    return WrapChildNode(pContext,val);
}

// 32-bit key setters
static cell_t Native_IntMapSetString(IPluginContext* pContext, const cell_t* params) {
    GET_JSON_HANDLE()
    int64_t key = static_cast<int64_t>(params[2]);
    char* value;
    pContext->LocalToString(params[3], &value);
    json->IntMapSetString(key, value);
    return 0;
}

static cell_t Native_IntMapSetInt(IPluginContext* pContext, const cell_t* params) {
    GET_JSON_HANDLE()
    int64_t key = static_cast<int64_t>(params[2]);
    json->IntMapSetInt(key, static_cast<int64_t>(params[3]));
    return 0;
}

static cell_t Native_IntMapSetFloat(IPluginContext* pContext, const cell_t* params) {
    GET_JSON_HANDLE()
    int64_t key = static_cast<int64_t>(params[2]);
    json->IntMapSetFloat(key, static_cast<double>(sp_ctof(params[3])));
    return 0;
}

static cell_t Native_IntMapSetBool(IPluginContext* pContext, const cell_t* params) {
    GET_JSON_HANDLE()
    int64_t key = static_cast<int64_t>(params[2]);
    json->IntMapSetBool(key, params[3] != 0);
    return 0;
}

static cell_t Native_IntMapSetNull(IPluginContext* pContext, const cell_t* params) {
    GET_JSON_HANDLE()
    int64_t key = static_cast<int64_t>(params[2]);
    json->IntMapSetNull(key);
    return 0;
}

static cell_t Native_IntMapSetObject(IPluginContext* pContext, const cell_t* params) {
    GET_JSON_HANDLE()
    int64_t key = static_cast<int64_t>(params[2]);
    DataHandle* child = g_handle_manager.GetDataHandle(params[3]);
    if (!child)
        return 0;
    if (json->node == child->node)
        return pContext->ThrowNativeError("Cannot SetObject with self as child");
    child->node->Incref();
    json->node->IntMapInsert(key, child->node);
    g_handle_manager.FreeHandle(params[3]);
    return 0;
}

// 64-bit key setters
static cell_t Native_IntMapSetString64(IPluginContext* pContext, const cell_t* params) {
    GET_JSON_HANDLE()
    int64_t key = ReadInt64Param(pContext, params[2]);
    char* value;
    pContext->LocalToString(params[3], &value);
    json->IntMapSetString(key, value);
    return 0;
}

static cell_t Native_IntMapSetInt64(IPluginContext* pContext, const cell_t* params) {
    GET_JSON_HANDLE()
    int64_t key = ReadInt64Param(pContext, params[2]);
    json->IntMapSetInt(key, static_cast<int64_t>(params[3]));
    return 0;
}

static cell_t Native_IntMapSetInt64KeyValue(IPluginContext* pContext, const cell_t* params) {
    GET_JSON_HANDLE()
    int64_t key = ReadInt64Param(pContext, params[2]);
    int64_t val = ReadInt64Param(pContext, params[3]);
    json->IntMapSetInt(key, val);
    return 0;
}

static cell_t Native_IntMapSetFloat64(IPluginContext* pContext, const cell_t* params) {
    GET_JSON_HANDLE()
    int64_t key = ReadInt64Param(pContext, params[2]);
    json->IntMapSetFloat(key, static_cast<double>(sp_ctof(params[3])));
    return 0;
}

static cell_t Native_IntMapSetBool64(IPluginContext* pContext, const cell_t* params) {
    GET_JSON_HANDLE()
    int64_t key = ReadInt64Param(pContext, params[2]);
    json->IntMapSetBool(key, params[3] != 0);
    return 0;
}

static cell_t Native_IntMapSetNull64(IPluginContext* pContext, const cell_t* params) {
    GET_JSON_HANDLE()
    int64_t key = ReadInt64Param(pContext, params[2]);
    json->IntMapSetNull(key);
    return 0;
}

static cell_t Native_IntMapSetObject64(IPluginContext* pContext, const cell_t* params) {
    GET_JSON_HANDLE()
    int64_t key = ReadInt64Param(pContext, params[2]);
    DataHandle* child = g_handle_manager.GetDataHandle(params[3]);
    if (!child)
        return 0;
    if (json->node == child->node)
        return pContext->ThrowNativeError("Cannot SetObject with self as child");
    child->node->Incref();
    json->node->IntMapInsert(key, child->node);
    g_handle_manager.FreeHandle(params[3]);
    return 0;
}

// Mutation
static cell_t Native_IntMapRemoveKey(IPluginContext* pContext, const cell_t* params) {
    GET_JSON_HANDLE()
    int64_t key = static_cast<int64_t>(params[2]);
    return json->IntMapRemoveKey(key) ? 1 : 0;
}

static cell_t Native_IntMapRemoveKey64(IPluginContext* pContext, const cell_t* params) {
    GET_JSON_HANDLE()
    int64_t key = ReadInt64Param(pContext, params[2]);
    return json->IntMapRemoveKey(key) ? 1 : 0;
}

static cell_t Native_IntMapClear(IPluginContext* pContext, const cell_t* params) {
    GET_JSON_HANDLE()
    json->IntMapClear();
    return 0;
}

static cell_t Native_IntMapMerge(IPluginContext* pContext, const cell_t* params) {
    GET_JSON_HANDLE()
    DataHandle* other = g_handle_manager.GetDataHandle(params[2]);
    if (!other)
        return 0;
    if (json->node == other->node)
        return 0;  // self-merge is a no-op
    json->IntMapMerge(other->node, params[3] != 0);
    return 0;
}

static cell_t Native_IntMapSize(IPluginContext* pContext, const cell_t* params) {
    GET_JSON_HANDLE()
    return static_cast<cell_t>(json->IntMapSize());
}

static cell_t Native_IntMapHasKey(IPluginContext* pContext, const cell_t* params) {
    GET_JSON_HANDLE()
    int64_t key = static_cast<int64_t>(params[2]);
    return json->IntMapHasKey(key) ? 1 : 0;
}

static cell_t Native_IntMapHasKey64(IPluginContext* pContext, const cell_t* params) {
    GET_JSON_HANDLE()
    int64_t key = ReadInt64Param(pContext, params[2]);
    return json->IntMapHasKey(key) ? 1 : 0;
}


// async2_JsonGetBuffer(Json handle, char[] buffer, int maxlen, int offset = 0) → bytes copied
static cell_t Native_JsonGetBuffer(IPluginContext* pContext, const cell_t* params) {
    DataHandle* dh = g_handle_manager.GetDataHandle(params[1]);
    if (!dh || !dh->node || dh->node->type != DataType::Binary)
        return 0;

    const auto& bin = dh->node->Bin();
    int offset = (params[0] >= 4) ? params[4] : 0;
    if (offset < 0 || static_cast<size_t>(offset) >= bin.size())
        return 0;

    char* buf;
    pContext->LocalToPhysAddr(params[2], reinterpret_cast<cell_t**>(&buf));
    int maxlen = params[3];
    if (maxlen <= 0) return 0;
    size_t available = bin.size() - static_cast<size_t>(offset);
    int to_copy = (available < static_cast<size_t>(maxlen)) ? static_cast<int>(available) : maxlen;
    memcpy(buf, bin.data() + offset, to_copy);
    return to_copy;
}

sp_nativeinfo_t g_JsonNatives[] = {
    {"async2_JsonParseResponse",        Native_JsonParse},
    {"async2_JsonParseString",          Native_JsonParseString},
    {"async2_JsonParseFile",            Native_JsonParseFile},
    {"async2_JsonCreateObject",         Native_JsonCreateObject},
    {"async2_JsonCreateArray",          Native_JsonCreateArray},
    {"async2_JsonClose",                Native_JsonClose},
    {"async2_JsonGetType",              Native_JsonGetType},
    {"async2_JsonObjectSize",           Native_JsonObjectSize},
    {"async2_JsonArrayLength",          Native_JsonArrayLength},
    {"async2_JsonHasKey",               Native_JsonHasKey},
    {"async2_JsonGetString",            Native_JsonGetString},
    {"async2_JsonGetInt",               Native_JsonGetInt},
    {"async2_JsonGetInt64",             Native_JsonGetInt64},
    {"async2_JsonGetFloat",             Native_JsonGetFloat},
    {"async2_JsonGetBool",              Native_JsonGetBool},
    {"async2_JsonGetObject",            Native_JsonGetObject},
    {"async2_JsonGetArray",             Native_JsonGetArray},
    {"async2_JsonArrayGetString",       Native_JsonArrayGetString},
    {"async2_JsonArrayGetInt",          Native_JsonArrayGetInt},
    {"async2_JsonArrayGetInt64",        Native_JsonArrayGetInt64},
    {"async2_JsonArrayGetFloat",        Native_JsonArrayGetFloat},
    {"async2_JsonArrayGetBool",         Native_JsonArrayGetBool},
    {"async2_JsonArrayGetObject",       Native_JsonArrayGetObject},
    {"async2_ObjectIterCreate",         Native_ObjectIterCreate},
    {"async2_IntMapIterCreate",         Native_IntMapIterCreate},
    {"async2_IterNext",                 Native_IterNext},
    {"async2_IntMapIterNext",           Native_IntMapIterNext},
    {"async2_IntMapIterNext64",         Native_IntMapIterNext64},
    {"async2_IterClose",                Native_IterClose},
    {"async2_IterGetType",              Native_IterGetType},
    {"async2_IterGetInt",               Native_IterGetInt},
    {"async2_IterGetInt64",             Native_IterGetInt64},
    {"async2_IterGetFloat",             Native_IterGetFloat},
    {"async2_IterGetBool",              Native_IterGetBool},
    {"async2_IterGetString",            Native_IterGetString},
    {"async2_IterGetObject",            Native_IterGetObject},
    {"async2_JsonSetString",            Native_JsonSetString},
    {"async2_JsonSetInt",               Native_JsonSetInt},
    {"async2_JsonSetInt64",             Native_JsonSetInt64},
    {"async2_JsonSetFloat",             Native_JsonSetFloat},
    {"async2_JsonSetBool",              Native_JsonSetBool},
    {"async2_JsonSetNull",              Native_JsonSetNull},
    {"async2_JsonSetObject",            Native_JsonSetObject},
    {"async2_JsonRemoveKey",            Native_JsonRemoveKey},
    {"async2_JsonObjectClear",          Native_JsonObjectClear},
    {"async2_JsonObjectMerge",          Native_JsonObjectMerge},
    {"async2_JsonArrayRemove",          Native_JsonArrayRemove},
    {"async2_JsonArraySetString",       Native_JsonArraySetString},
    {"async2_JsonArraySetInt",          Native_JsonArraySetInt},
    {"async2_JsonArraySetInt64",        Native_JsonArraySetInt64},
    {"async2_JsonArraySetFloat",        Native_JsonArraySetFloat},
    {"async2_JsonArraySetBool",         Native_JsonArraySetBool},
    {"async2_JsonArraySetNull",         Native_JsonArraySetNull},
    {"async2_JsonArraySetObject",       Native_JsonArraySetObject},
    {"async2_JsonArrayClear",           Native_JsonArrayClear},
    {"async2_JsonArrayExtend",          Native_JsonArrayExtend},
    {"async2_JsonArrayAppendString",    Native_JsonArrayAppendString},
    {"async2_JsonArrayAppendInt",       Native_JsonArrayAppendInt},
    {"async2_JsonArrayAppendInt64",     Native_JsonArrayAppendInt64},
    {"async2_JsonArrayAppendFloat",     Native_JsonArrayAppendFloat},
    {"async2_JsonArrayAppendBool",      Native_JsonArrayAppendBool},
    {"async2_JsonArrayAppendNull",      Native_JsonArrayAppendNull},
    {"async2_JsonArrayAppendObject",    Native_JsonArrayAppendObject},
    {"async2_JsonEquals",               Native_JsonEquals},
    {"async2_JsonRef",                  Native_JsonRef},
    {"async2_JsonCopy",                 Native_JsonCopy},
    {"async2_JsonSerialize",            Native_JsonSerialize},
    {"async2_JsonPathGetInt",           Native_JsonPathGetInt},
    {"async2_JsonPathGetInt64",         Native_JsonPathGetInt64},
    {"async2_JsonPathGetFloat",         Native_JsonPathGetFloat},
    {"async2_JsonPathGetBool",          Native_JsonPathGetBool},
    {"async2_JsonPathGetString",        Native_JsonPathGetString},
    {"async2_JsonPathGet",              Native_JsonPathGet},
    {"async2_JsonPathGetType",          Native_JsonPathGetType},
    {"async2_JsonPathFailed",           Native_JsonPathFailed},
    {"async2_JsonPathError",            Native_JsonPathError},
    {"async2_JsonMemorySize",           Native_JsonMemorySize},
    {"async2_JsonPoolStats",            Native_JsonPoolStats},
    {"async2_JsonGetBuffer",            Native_JsonGetBuffer},
    // IntMap natives
    {"async2_IntObjectCreate",             Native_IntMapCreate},
    {"async2_IntObjectGetString",          Native_IntMapGetString},
    {"async2_IntObjectGetInt",             Native_IntMapGetInt},
    {"async2_IntObjectGetFloat",           Native_IntMapGetFloat},
    {"async2_IntObjectGetBool",            Native_IntMapGetBool},
    {"async2_IntObjectGetObject",          Native_IntMapGetObject},
    {"async2_IntObjectGetArray",           Native_IntMapGetArray},
    {"async2_IntObject64GetString",        Native_IntMapGetString64},
    {"async2_IntObject64GetInt",           Native_IntMapGetInt64Key},
    {"async2_IntObject64GetInt64",         Native_IntMapGetInt64KeyValue},
    {"async2_IntObject64GetFloat",         Native_IntMapGetFloat64},
    {"async2_IntObject64GetBool",          Native_IntMapGetBool64},
    {"async2_IntObject64GetObject",        Native_IntMapGetObject64},
    {"async2_IntObject64GetArray",         Native_IntMapGetArray64},
    {"async2_IntObjectSetString",          Native_IntMapSetString},
    {"async2_IntObjectSetInt",             Native_IntMapSetInt},
    {"async2_IntObjectSetFloat",           Native_IntMapSetFloat},
    {"async2_IntObjectSetBool",            Native_IntMapSetBool},
    {"async2_IntObjectSetNull",            Native_IntMapSetNull},
    {"async2_IntObjectSetObject",          Native_IntMapSetObject},
    {"async2_IntObject64SetString",        Native_IntMapSetString64},
    {"async2_IntObject64SetInt",           Native_IntMapSetInt64},
    {"async2_IntObject64SetInt64",         Native_IntMapSetInt64KeyValue},
    {"async2_IntObject64SetFloat",         Native_IntMapSetFloat64},
    {"async2_IntObject64SetBool",          Native_IntMapSetBool64},
    {"async2_IntObject64SetNull",          Native_IntMapSetNull64},
    {"async2_IntObject64SetObject",        Native_IntMapSetObject64},
    {"async2_IntObjectRemoveKey",          Native_IntMapRemoveKey},
    {"async2_IntObject64RemoveKey",        Native_IntMapRemoveKey64},
    {"async2_IntObjectClear",              Native_IntMapClear},
    {"async2_IntObjectMerge",              Native_IntMapMerge},
    {"async2_IntObjectSize",               Native_IntMapSize},
    {"async2_IntObjectHasKey",             Native_IntMapHasKey},
    {"async2_IntObject64HasKey",           Native_IntMapHasKey64},
    {nullptr,                           nullptr},
};
