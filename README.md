# async2

The last networking extension you will need for sourcemod hopefully.

Windows and Linux support, both x86 and x64. Any engine.

## CURRENTLY IN ALPHA

The Json API is currently being changed to make it as perfect as possible. DO NOT USE IN production. This message will be removed when the API is stable.

## Features

- **HTTP/S** — requests with HTTP/2, HTTPS, automatic decompression (gzip/brotli), retry with exponential backoff, connection pooling
- **HTTP Utils** — url encode/decode query string encode/decode
- **WebSocket** — async client with TLS, message reassembly, auto-ping, auto-reconnect with exponential backoff
- **Json** — high-performance all purpose data structure with O(1) key lookup, deep path access, and int64 support
- **MessagePack** — binary serialization, convert any Json to messagepack seamlessly. Messagepack to Json if it only has JSON types.
- **IntObject** — int64-keyed maps (useful for SteamIDs, entity indices). can be put inside a Json, but won't be serialized as Json.
- **TCP** — async client and server
- **UDP** — async client and server
- **DNS** — all dns (when you use domain names instead of ips) is async and will never block. Old extensions that use regular DNS can block until the server is rebooted. Cache and configurable timeouts.
- **HJSON** — parse config files with comments, unquoted keys/values, multiline strings. AVX2-accelerated with scalar fallback.
- **Crypto** — Base64, hex, SHA-256, SHA-1, MD5, CRC32, HMAC
- **Linked List** — Yes this basic datastructure is missing from sourcemod and needed for efficient implementation of caches. An LRU cache is included in async2_lru.inc
- **Time** — Native for current time as milliseconds since Unix epoch. Compatible with javascript. Never jumps backwards in time.

All I/O runs on a background thread (libuv). Callbacks fire on the game thread.

Over 500 sourcepawn tests, over 600 C++ tests. All networking tested.

### JSON Performance

Custom all-purpose data structure for speed. No weaknesses in any spot unlike the competition.

| Benchmark | async2 | yyjson | simdjson | jansson |
|---|--:|--:|--:|--:|
| parse only | 53.9 us | 8.7 us | 864.7 ns | 250.9 us |
| obj[10] random key access | 4.6 ns | 16.3 ns | N/A | 7.1 ns |
| obj[50] random key access | 5.3 ns | 38.1 ns | N/A | 12.7 ns |
| obj[1000] random key access | 9.7 ns | 653.5 ns | N/A | 14.4 ns |
| arr[10] random index access | 0.2 ns | 0.7 ns | N/A | 1.5 ns |
| arr[50] random index access | 0.2 ns | 0.7 ns | N/A | 1.5 ns |
| arr[1000] random index access | 0.3 ns | 0.7 ns | N/A | 1.6 ns |
| obj insert 10 keys | 29.2 ns | 4.0 ns | N/A | 28.8 ns |
| obj insert 50 keys | 27.5 ns | 3.1 ns | N/A | 32.1 ns |
| obj insert 1000 keys | 32.6 ns | 2.5 ns | N/A | 56.8 ns |
| obj[10] random delete | 17.7 ns | 18.6 ns | N/A | 18.1 ns |
| obj[50] random delete | 18.3 ns | 55.3 ns | N/A | 21.8 ns |
| obj[1000] random delete | 26.2 ns | 853.2 ns | N/A | 25.1 ns |
| arr append 10 | 9.5 ns | 2.4 ns | N/A | 10.8 ns |
| arr append 50 | 7.3 ns | 1.8 ns | N/A | 10.3 ns |
| arr append 1000 | 9.1 ns | 1.1 ns | N/A | 18.4 ns |
| arr[10] random insert | 10.0 ns | 4.0 ns | N/A | 12.9 ns |
| arr[50] random insert | 9.8 ns | 7.2 ns | N/A | 12.6 ns |
| arr[1000] random insert | 19.3 ns | 165.7 ns | N/A | 36.2 ns |
| arr[10] random delete | 8.5 ns | 7.9 ns | N/A | 11.4 ns |
| arr[50] random delete | 8.4 ns | 10.6 ns | N/A | 11.6 ns |
| arr[1000] random delete | 17.4 ns | 160.6 ns | N/A | 20.1 ns |

Deep nested access without intermediate handles:

```sourcepawn
async2_JsonPathGetInt(json, "foo", "bar", 3, "baz"); // json["foo"]["bar"][3]["baz"]
```

## Installation

1. Download the latest release from the [Releases](../../releases) page
2. Extract and copy `addons/` into your game directory (e.g. `tf/`)
3. Add `#include <async2>` to your plugin

