# async2

The last networking extension you will need for sourcemod hopefully.

Windows and Linux support, both x86 and x64. Any engine.

## Features

- **HTTP/S** — requests with HTTP/2, HTTPS, automatic decompression (gzip/brotli), retry with exponential backoff, connection pooling
- **Json** — high-performance all purpose data structure with O(1) key lookup, deep path access, and int64 support
- **MessagePack** — binary serialization, convert any Json to messagepack seamlessly. Messagepack to Json if it only has JSON types.
- **IntObject** — int64-keyed maps (useful for SteamIDs, entity indices). can be put inside a Json, but won't be serialized as Json.
- **TCP** — async client and server sockets
- **UDP** — async datagram sockets
- **WebSocket** — async client with TLS, message reassembly, auto-ping, auto-reconnect with exponential backoff
- **DNS** — async resolution for TCP/UDP with caching and configurable timeouts
- **HJSON** — parse config files with comments, unquoted keys/values, multiline strings. AVX2-accelerated with scalar fallback.
- **Crypto** — Base64, hex, SHA-256, SHA-1, MD5, CRC32, HMAC

All I/O runs on a background thread (libuv). Callbacks fire on the game thread.

Over 500 sourcepawn tests, over 600 C++ tests. All networking tested.

### JSON Performance

Custom all-purpose data structure for speed. No weaknesses in any spot unlike the competition.

| Benchmark | async2 | yyjson | simdjson | jansson |
|---|--:|--:|--:|--:|
| parse only | 50.2 us | 9.2 us | 940.7 ns | 273.7 us |
| obj[10] random key access | 4.7 ns | 16.1 ns | N/A | 10.9 ns |
| obj[50] random key access | 5.4 ns | 38.2 ns | N/A | 12.3 ns |
| obj[1000] random key access | 9.9 ns | 596.6 ns | N/A | 14.7 ns |
| arr[10] random index access | 0.3 ns | 0.7 ns | N/A | 1.5 ns |
| arr[1000] random index access | 0.3 ns | 0.7 ns | N/A | 1.5 ns |
| obj insert 10 keys | 28.6 ns | 4.1 ns | N/A | 28.3 ns |
| obj insert 50 keys | 24.7 ns | 3.0 ns | N/A | 31.9 ns |
| obj insert 1000 keys | 29.5 ns | 2.4 ns | N/A | 56.7 ns |
| obj[10] random delete | 18.1 ns | 19.8 ns | N/A | 20.8 ns |
| obj[50] random delete | 17.9 ns | 62.4 ns | N/A | 21.0 ns |
| obj[1000] random delete | 26.2 ns | 912.6 ns | N/A | 25.3 ns |
| arr append 1000 | 6.3 ns | 1.2 ns | N/A | 18.6 ns |
| arr[10] random insert | 8.7 ns | 4.7 ns | N/A | 12.9 ns |
| arr[1000] random insert | 16.5 ns | 169.2 ns | N/A | 32.1 ns |
| arr[10] random delete | 7.6 ns | 7.5 ns | N/A | 11.4 ns |
| arr[1000] random delete | 17.2 ns | 165.3 ns | N/A | 20.1 ns |

Deep nested access without intermediate handles:

```sourcepawn
async2_JsonPathGetInt(json, "foo", "bar", 3, "baz"); // json["foo"]["bar"][3]["baz"]
```

## Installation

1. Download the latest release from the [Releases](../../releases) page
2. Extract and copy `addons/` into your game directory (e.g. `tf/`)
3. Add `#include <async2>` to your plugin

## Quick Start

### HTTP GET with JSON parsing

```sourcepawn
#include <async2>

public void OnPluginStart()
{
    WebRequest req = async2_New();
    req.SetResponseType(RESPONSE_JSON);  // parse JSON on background thread
    req.Execute("GET", "https://api.example.com/player/123", OnResponse);
}

void OnResponse(WebRequest req, int curlcode, int httpcode, Json data)
{
    if (curlcode != 0) {
        char error[256];
        async2_ErrorString(curlcode, error, sizeof(error));
        PrintToServer("Error: %s", error);
        return;
    }

    if (data == null) return;

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
WebRequest req = async2_New();
req.SetHeader("Authorization", "Bearer my-token");

Json body = async2_JsonCreateObject();
body.SetString("action", "ban");
body.SetInt("duration", 3600);
req.SetBodyJSON(body);  // body copied to C++ here. changes to body don't change what's sent after this
body.Close();

req.Execute("POST", "https://api.example.com/admin", OnResponse);
```

### JSON

```sourcepawn
// Create and build
Json obj = async2_JsonCreateObject();
obj.SetString("name", "player");
obj.SetInt("score", 100);

Json items = async2_JsonCreateArray();
items.PushInt(1);
items.PushInt(2);
items.PushInt(3);
obj.SetObject("items", items);
items.Close();

char buf[256];
obj.Serialize(buf, sizeof(buf));
// {"name":"player","score":100,"items":[1,2,3]}

// Iterate object keys
obj.ObjectIterReset();
char key[64];
while (obj.ObjectIterNext(key, sizeof(key))) {
    JsonType type = obj.GetType(key);
    PrintToServer("key: %s, type: %d", key, type);
}
obj.Close();

// Parse from string or file
Json data = Json.ParseString("{\"users\":[{\"id\":1,\"name\":\"alice\"}]}");
// Json data = async2_JsonParseFile("addons/sourcemod/data/mydata.json");
char name[64];
async2_JsonPathGetString(data, "users", 0, "name", name, sizeof(name));
// name = "alice"
data.Close();
```

### [HJSON](https://hjson.github.io/)

Parse config files with comments, unquoted keys, and multiline strings. Returns a standard `Json` handle.

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

async2 types (`WebRequest`, `Json`, `TcpSocket`, `UdpSocket`, `WsSocket`) are **not** SourceMod handles. They use an internal handle manager. Do not pass them to `CloseHandle()`, `delete`, or any API expecting a `Handle`. Always use the type's own `.Close()` method.

All types support `null` assignment and comparison:

```sourcepawn
WsSocket g_ws = null;

// later...
if (g_ws != null) {
    g_ws.Close();
    g_ws = null;
}
```

### Coming from async

This is the successor of async. Not drop-in compatible, but you can run both side-by-side and migrate one plugin at a time. Key differences: request handles auto-close after the callback, large POST body bug is fixed, compression correctly sets Content-Type, and it works on any engine.

## FAQ

- **Why bundle JSON and MessagePack instead of a separate extension?**

  SourceMod has limited stack space. If you put everything into a string, it can silently fail. You can work around this with `#pragma dynamic`, but you'd have to guess the max size ahead of time. With JSON in the same extension, you can pass or receive arbitrarily large data from the web.

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
