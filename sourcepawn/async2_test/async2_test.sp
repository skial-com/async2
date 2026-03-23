#include <sourcemod>
#include <async2>

int g_passed;
int g_failed;

// TCP/UDP/WS test state
int g_tcp_test_pending;
int g_udp_test_pending;
int g_ws_test_pending;

// Stress test state
bool g_stress_running;
int g_stress_sent;
int g_stress_completed;
int g_stress_failed;
int g_stress_inflight;
int g_stress_max_inflight;
float g_stress_start_time;
int g_stress_cross_map;       // callbacks received after a map change
int g_stress_map_gen;         // increments on each map change while running
bool g_stress_fresh_conn;     // force fresh connection per request
int g_stress_http_ok;         // per-protocol success counts
int g_stress_tcp_ok;
int g_stress_udp_ok;
int g_stress_ws_ok;

// HTTP test state
int g_http_pending;

// Handle leak tracking
int g_handles_at_start;

public Plugin myinfo = {
    name = "Async2 Unit Tests",
    author = "Bottiger",
    description = "Unit tests for async2 extension",
    version = "1.0",
    url = "www.skial.com"
};

public void OnPluginStart() {
    RegServerCmd("sm_async2_test_json", Command_RunJsonTests, "Run JSON unit tests");
    RegServerCmd("sm_async2_test_http", Command_RunHttpTests, "Run HTTP unit tests");
    RegServerCmd("sm_async2_test_all", Command_RunAllTests, "Run all unit tests");
    RegServerCmd("sm_async2_test_tcp", Command_RunTcpTests, "Run TCP unit tests");
    RegServerCmd("sm_async2_test_udp", Command_RunUdpTests, "Run UDP unit tests");
    RegServerCmd("sm_async2_test_ws", Command_RunWsTests, "Run WebSocket unit tests");
    RegServerCmd("sm_async2_test_stress", Command_StressStart, "Start HTTP stress test (continuous requests)");
    RegServerCmd("sm_async2_test_stress_nokeep", Command_StressStartNoKeep, "Start stress test with fresh connections");
    RegServerCmd("sm_async2_test_stress_stop", Command_StressStop, "Stop HTTP stress test");
    RegServerCmd("sm_async2_test_linkedlist", Command_RunLinkedListTests, "Run LinkedList unit tests");
    RegServerCmd("sm_async2_test_lru_cache", Command_RunLruCacheTests, "Run LRU cache unit tests");

    ConVar sv_cheats = FindConVar("sv_cheats");
    sv_cheats.BoolValue = true;
}

void Assert(bool condition, const char[] name) {
    if (condition) {
        g_passed++;
    } else {
        g_failed++;
        PrintToServer("[FAIL] %s", name);
    }
}

void AssertEq(int actual, int expected, const char[] name) {
    if (actual == expected) {
        g_passed++;
    } else {
        g_failed++;
        PrintToServer("[FAIL] %s (expected %d, got %d)", name, expected, actual);
    }
}

void AssertFloatEq(float actual, float expected, const char[] name) {
    float diff = actual - expected;
    if (diff < 0.0) diff = -diff;
    if (diff < 0.001) {
        g_passed++;
    } else {
        g_failed++;
        PrintToServer("[FAIL] %s (expected %f, got %f)", name, expected, actual);
    }
}

void AssertStrEq(const char[] actual, const char[] expected, const char[] name) {
    if (StrEqual(actual, expected)) {
        g_passed++;
    } else {
        g_failed++;
        PrintToServer("[FAIL] %s (expected \"%s\", got \"%s\")", name, expected, actual);
    }
}

void MaybeFinishAll() {
    if (g_http_pending <= 0 && g_tcp_test_pending <= 0 && g_udp_test_pending <= 0 && g_ws_test_pending <= 0) {
        // Defer handle leak check by one frame — close event callbacks fire
        // before FreeHandle, so the last socket handle is still alive here.
        CreateTimer(0.0, Timer_FinishAll);
    }
}

public Action Timer_FinishAll(Handle timer) {
    int handles_now = async2_GetHandleCount();
    int leaked = handles_now - g_handles_at_start;
    PrintToServer("========================================");
    PrintToServer("  Results: %d passed, %d failed", g_passed, g_failed);
    if (leaked != 0)
        PrintToServer("  Handle leak: %d handles (%d -> %d)", leaked, g_handles_at_start, handles_now);
    PrintToServer("========================================");
    return Plugin_Stop;
}

// Test suites
#include "test_json.sp"
#include "test_intmap.sp"
#include "test_msgpack.sp"
#include "test_dns.sp"
#include "test_crypto.sp"
#include "test_tcp.sp"
#include "test_udp.sp"
#include "test_ws.sp"
#include "test_http.sp"
#include "test_stress.sp"
#include "test_linkedlist.sp"
#include "test_lru_cache.sp"
#include "test_time.sp"
#include "test_handles.sp"

