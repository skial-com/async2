# Add permessage-deflate to WebSocket

## Context

WebSocket connections currently have no compression support. Adding RFC 7692 permessage-deflate allows compressed message payloads, reducing bandwidth significantly for text-heavy protocols (JSON, MsgPack). The main blocker is that libcurl 8.18.0 does not support permessage-deflate — its `ws_frame_firstbyte2flags` rejects RSV1 bits (used to signal compressed frames) and there's no way to set RSV1 on outgoing frames. We must patch curl and implement the compression layer ourselves using zlib (already linked).

## Files to modify

| File | Change |
|------|--------|
| `third_party/curl/include/curl/websockets.h` | Add `CURLWS_COMPRESSED` flag |
| `third_party/curl/lib/ws.c` | Allow RSV1 in decoder/encoder |
| `src/ws_connection.h` | Add deflate fields, zlib streams |
| `src/ws_connection.cpp` | Add `InitDeflateStreams`, `CleanupDeflateStreams` |
| `src/event_loop.h` | Declare `OnWsReceiveHeaders` |
| `src/event_loop.cpp` | Negotiate, compress, decompress |
| `src/natives_ws.cpp` | Handle `WS_PERMESSAGE_DEFLATE` option |
| `sourcepawn/async2/ws.inc` | Add option constant + methodmap method |
| `src/natives_utils.cpp` | Bump API version 4 → 5 |

## Step 1: Patch curl — `CURLWS_COMPRESSED` flag

### `third_party/curl/include/curl/websockets.h`
After `#define CURLWS_PONG (1 << 6)` (line 60), add:
```c
#define CURLWS_COMPRESSED (1 << 7)
```

### `third_party/curl/lib/ws.c`

**`ws_frame_firstbyte2flags` (line 146)**: Strip RSV1 before the switch so existing cases match. Refactor returns to breaks, then after the switch OR `CURLWS_COMPRESSED` into flags when RSV1 was set on a data frame:
```c
static int ws_frame_firstbyte2flags(struct Curl_easy *data,
                                    uint8_t firstbyte, int cont_flags)
{
  bool rsv1 = (firstbyte & WSBIT_RSV1) != 0;
  int flags;
  firstbyte &= (uint8_t)~WSBIT_RSV1;
  switch(firstbyte) {
    // ... all existing cases, but replace `return X;` with `flags = X; break;`
    // default case still returns 0 (error)
  }
  if(rsv1) {
    if(flags & (CURLWS_TEXT | CURLWS_BINARY | CURLWS_CONT))
      flags |= CURLWS_COMPRESSED;
    else {
      failf(data, "[WS] RSV1 set on control frame");
      return 0;
    }
  }
  return flags;
}
```

**`ws_frame_flags2firstbyte` (line 225)**: Strip `CURLWS_COMPRESSED` before the switch. After the switch produces firstbyte, OR in `WSBIT_RSV1` if compressed:
```c
bool compressed = (flags & CURLWS_COMPRESSED) != 0;
switch(flags & ~(CURLWS_OFFSET | CURLWS_COMPRESSED)) {
  // ... all existing cases unchanged
}
if(compressed)
  *pfirstbyte |= WSBIT_RSV1;
```

## Step 2: Add deflate state to WsConnection

### `src/ws_connection.h`
Add `#include <zlib.h>`. Add fields to `WsConnection`:
```cpp
// Permessage-deflate (game thread sets permessage_deflate before Connect; rest event thread only)
bool permessage_deflate = false;
bool deflate_negotiated_ = false;
bool server_no_context_takeover_ = false;
bool client_no_context_takeover_ = false;
int server_max_window_bits_ = 15;
int client_max_window_bits_ = 15;
z_stream inflate_stream_{};
z_stream deflate_stream_{};
bool inflate_initialized_ = false;
bool deflate_initialized_ = false;
bool message_compressed_ = false;

bool InitDeflateStreams();
void CleanupDeflateStreams();
```

### `src/ws_connection.cpp`
Add `#include <zlib.h>`. Implement:
- `InitDeflateStreams()`: `inflateInit2(&inflate_stream_, -server_max_window_bits_)` + `deflateInit2(&deflate_stream_, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -client_max_window_bits_, 8, Z_DEFAULT_STRATEGY)`. Raw deflate (negative windowBits) per RFC 7692.
- `CleanupDeflateStreams()`: `inflateEnd`/`deflateEnd` if initialized, reset `deflate_negotiated_`/`message_compressed_`.
- Call `CleanupDeflateStreams()` in `~WsConnection()`.

