#if defined _async2_tcp_included
    #endinput
#endif
#define _async2_tcp_included

// ============================================================================
// TCP Natives
// ============================================================================
// Client: TcpNew(userdata) → SetCallbacks → Connect(host, port) → [onConnect] → Send → [onData] → Close
// Server: TcpNew(userdata) → SetCallbacks → Listen(addr, port, onAccept) → [onAccept(client)] → ...
//
// Accepted client sockets inherit the listener's onData/onError/onClose callbacks
// and chunk size. Set new callbacks on the client handle if needed.
// DNS resolution is async via c-ares (not curl). Errors → onError callback.
// All callbacks fire on the game thread.

// Callback typesets for TCP
typeset TcpConnectCallback {
    function void(TcpSocket socket, any userdata);
};

typeset TcpDataCallback {
    function void(TcpSocket socket, const char[] data, int length, any userdata);
};

typeset TcpErrorCallback {
    function void(TcpSocket socket, int error, const char[] msg, any userdata);
};

typeset TcpCloseCallback {
    function void(TcpSocket socket, bool userClosed, any userdata);
};

typeset TcpAcceptCallback {
    function void(TcpSocket listener, TcpSocket client, const char[] addr, int port, any userdata);
};

/**
 * Creates a new TCP socket. Must be closed with async2_TcpClose when done.
 * @param userdata  Passed through to all callbacks as the last parameter.
 */
native TcpSocket async2_TcpNew(any userdata = 0);

/**
 * Sets all four event callbacks. Must be called before Connect or Listen.
 */
native int async2_TcpSetCallbacks(TcpSocket socket, TcpConnectCallback onConnect,
    TcpDataCallback onData, TcpErrorCallback onError, TcpCloseCallback onClose);

/**
 * Connects to a remote host. DNS resolution is async (c-ares).
 * The onConnect callback fires when the connection is established.
 * Hostnames and IP addresses are both supported.
 */
native int async2_TcpConnect(TcpSocket socket, const char[] host, int port);

/**
 * Binds a TCP socket to a specific local address/port before connecting.
 * Must be called BEFORE Connect. DNS resolution is async.
 * Use "0.0.0.0" with port 0 for OS-assigned ephemeral binding.
 *
 * @param addr  Local address to bind to (hostname or IP).
 * @param port  Local port (0 = OS-assigned).
 */
native int async2_TcpBind(TcpSocket socket, const char[] addr, int port);

/**
 * Sends raw bytes on a connected socket. Data is copied internally.
 * @param data      Raw byte buffer (may contain nulls).
 * @param length    Number of bytes to send.
 */
native int async2_TcpSend(TcpSocket socket, const char[] data, int length);

/**
 * Closes a TCP socket (client or server). The onClose callback always fires
 * after the socket is fully closed, even for user-initiated close.
 * Safe to call at any time.
 */
native int async2_TcpClose(TcpSocket socket);

/**
 * Starts listening for incoming TCP connections.
 * @param addr      Local bind address ("0.0.0.0" for all interfaces).
 * @param port      Local port to listen on.
 * @param onAccept  Called for each accepted connection with client handle.
 * @param backlog   Maximum pending connections queue size.
 */
native int async2_TcpListen(TcpSocket socket, const char[] addr, int port,
    TcpAcceptCallback onAccept, int backlog = 128);

/**
 * Sets a socket option.
 * Options 0-3 should be set before Connect/Listen (stored on socket, applied at init).
 * Options 4-5 can be set at any time (routed to event thread if handle exists).
 *
 * @param option    Option identifier (use TCP_* constants).
 * @param value     Option value.
 */
native int async2_TcpSetOption(TcpSocket socket, int option, int value);

// Option constants for async2_TcpSetOption
#define TCP_CHUNK_SIZE      0  // Max bytes per onData callback (default 4096)
#define TCP_NODELAY         1  // Disable Nagle's algorithm (0=off, 1=on)
#define TCP_KEEPALIVE       2  // Enable keepalive probes (0=off, 1=on)
#define TCP_KEEPALIVE_DELAY 3  // Seconds between keepalive probes (default 60)
#define TCP_SEND_BUFSIZE    4  // SO_SNDBUF in bytes (0 = OS default)
#define TCP_RECV_BUFSIZE    5  // SO_RCVBUF in bytes (0 = OS default)

methodmap TcpSocket __nullable__ {
    public TcpSocket(any userdata = 0) {
        return view_as<TcpSocket>(async2_TcpNew(userdata));
    }

    public void SetCallbacks(TcpConnectCallback onConnect, TcpDataCallback onData,
        TcpErrorCallback onError, TcpCloseCallback onClose) {
        async2_TcpSetCallbacks(this, onConnect, onData, onError, onClose);
    }

    public void Connect(const char[] host, int port) {
        async2_TcpConnect(this, host, port);
    }

    public void Bind(const char[] addr, int port) {
        async2_TcpBind(this, addr, port);
    }

    public void Send(const char[] data, int length) {
        async2_TcpSend(this, data, length);
    }

    public void Close() {
        async2_TcpClose(this);
    }

    public void Listen(const char[] addr, int port, TcpAcceptCallback onAccept, int backlog = 128) {
        async2_TcpListen(this, addr, port, onAccept, backlog);
    }

    public void SetOption(int option, int value) {
        async2_TcpSetOption(this, option, value);
    }
}