// ============================================================================
// Runners
// ============================================================================

public Action Command_RunJsonTests(int args) {
    g_passed = 0;
    g_failed = 0;

    PrintToServer("========================================");
    PrintToServer("  async2 JSON unit tests");
    PrintToServer("========================================");

    RunJsonTests();
    RunMsgPackTests();
    RunIntMapTests();
    RunDnsTests();
    RunCryptoTests();
    RunLinkedListTests();
    RunLruCacheTests();
    RunTimeTests();
    RunHandleTests();

    PrintToServer("========================================");
    PrintToServer("  Results: %d passed, %d failed", g_passed, g_failed);
    PrintToServer("========================================");

    return Plugin_Handled;
}

public Action Command_RunLinkedListTests(int args) {
    g_passed = 0;
    g_failed = 0;

    PrintToServer("========================================");
    PrintToServer("  async2 LinkedList unit tests");
    PrintToServer("========================================");

    RunLinkedListTests();

    PrintToServer("========================================");
    PrintToServer("  Results: %d passed, %d failed", g_passed, g_failed);
    PrintToServer("========================================");

    return Plugin_Handled;
}

public Action Command_RunLruCacheTests(int args) {
    g_passed = 0;
    g_failed = 0;

    PrintToServer("========================================");
    PrintToServer("  async2 LRU cache unit tests");
    PrintToServer("========================================");

    RunLruCacheTests();

    PrintToServer("========================================");
    PrintToServer("  Results: %d passed, %d failed", g_passed, g_failed);
    PrintToServer("========================================");

    return Plugin_Handled;
}

public Action Command_RunHttpTests(int args) {
    g_passed = 0;
    g_failed = 0;
    g_http_pending = 0;
    g_tcp_test_pending = 0;
    g_udp_test_pending = 0;
    g_ws_test_pending = 0;
    g_handles_at_start = async2_GetHandleCount();

    PrintToServer("========================================");
    PrintToServer("  async2 HTTP unit tests");
    PrintToServer("  (requires: go run test/test_server.go)");
    PrintToServer("========================================");

    RunHttpTests();
    RunTcpTests();
    RunUdpTests();
    RunWsTests();

    int total_pending = g_http_pending + g_tcp_test_pending + g_udp_test_pending + g_ws_test_pending;
    PrintToServer("  %d sync tests passed, waiting for %d async callbacks...", g_passed, total_pending);

    return Plugin_Handled;
}

public Action Command_RunAllTests(int args) {
    g_passed = 0;
    g_failed = 0;
    g_http_pending = 0;
    g_tcp_test_pending = 0;
    g_udp_test_pending = 0;
    g_ws_test_pending = 0;
    g_handles_at_start = async2_GetHandleCount();

    PrintToServer("========================================");
    PrintToServer("  async2 ALL unit tests");
    PrintToServer("========================================");

    RunJsonTests();
    RunMsgPackTests();
    RunIntMapTests();
    RunDnsTests();
    RunCryptoTests();
    RunLinkedListTests();
    RunLruCacheTests();
    RunTimeTests();
    RunHandleTests();
    RunHttpTests();
    RunTcpTests();
    RunUdpTests();
    RunWsTests();

    int total_pending = g_http_pending + g_tcp_test_pending + g_udp_test_pending + g_ws_test_pending;
    PrintToServer("  %d sync tests passed, waiting for %d async callbacks...", g_passed, total_pending);

    return Plugin_Handled;
}

public Action Command_RunWsTests(int args) {
    g_passed = 0;
    g_failed = 0;
    g_ws_test_pending = 0;

    PrintToServer("========================================");
    PrintToServer("  async2 WebSocket unit tests");
    PrintToServer("  (requires: go run test/test_server.go)");
    PrintToServer("========================================");

    RunWsTests();

    PrintToServer("  Waiting for %d WS callbacks...", g_ws_test_pending);
    return Plugin_Handled;
}

public Action Command_RunTcpTests(int args) {
    g_passed = 0;
    g_failed = 0;
    g_tcp_test_pending = 0;

    PrintToServer("========================================");
    PrintToServer("  async2 TCP unit tests");
    PrintToServer("  (requires: go run test/test_server.go)");
    PrintToServer("========================================");

    RunTcpTests();

    PrintToServer("  Waiting for %d TCP callbacks...", g_tcp_test_pending);
    return Plugin_Handled;
}

public Action Command_RunUdpTests(int args) {
    g_passed = 0;
    g_failed = 0;
    g_udp_test_pending = 0;

    PrintToServer("========================================");
    PrintToServer("  async2 UDP unit tests");
    PrintToServer("  (requires: go run test/test_server.go)");
    PrintToServer("========================================");

    RunUdpTests();

    PrintToServer("  Waiting for %d UDP callbacks...", g_udp_test_pending);
    return Plugin_Handled;
}