## Quick Start

### HTTP GET

```sourcepawn
#include <async2>

public void OnPluginStart()
{
    WebRequest req = async2_HttpNew();
    req.Execute("GET", "https://api.example.com/player/123", OnResponse);
}

void OnResponse(WebRequest req, int curlcode, int httpcode, int size)
{
    if (curlcode != 0) {
        char error[CURL_ERROR_SIZE];
        req.GetErrorMessage(error, sizeof(error));
        PrintToServer("Error: %s", error);
        return;
    }

    char body[4096];
    req.GetString(body, sizeof(body));
    PrintToServer("Response (%d): %s", httpcode, body);
}
```

### HTTP GET with JSON parsing

```sourcepawn
public void OnPluginStart()
{
    WebRequest req = async2_HttpNew();
    req.SetResponseType(RESPONSE_JSON);  // parse JSON on background thread
    req.Execute("GET", "https://api.example.com/player/123", OnJsonResponse);
}

void OnJsonResponse(WebRequest req, int curlcode, int httpcode, Json data)
{
    if (curlcode != 0 || data == null) return;

    // Deep path access — no intermediate handles needed
    char name[64];
    async2_JsonPathGetString(data, "data", "user", "name", name, sizeof(name));
    int rank = async2_JsonPathGetInt(data, "data", "stats", "rank");
    PrintToServer("%s is rank %d", name, rank);

    data.Close();
}
```

### HTTP POST with JSON body

```sourcepawn
WebRequest req = async2_HttpNew();
req.SetHeader("Authorization", "Bearer my-token");

Json body = Json.CreateObject();
body.SetString("action", "ban");
body.SetInt("duration", 3600);
req.SetBodyJSON(body);  // consumes body — handle is freed, Close() is optional (safe no-op)

req.Execute("POST", "https://api.example.com/admin", OnResponse);
```

### JSON

```sourcepawn
// Create and build
Json obj = Json.CreateObject();
obj.SetString("name", "player");
obj.SetInt("score", 100);

Json items = Json.CreateArray();
items.ArrayAppendInt(1);
items.ArrayAppendInt(2);
items.ArrayAppendInt(3);
obj.SetObject("items", items);  // consumes items — handle is freed, Close() is optional
// SetObject/ArrayAppendObject consume the child handle. No deep copy.
// If you need the child afterward, copy first:
//   Json copy = child.Copy();
//   parent.SetObject("a", copy);  // deep copy, original stays valid
//   parent.SetObject("b", child);         // consumes child

char buf[256];
obj.Serialize(buf, sizeof(buf));
// {"name":"player","score":100,"items":[1,2,3]}

// Iterate object keys
char key[64];
Iterator iter = Iterator.FromObject(obj);
while (iter.Next(key, sizeof(key))) {
    PrintToServer("key: %s", key);
}
iter.Close();

// Lightweight reference — shares the same data, no deep copy
Json ref = obj.Ref();
ref.SetInt("score", 200);  // mutation visible through both handles
ref.Close();  // close independently
obj.Close();

// Parse from string or file
Json data = Json.ParseString("{\"users\":[{\"id\":1,\"name\":\"alice\"}]}");
// Json data = async2_JsonParseFile("addons/sourcemod/data/mydata.json");
char name[64];
async2_JsonPathGetString(data, "users", 0, "name", name, sizeof(name));
// name = "alice"
data.Close();
```

### HJSON

Parse config files with comments, unquoted keys, and multiline strings. Returns a standard `Json` handle.

https://hjson.github.io/

```sourcepawn
// Parse from file (path relative to game directory)
Json cfg = async2_HjsonParseFile("addons/sourcemod/configs/myconfig.hjson");
if (cfg == null) {
    PrintToServer("Failed to parse config");
    return;
}

char host[64];
cfg.GetString("host", host, sizeof(host));
int port = cfg.GetInt("port");
bool debug = cfg.GetBool("debug");
cfg.Close();
```

Example `myconfig.hjson`:

```hjson
# Server settings
host: 127.0.0.1
port: 27015
debug: true

// Multiline strings
motd:
  '''
  Welcome to the server!
  Have fun and play fair.
  '''
```

### TCP Client

