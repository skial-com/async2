// Stress Tests — requires test server: go run test/test_server.go

// ============================================================================
// Stress Test
// ============================================================================

#define STRESS_CONCURRENCY 10  // requests in-flight at once

public void OnMapStart() {
    if (!g_stress_running)
        return;

    g_stress_map_gen++;
    PrintToServer("[Stress] Map changed (gen %d) — %d in-flight, waiting for cross-map callbacks...",
        g_stress_map_gen, g_stress_inflight);
}

public Action Command_StressStartNoKeep(int args) {
    g_stress_fresh_conn = true;
    return Command_StressStart(args);
}

public Action Command_StressStart(int args) {
    if (g_stress_running) {
        PrintToServer("[Stress] Already running. Use sm_async2_test_stress_stop to stop.");
        return Plugin_Handled;
    }

    if (g_stress_running) {
        PrintToServer("[Stress] Already running. Use sm_async2_test_stress_stop to stop.");
        g_stress_fresh_conn = false;
        return Plugin_Handled;
    }

    g_stress_running = true;
    g_stress_sent = 0;
    g_stress_completed = 0;
    g_stress_failed = 0;
    g_stress_inflight = 0;
    g_stress_cross_map = 0;
    g_stress_map_gen = 0;
    g_stress_max_inflight = STRESS_CONCURRENCY;
    g_stress_start_time = GetEngineTime();
    g_stress_http_ok = 0;
    g_stress_tcp_ok = 0;
    g_stress_udp_ok = 0;
    g_stress_ws_ok = 0;

    if (args >= 1) {
        char arg[16];
        GetCmdArg(1, arg, sizeof(arg));
        int val = StringToInt(arg);
        if (val > 0)
            g_stress_max_inflight = val;
    }

    PrintToServer("[Stress] Starting — %d concurrent, fresh_conn=%s, protocols=HTTP2+TCP+UDP+WS",
        g_stress_max_inflight, g_stress_fresh_conn ? "yes" : "no");
    PrintToServer("[Stress] Use sm_async2_test_stress_stop to stop");

    // Seed the initial batch
    for (int i = 0; i < g_stress_max_inflight; i++) {
        StressSendOne();
    }

    return Plugin_Handled;
}

public Action Command_StressStop(int args) {
    if (!g_stress_running) {
        PrintToServer("[Stress] Not running.");
        return Plugin_Handled;
    }

    g_stress_running = false;
    g_stress_fresh_conn = false;
    float elapsed = GetEngineTime() - g_stress_start_time;
    float rps = (elapsed > 0.0) ? float(g_stress_completed) / elapsed : 0.0;

    PrintToServer("========================================");
    PrintToServer("  Stress Test Results");
    PrintToServer("========================================");
    PrintToServer("  Sent:       %d", g_stress_sent);
    PrintToServer("  Completed:  %d", g_stress_completed);
    PrintToServer("  Failed:     %d", g_stress_failed);
    PrintToServer("  Cross-map:  %d", g_stress_cross_map);
    PrintToServer("  In-flight:  %d (draining)", g_stress_inflight);
    PrintToServer("  HTTP:       %d ok", g_stress_http_ok);
    PrintToServer("  TCP:        %d ok", g_stress_tcp_ok);
    PrintToServer("  UDP:        %d ok", g_stress_udp_ok);
    PrintToServer("  WS:         %d ok", g_stress_ws_ok);
    int pool_total, pool_in_use, pool_block_size;
    async2_JsonPoolStats(pool_total, pool_in_use, pool_block_size);
    PrintToServer("  Elapsed:    %.1f seconds", elapsed);
    PrintToServer("  Avg RPS:    %.1f", rps);
    PrintToServer("  Pool:       %d/%d nodes (%d KB), block=%d bytes",
        pool_in_use, pool_total, (pool_in_use * pool_block_size) / 1024, pool_block_size);
    PrintToServer("========================================");

    ServerCommand("sm_kick @bots");

    return Plugin_Handled;
}

