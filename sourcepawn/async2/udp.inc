#if defined _async2_udp_included
    #endinput
#endif
#define _async2_udp_included

// ============================================================================
// UDP Natives
// ============================================================================
// UdpNew(userdata) → SetCallbacks → Bind(addr, port) → Send/[onData] → Close
// Bind starts receiving. Send resolves DNS async before sending.
// All callbacks fire on the game thread.

// Callback typesets for UDP
typeset UdpDataCallback {
    function void(UdpSocket socket, const char[] data, int length,
        const char[] addr, int port, any userdata);
};

typeset UdpErrorCallback {
    function void(UdpSocket socket, int error, const char[] msg, any userdata);
};

typeset UdpCloseCallback {
    function void(UdpSocket socket, bool userClosed, any userdata);
};

/**
 * Creates a new UDP socket. Must be closed with async2_UdpClose when done.
 * @param userdata  Passed through to all callbacks as the last parameter.
 */
native UdpSocket async2_UdpNew(any userdata = 0);

/**
 * Sets all three event callbacks. Must be called before Bind.
 */
native int async2_UdpSetCallbacks(UdpSocket socket, UdpDataCallback onData,
    UdpErrorCallback onError, UdpCloseCallback onClose);

/**
 * Binds the socket to a local address and port. Starts receiving datagrams.
 * @param addr  Local bind address ("0.0.0.0" for all interfaces, or "0.0.0.0" with port 0 for ephemeral).
 * @param port  Local port (0 = OS-assigned ephemeral port).
 */
native int async2_UdpBind(UdpSocket socket, const char[] addr, int port);

/**
 * Sends a datagram. DNS resolution for the target is async.
 * @param data      Raw byte buffer (may contain nulls).
 * @param length    Number of bytes to send.
 * @param addr      Destination hostname or IP address.
 * @param port      Destination port.
 */
native int async2_UdpSend(UdpSocket socket, const char[] data, int length,
    const char[] addr, int port);

/**
 * Closes a UDP socket. The onClose callback always fires after cleanup,
 * even for user-initiated close.
 */
native int async2_UdpClose(UdpSocket socket);

/**
 * Sets a socket option.
 * Options 0-1 should be set before Bind (stored on socket, applied at init).
 * Options 2-3 can be set at any time (routed to event thread if handle exists).
 *
 * @param option    Option identifier (use UDP_* constants).
 * @param value     Option value.
 */
native int async2_UdpSetOption(UdpSocket socket, int option, int value);

// Option constants for async2_UdpSetOption
#define UDP_BROADCAST    0  // Enable broadcast sends (0=off, 1=on)
#define UDP_TTL          1  // IP time-to-live (0 = OS default, typically 64)
#define UDP_SEND_BUFSIZE 2  // SO_SNDBUF in bytes (0 = OS default)
#define UDP_RECV_BUFSIZE 3  // SO_RCVBUF in bytes (0 = OS default)

methodmap UdpSocket __nullable__ {
    public UdpSocket(any userdata = 0) {
        return view_as<UdpSocket>(async2_UdpNew(userdata));
    }

    public void SetCallbacks(UdpDataCallback onData, UdpErrorCallback onError, UdpCloseCallback onClose) {
        async2_UdpSetCallbacks(this, onData, onError, onClose);
    }

    public void Bind(const char[] addr, int port) {
        async2_UdpBind(this, addr, port);
    }

    public void Send(const char[] data, int length, const char[] addr, int port) {
        async2_UdpSend(this, data, length, addr, port);
    }

    public void Close() {
        async2_UdpClose(this);
    }

    public void SetOption(int option, int value) {
        async2_UdpSetOption(this, option, value);
    }
}