```sourcepawn
TcpSocket sock = new TcpSocket();
sock.SetCallbacks(OnConnect, OnData, OnError, OnClose);
sock.Connect("127.0.0.1", 8080);

void OnConnect(TcpSocket sock, any userdata)
{
    sock.Send("Hello", 5);
}

void OnData(TcpSocket sock, const char[] data, int length, any userdata)
{
    PrintToServer("Received %d bytes", length);
}

void OnError(TcpSocket sock, int error, const char[] msg, any userdata)
{
    PrintToServer("Error %d: %s", error, msg);
}

void OnClose(TcpSocket sock, bool userClosed, any userdata)
{
    PrintToServer("Connection closed (by us: %s)", userClosed ? "yes" : "no");
}
```

### WebSocket Client

```sourcepawn
WsSocket ws = new WsSocket();
ws.SetCallbacks(OnWsConnect, OnWsMessage, OnWsError, OnWsClose);
ws.SetReconnect(5, 1000, 2.0, 30000);  // auto-reconnect: 5 attempts, 1s initial, 2x backoff, 30s max
ws.Connect("wss://echo.websocket.org");

void OnWsConnect(WsSocket ws, any userdata)
{
    ws.SendText("Hello WebSocket");
}

void OnWsMessage(WsSocket ws, const char[] data, int length, bool isBinary, any userdata)
{
    PrintToServer("Received: %s", data);
}

void OnWsError(WsSocket ws, int error, const char[] msg, any userdata)
{
    PrintToServer("Error %d: %s", error, msg);
}

void OnWsClose(WsSocket ws, int code, const char[] reason, any userdata)
{
    PrintToServer("Closed with code %d", code);
}
```

### Important: Handles

async2 types (`WebRequest`, `Json`, `TcpSocket`, `UdpSocket`, `WsSocket`, `LinkedList`) are **not** SourceMod handles. They use an internal handle manager to bypass Sourcemod's very limited handle count.

Do not pass them to `CloseHandle()`, `delete`, or any API expecting a `Handle`. Always use the type's own `.Close()` method. When a plugin unloads, all handles it created are automatically cleaned up. Unloading the extension will close all handles.

To pass a handle to another plugin, the receiving plugin should call `async2_SetHandlePlugin` to take cleanup ownership. Without this, the handle is freed when the original plugin unloads. For Json and LinkedList, `.Copy()` creates an independent deep copy owned by the calling plugin. `.Ref()` creates a lightweight reference sharing the same data (no copy) — mutations through either handle are visible to both.

```sourcepawn
// proper cleanup
WsSocket g_ws = new WsSocket;

if (g_ws != null) 
{
    g_ws.Close();
    g_ws = null;
}

// passing a handle to another plugin so it doesn't close when we close
// but closes when they close

public int Native_TransferJson(Handle callingPlugin, int numParams)
{
    Json data = g_json;
    g_json = null; // we shouldn't be using the handle here since we're giving it to another plugin
    async2_SetHandlePlugin(data, callingPlugin);
    return view_as<int>(data);
}

// copying the handle so both plugins can use it

public int Native_CopyJson(Handle callingPlugin, int numParams)
{
    Json copy = g_json.Copy();
    async2_SetHandlePlugin(copy, callingPlugin);
    return view_as<int>(copy);
}

```

### Coming from async

This is the successor of async. Not drop-in compatible, but you can run both side-by-side and migrate one plugin at a time. Key differences: request handles auto-close after the callback, large POST body bug is fixed, compression correctly sets Content-Type, and it works on any engine.

## FAQ

- **Why bundle JSON and MessagePack instead of a separate extension?**

  SourceMod has limited stack space. If you put everything into a string, it can silently fail. You can work around this with `#pragma dynamic`, but you'd have to guess the max size ahead of time. With JSON in the same extension, you can pass or receive arbitrarily large data from the web.

  The Json object fills the gaps in sourcemod's basic data structures. Instead of using nested trie/StringMaps and arrays, a single Json does it all. 

- **Why bundle TCP and UDP?**

  When I ported the socket extension from sfPlayer to x64, it started exhibiting random crashes and deadlocks. Callbacks would sometimes just stop working. It also had no way of controlling DNS timeouts, so if the DNS request hung, it could stall forever.

## Building from Source

```bash
git submodule update --init --recursive

# Linux
./build_deps.sh x86_64    # can also be x86. or x86,x86_64
./build.sh x86_64

# Windows
build_deps.bat x86_64
mkdir build-x86_64 && cd build-x86_64
cmake .. -A x64 -DSM_PATH=<sourcemod>
cmake --build . --config Release -j
```

Requires C++17 compiler and CMake 3.16+. All dependencies are bundled as git submodules.

## License

[GNU General Public License v3.0](LICENSE). Third-party licenses are in their respective `third_party/` directories.