void StressSendOne() {
    if (!g_stress_running)
        return;

    // Rotate protocols: 0=HTTP GET, 1=HTTP POST, 2=TCP, 3=UDP, 4=WS
    int proto = g_stress_sent % 5;
    g_stress_sent++;
    g_stress_inflight++;

    switch (proto) {
        case 0: {
            // HTTP GET
            WebRequest req = NewRequest(g_stress_map_gen);
            if (g_stress_fresh_conn)
                req.SetOptInt(CURLOPT_FRESH_CONNECT, 1);
            req.Execute("GET", TEST_URL ... "/stress", OnStressHttpCallback);
        }
        case 1: {
            // HTTP POST with JSON body
            WebRequest req = NewRequest(g_stress_map_gen);
            if (g_stress_fresh_conn)
                req.SetOptInt(CURLOPT_FRESH_CONNECT, 1);
            Json body = Json.CreateObject();
            body.SetInt("n", g_stress_sent);
            body.SetString("test", "stress");
            req.SetBodyJSON(body);
            body.Close();
            req.Execute("POST", TEST_URL ... "/stress", OnStressHttpCallback);
        }
        case 2: {
            // TCP echo
            TcpSocket sock = new TcpSocket(g_stress_map_gen);
            sock.SetCallbacks(OnStressTcpConnect, OnStressTcpData, OnStressTcpError, OnStressTcpClose);
            sock.SetOption(TCP_NODELAY, 1);
            sock.Connect("127.0.0.1", 8789);
        }
        case 3: {
            // UDP echo
            UdpSocket sock = new UdpSocket(g_stress_map_gen);
            sock.SetCallbacks(OnStressUdpData, OnStressUdpError, OnStressUdpClose);
            sock.Bind("0.0.0.0", 0);
            char msg[] = "stress-udp";
            sock.Send(msg, 10, "127.0.0.1", 8791);
        }
        case 4: {
            // WebSocket echo
            WsSocket ws = new WsSocket(g_stress_map_gen);
            ws.SetCallbacks(OnStressWsConnect, OnStressWsMessage, OnStressWsError, OnStressWsClose);
            ws.Connect("ws://127.0.0.1:8792");
        }
    }
}

void StressComplete(bool ok) {
    g_stress_inflight--;
    g_stress_completed++;
    if (!ok) g_stress_failed++;

    // Print progress every 500 completions
    if (g_stress_completed % 500 == 0) {
        float elapsed = GetEngineTime() - g_stress_start_time;
        float rps = (elapsed > 0.0) ? float(g_stress_completed) / elapsed : 0.0;
        PrintToServer("[Stress] %d completed (%d failed), %.1f req/s, %d in-flight, H:%d T:%d U:%d W:%d",
            g_stress_completed, g_stress_failed, rps, g_stress_inflight,
            g_stress_http_ok, g_stress_tcp_ok, g_stress_udp_ok, g_stress_ws_ok);
    }

    StressSendOne();
}

// --- HTTP stress callbacks ---
public void OnStressHttpCallback(WebRequest req, int curlcode, int httpcode, int size, any userdata) {
    int sent_gen = view_as<int>(userdata);
    if (sent_gen != g_stress_map_gen) {
        g_stress_cross_map++;
    }
    bool ok = (curlcode == 0 && httpcode == 200);
    if (ok) g_stress_http_ok++;

    StressComplete(ok);
}

// --- TCP stress callbacks ---
public void OnStressTcpConnect(TcpSocket socket, any userdata) {
    char msg[] = "stress-tcp";
    socket.Send(msg, 10);
}

public void OnStressTcpData(TcpSocket socket, const char[] data, int length, any userdata) {
    g_stress_tcp_ok++;
    socket.Close();
}

public void OnStressTcpError(TcpSocket socket, int error, const char[] msg, any userdata) {
    socket.Close();
}

public void OnStressTcpClose(TcpSocket socket, bool userClosed, any userdata) {
    StressComplete(userClosed);  // userClosed=true means data received + Close(), false means error
}

// --- UDP stress callbacks ---
public void OnStressUdpData(UdpSocket socket, const char[] data, int length,
    const char[] addr, int port, any userdata) {
    g_stress_udp_ok++;
    socket.Close();
}

public void OnStressUdpError(UdpSocket socket, int error, const char[] msg, any userdata) {
    socket.Close();
}

public void OnStressUdpClose(UdpSocket socket, bool userClosed, any userdata) {
    StressComplete(userClosed);
}

// --- WS stress callbacks ---
public void OnStressWsConnect(WsSocket socket, any userdata) {
    socket.SendText("stress-ws");
}

public void OnStressWsMessage(WsSocket socket, const char[] data, int length, bool isBinary, any userdata) {
    g_stress_ws_ok++;
    socket.Close();
}

public void OnStressWsError(WsSocket socket, int error, const char[] msg, any userdata) {
    socket.Close();
}

public void OnStressWsClose(WsSocket socket, int code, const char[] reason, any userdata) {
    StressComplete(true);
}
