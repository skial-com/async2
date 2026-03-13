// UDP Tests (async, requires test server on port 8791)

void Test_UDP_Echo() {
    g_udp_test_pending++;
    UdpSocket sock = new UdpSocket(0);
    sock.SetCallbacks(OnUdpTestData, OnUdpTestError, OnUdpTestClose);
    sock.SetOption(UDP_TTL, 64);
    sock.Bind("0.0.0.0", 0);  // bind to any port
    char msg[] = "hello udp";
    sock.Send(msg, 9, "127.0.0.1", 8791);
}

public void OnUdpTestData(UdpSocket socket, const char[] data, int length,
    const char[] addr, int port, any userdata) {
    char expected[] = "hello udp";
    bool match = (length == 9);
    if (match) {
        for (int i = 0; i < 9; i++) {
            if (data[i] != expected[i]) { match = false; break; }
        }
    }
    Assert(match, "UDP echo data matches");
    socket.Close();
}

public void OnUdpTestError(UdpSocket socket, int error, const char[] msg, any userdata) {
    g_failed++;
    PrintToServer("[FAIL] UDP echo error: %s", msg);
    g_udp_test_pending--;
}

public void OnUdpTestClose(UdpSocket socket, bool userClosed, any userdata) {
    g_udp_test_pending--;
    if (g_udp_test_pending == 0)
        PrintToServer("  UDP tests done.");
    MaybeFinishAll();
}

void RunUdpTests() {
    Test_UDP_Echo();
}
