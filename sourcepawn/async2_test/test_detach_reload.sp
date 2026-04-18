// test_detach_reload.sp — Integration test: detached HTTP request survives plugin reload.
//
// Verifies that async2_SetHandlePlugin(h, INVALID_HANDLE) detaches a request
// from plugin ownership such that when the plugin unloads, the request
// continues running on the event thread to completion.
//
// Mechanism:
//   Phase 1 (sm_async2_test_detach_reload command):
//     - Fetch baseline counter from test server
//     - Store baseline in convar `async2_test_reload_baseline`, set
//       `async2_test_reload_check` to 1
//     - Fire detached POST /counter/increment (server sleeps 1s then +=1)
//     - Schedule ServerCommand("sm plugins reload async2_test") after 200ms,
//       guaranteeing the reload races the in-flight request
//
//   Plugin unloads → detach skips handle in OnPluginUnloaded → event thread
//   continues → /counter/increment completes server-side
//
//   Phase 2 (OnPluginStart of the reloaded plugin):
//     - If check convar == 1: reset it, wait 2s, GET /counter
//     - Assert counter == baseline + 1 (detached request survived reload)

ConVar g_cv_reload_check;
ConVar g_cv_reload_baseline;

int g_detach_baseline;

void InitDetachReloadTest() {
    g_cv_reload_check = CreateConVar("async2_test_reload_check", "0",
        "1 = plugin should verify detach counter on load");
    g_cv_reload_baseline = CreateConVar("async2_test_reload_baseline", "0",
        "counter baseline for detach reload test");

    RegServerCmd("sm_async2_test_detach_reload", Command_DetachReload,
        "Run detach+reload integration test");

    // Verification phase — plugin just loaded, check if we're in the middle of a test.
    if (g_cv_reload_check.IntValue == 1) {
        g_cv_reload_check.IntValue = 0;
        g_detach_baseline = g_cv_reload_baseline.IntValue;
        PrintToServer("[INFO] Detach reload: verification phase — baseline was %d", g_detach_baseline);
        // Server /counter/increment sleeps 1s; wait a bit longer to ensure it finished.
        CreateTimer(2.0, Timer_VerifyDetachCounter);
    }
}

public Action Timer_VerifyDetachCounter(Handle timer) {
    WebRequest req = async2_HttpNew();
    req.Execute("GET", "http://127.0.0.1:8787/counter", OnVerifyCounter);
    return Plugin_Stop;
}

public void OnVerifyCounter(WebRequest req, int curlcode, int httpcode, int size) {
    PrintToServer("========================================");
    PrintToServer("  async2 detach-reload verification");
    PrintToServer("========================================");

    if (curlcode != 0 || httpcode != 200) {
        PrintToServer("[FAIL] Detach reload: counter fetch failed curlcode=%d httpcode=%d",
            curlcode, httpcode);
        return;
    }

    Json j = Json.ParseResponse(req);
    if (view_as<int>(j) == 0) {
        PrintToServer("[FAIL] Detach reload: counter response parse failed");
        return;
    }
    int counter_now = j.GetInt("counter");
    j.Close();

    int expected = g_detach_baseline + 1;
    if (counter_now == expected) {
        PrintToServer("[PASS] Detach reload: counter %d -> %d (detached request survived plugin reload)",
            g_detach_baseline, counter_now);
    } else {
        PrintToServer("[FAIL] Detach reload: counter expected %d, got %d (baseline %d)",
            expected, counter_now, g_detach_baseline);
    }
    PrintToServer("========================================");
}

public Action Command_DetachReload(int args) {
    PrintToServer("========================================");
    PrintToServer("  async2 detach-reload test (phase 1)");
    PrintToServer("========================================");

    WebRequest req = async2_HttpNew();
    req.Execute("GET", "http://127.0.0.1:8787/counter", OnDetachBaseline);
    return Plugin_Handled;
}

public void OnDetachBaseline(WebRequest req, int curlcode, int httpcode, int size) {
    if (curlcode != 0 || httpcode != 200) {
        PrintToServer("[FAIL] Detach reload: baseline fetch failed curlcode=%d", curlcode);
        return;
    }

    Json j = Json.ParseResponse(req);
    int baseline = j.GetInt("counter");
    j.Close();

    PrintToServer("[INFO] Detach reload: baseline counter = %d", baseline);
    g_cv_reload_baseline.IntValue = baseline;
    g_cv_reload_check.IntValue = 1;

    // Fire the detached increment. Server sleeps 1s before incrementing.
    WebRequest incReq = async2_HttpNew();
    incReq.SetOptInt(CURLOPT_TIMEOUT_MS, 10000);
    incReq.Execute("POST", "http://127.0.0.1:8787/counter/increment", OnDetachIncrement);
    int detach_ret = async2_SetHandlePlugin(view_as<int>(incReq), INVALID_HANDLE);
    PrintToServer("[INFO] Detach reload: fired increment, detach ret=%d", detach_ret);

    // Reload while the increment is in-flight (server hasn't incremented yet).
    CreateTimer(0.2, Timer_ReloadPlugin);
}

public void OnDetachIncrement(WebRequest req, int curlcode, int httpcode, int size) {
    // This only fires if the plugin is STILL loaded when the request completes.
    // If reload beat it (the normal case), GetSourcepawnFunction returns nullptr
    // and this is silently skipped — that's the behavior under test.
    PrintToServer("[INFO] Detach reload: increment callback ran in original plugin (reload lost race) curlcode=%d",
        curlcode);
}

public Action Timer_ReloadPlugin(Handle timer) {
    PrintToServer("[INFO] Detach reload: reloading plugin (increment still in-flight)");
    ServerCommand("sm plugins reload async2_test");
    return Plugin_Stop;
}
