// TCP Tests (async, requires test server on port 8789)

void Test_TCP_Echo() {
    g_tcp_test_pending++;
    TcpSocket sock = new TcpSocket(0);
    sock.SetCallbacks(OnTcpTestConnect, OnTcpTestData, OnTcpTestError, OnTcpTestClose);
    sock.SetOption(TCP_NODELAY, 1);
    sock.SetOption(TCP_CHUNK_SIZE, 8192);
    sock.Connect("127.0.0.1", 8789);
}

public void OnTcpTestConnect(TcpSocket socket, any userdata) {
    char msg[] = "hello tcp";
    socket.Send(msg, 9);
}

public void OnTcpTestData(TcpSocket socket, const char[] data, int length, any userdata) {
    // Verify echo
    char expected[] = "hello tcp";
    bool match = (length == 9);
    if (match) {
        for (int i = 0; i < 9; i++) {
            if (data[i] != expected[i]) { match = false; break; }
        }
    }
    Assert(match, "TCP echo data matches");
    socket.Close();
}

public void OnTcpTestError(TcpSocket socket, int error, const char[] msg, any userdata) {
    g_failed++;
    PrintToServer("[FAIL] TCP echo error: %s", msg);
    g_tcp_test_pending--;
}

public void OnTcpTestClose(TcpSocket socket, bool userClosed, any userdata) {
    g_tcp_test_pending--;
    if (g_tcp_test_pending == 0)
        PrintToServer("  TCP tests done.");
    MaybeFinishAll();
}

void RunTcpTests() {
    Test_TCP_Echo();
}