## Step 3: Negotiate during handshake

### `src/event_loop.h`
Add declaration: `static size_t OnWsReceiveHeaders(char* buffer, size_t size, size_t nitems, void* userdata);`

### `src/event_loop.cpp`

**New `OnWsReceiveHeaders` callback**: Parse response headers for `Sec-WebSocket-Extensions: permessage-deflate`. If found, set `conn->deflate_negotiated_ = true` and parse parameters (`server_no_context_takeover`, `client_no_context_takeover`, `server_max_window_bits=N`, `client_max_window_bits=N`).

**`WsInitCurl` (line 1885)**:
- Reset `conn->deflate_negotiated_ = false` at the top (for reconnect correctness)
- After `BuildHeaderSlist`, if `conn->permessage_deflate`, append `Sec-WebSocket-Extensions: permessage-deflate; client_max_window_bits` to `conn->built_headers_`
- Set `CURLOPT_HEADERFUNCTION`/`CURLOPT_HEADERDATA` for extension negotiation

**`CheckCompletedJobs` WS success path (line 567)**: After `conn->reconnect_count = 0`, if `conn->deflate_negotiated_`, call `conn->InitDeflateStreams()`. Non-fatal on failure (proceed uncompressed).

## Step 4: Compress/decompress messages

### `OnWsPollActivity` — incoming (line 1705)
- In the `if (!conn->in_fragmented_message_)` block, capture `conn->message_compressed_ = (meta->flags & CURLWS_COMPRESSED) != 0`
- After reassembly complete (line 1739), before creating the event: if `message_compressed_ && deflate_negotiated_`:
  - Append `\x00\x00\xFF\xFF` to `message_buf_` (RFC 7692 §7.2.2)
  - Inflate in a loop using `inflate_stream_`, check `max_message_size` during decompression (zip bomb guard → close 1009)
  - On inflate error → WS_ERROR + disconnect/reconnect
  - If `server_no_context_takeover_`, call `inflateReset`
  - Replace `message_buf_` with decompressed data

### `ProcessWsSend` — outgoing (line 1510)
- After body_node serialization, before the flags switch: if `deflate_negotiated_ && deflate_initialized_` and it's a data frame with non-empty data:
  - Deflate with `Z_SYNC_FLUSH`, strip trailing `\x00\x00\xFF\xFF`
  - Only use compressed version if smaller than original (safe with context takeover since neither side's context advances for uncompressed messages)
  - If `client_no_context_takeover_`, call `deflateReset`
- If compressed, add `CURLWS_COMPRESSED` to `flags` before `curl_ws_send`

## Step 5: Cleanup on disconnect/reconnect

**`WsCleanup` (line 1951)**: Call `conn->CleanupDeflateStreams()` in both `keep_connection` and non-keep paths. For the keep path, also reset `conn->message_compressed_ = false`. Streams are re-initialized after the next successful reconnect.

## Step 6: SourcePawn API + version bump

### `sourcepawn/async2/ws.inc`
- Add `#define WS_PERMESSAGE_DEFLATE 8` after `WS_CONNECT_TIMEOUT`
- Add to methodmap:
```sourcepawn
public void SetPermessageDeflate(bool enable = true) {
    async2_WsSetOption(this, WS_PERMESSAGE_DEFLATE, enable ? 1 : 0);
}
```

### `src/natives_ws.cpp`
In `Native_WsSetOption` switch, add case 8: `conn->permessage_deflate = (value != 0);`

### `src/natives_utils.cpp`
Bump `g_async2_api_version = 4` → `5`.

## Verification

1. Rebuild curl: `./build_deps.sh x86_64` (patches are in third_party/curl)
2. Build extension: `./build.sh x86_64`
3. Run C++ tests: `cd test && mkdir -p build && cd build && cmake .. && make -j4 && ctest --output-on-failure`
4. Compile SP tests: `spcomp64 sourcepawn/async2_test/async2_test.sp -isourcepawn -i../sdk/sourcemod/scripting/include -osourcepawn/async2_test/async2_test.smx`
5. Manual test: Connect to a server that supports permessage-deflate (most production WS servers do). Verify compressed frames via Wireshark or server-side logging. Test with `SetPermessageDeflate(true)` before Connect.
6. Verify reconnect: Deflate streams re-initialize after reconnect.
7. Verify fallback: Server that doesn't support permessage-deflate → connection works uncompressed.
