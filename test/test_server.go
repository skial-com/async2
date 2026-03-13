// Minimal HTTP/HTTPS test server for async2 SourcePawn integration tests.
//
// Run:  go run test/test_server.go [port]    (default 8787)
//       HTTPS runs on port+1 (default 8788) with a self-signed cert.
//
// Ports:
//   8787  — HTTP/1.1
//   8788  — HTTPS/HTTP2 (self-signed cert)
//   8789  — TCP echo (binary-safe, echoes data back as received)
//   8790  — TCP prefix-length echo (4-byte big-endian length + payload)
//   8791  — UDP echo (echoes datagrams back to sender)
//   8792  — WebSocket echo (ws://, echoes text/binary messages back)
//
// HTTP Endpoints:
//   GET  /get              — echoes back query string as JSON
//   POST /post             — echoes back request body and headers as JSON
//   POST /echo             — returns the raw request body as-is
//   GET  /json             — returns a static JSON object
//   GET  /array            — returns a static JSON array
//   GET  /headers          — returns request headers as JSON
//   GET  /status/<code>    — returns the given HTTP status code
//   GET  /large            — returns a large JSON response (~100 keys)
//   GET  /empty            — returns 200 with empty body
//   GET  /slow             — waits 2 seconds before responding
//   GET  /redirect         — 302 redirect to /get
//   GET  /gzip             — returns gzip-compressed JSON response
//   POST /deflate          — accepts deflate-compressed body, echoes decompressed
//   GET  /multi-redirect   — 302 chain: /multi-redirect -> /redirect -> /get
//   POST /stress           — echoes back a sequence number and request body size
//   POST /msgpack-echo     — echoes raw body back as application/x-msgpack
package main

import (
	"bytes"
	"compress/gzip"
	"compress/zlib"
	"crypto/ecdsa"
	"crypto/elliptic"
	"crypto/rand"
	"crypto/sha1"
	"crypto/tls"
	"crypto/x509"
	"crypto/x509/pkix"
	"encoding/base64"
	"encoding/binary"
	"encoding/json"
	"fmt"
	"io"
	"math/big"
	"net"
	"net/http"
	"os"
	"strconv"
	"strings"
	"sync/atomic"
	"time"
)

var stressCounter atomic.Int64

func sendJSON(w http.ResponseWriter, data any, status int) {
	body, _ := json.Marshal(data)
	w.Header().Set("Content-Type", "application/json")
	w.Header().Set("Content-Length", strconv.Itoa(len(body)))
	w.WriteHeader(status)
	w.Write(body)
}

func sendGzipJSON(w http.ResponseWriter, data any) {
	body, _ := json.Marshal(data)
	var buf bytes.Buffer
	gz := gzip.NewWriter(&buf)
	gz.Write(body)
	gz.Close()
	compressed := buf.Bytes()
	w.Header().Set("Content-Type", "application/json")
	w.Header().Set("Content-Encoding", "gzip")
	w.Header().Set("Content-Length", strconv.Itoa(len(compressed)))
	w.WriteHeader(200)
	w.Write(compressed)
}

func headersMap(h http.Header) map[string]string {
	m := make(map[string]string, len(h))
	for k, v := range h {
		m[k] = strings.Join(v, ", ")
	}
	return m
}

func handler(w http.ResponseWriter, r *http.Request) {
	path := r.URL.Path

	switch r.Method {
	case "GET":
		switch {
		case path == "/get":
			args := make(map[string]any)
			for k, v := range r.URL.Query() {
				if len(v) == 1 {
					args[k] = v[0]
				} else {
					args[k] = v
				}
			}
			sendJSON(w, map[string]any{"args": args, "url": r.URL.RequestURI()}, 200)

		case path == "/json":
			sendJSON(w, map[string]any{
				"name": "async2", "version": 1, "active": true,
				"tags": []string{"http", "json"},
			}, 200)

		case path == "/array":
			sendJSON(w, []any{1, 2, 3, "four", true, nil}, 200)

		case path == "/headers":
			sendJSON(w, map[string]any{"headers": headersMap(r.Header)}, 200)

		case strings.HasPrefix(path, "/status/"):
			code, err := strconv.Atoi(strings.TrimPrefix(path, "/status/"))
			if err != nil {
				code = 400
			}
			sendJSON(w, map[string]any{"status": code}, code)

		case path == "/large":
			data := make(map[string]int, 100)
			for i := 0; i < 100; i++ {
				data[fmt.Sprintf("key_%d", i)] = i
			}
			sendJSON(w, data, 200)

		case path == "/empty":
			w.Header().Set("Content-Length", "0")
			w.WriteHeader(200)

		case path == "/slow":
			time.Sleep(2 * time.Second)
			sendJSON(w, map[string]any{"waited": 2}, 200)

		case path == "/redirect":
			http.Redirect(w, r, "/get", 302)

		case path == "/multi-redirect":
			http.Redirect(w, r, "/redirect", 302)

		case path == "/stress":
			seq := stressCounter.Add(1)
			sendJSON(w, map[string]any{"seq": seq}, 200)

		case path == "/gzip":
			sendGzipJSON(w, map[string]any{
				"compressed": true, "encoding": "gzip", "data": "hello gzip",
			})

		default:
			sendJSON(w, map[string]any{"error": "not found"}, 404)
		}

	case "POST", "PUT", "PATCH":
		body, _ := io.ReadAll(r.Body)

		switch {
		case path == "/post":
			var parsed any
			if err := json.Unmarshal(body, &parsed); err != nil {
				parsed = string(body)
			}
			sendJSON(w, map[string]any{
				"data": parsed, "headers": headersMap(r.Header), "size": len(body),
			}, 200)

		case path == "/echo":
			ct := r.Header.Get("Content-Type")
			if ct == "" {
				ct = "application/octet-stream"
			}
			w.Header().Set("Content-Type", ct)
			w.Header().Set("Content-Length", strconv.Itoa(len(body)))
			w.WriteHeader(200)
			w.Write(body)

		case path == "/msgpack-echo":
			w.Header().Set("Content-Type", "application/x-msgpack")
			w.Header().Set("Content-Length", strconv.Itoa(len(body)))
			w.WriteHeader(200)
			w.Write(body)

		case path == "/stress":
			seq := stressCounter.Add(1)
			sendJSON(w, map[string]any{"seq": seq, "size": len(body)}, 200)

		case path == "/deflate":
			encoding := r.Header.Get("Content-Encoding")
			if encoding == "deflate" {
				reader, err := zlib.NewReader(bytes.NewReader(body))
				if err != nil {
					sendJSON(w, map[string]any{"error": fmt.Sprintf("decompress failed: %v", err)}, 400)
					return
				}
				decompressed, err := io.ReadAll(reader)
				reader.Close()
				if err != nil {
					sendJSON(w, map[string]any{"error": fmt.Sprintf("decompress failed: %v", err)}, 400)
				} else {
					sendJSON(w, map[string]any{
						"decompressed":      string(decompressed),
						"compressed_size":   len(body),
						"decompressed_size": len(decompressed),
					}, 200)
				}
			} else {
				sendJSON(w, map[string]any{
					"decompressed":      string(body),
					"compressed_size":   len(body),
					"decompressed_size": len(body),
					"note":             "not compressed",
				}, 200)
			}

		default:
			sendJSON(w, map[string]any{"error": "not found"}, 404)
		}

	case "DELETE":
		sendJSON(w, map[string]any{"deleted": true, "path": path}, 200)

	default:
		sendJSON(w, map[string]any{"error": "method not allowed"}, 405)
	}
}

func generateSelfSignedCert() (tls.Certificate, error) {
	key, err := ecdsa.GenerateKey(elliptic.P256(), rand.Reader)
	if err != nil {
		return tls.Certificate{}, err
	}

	template := &x509.Certificate{
		SerialNumber: big.NewInt(1),
		Subject:      pkix.Name{CommonName: "localhost"},
		NotBefore:    time.Now(),
		NotAfter:     time.Now().Add(24 * time.Hour),
		KeyUsage:     x509.KeyUsageDigitalSignature,
		ExtKeyUsage:  []x509.ExtKeyUsage{x509.ExtKeyUsageServerAuth},
		IPAddresses:  []net.IP{net.ParseIP("127.0.0.1")},
	}

	certDER, err := x509.CreateCertificate(rand.Reader, template, template, &key.PublicKey, key)
	if err != nil {
		return tls.Certificate{}, err
	}

	return tls.Certificate{
		Certificate: [][]byte{certDER},
		PrivateKey:  key,
	}, nil
}

// --- Minimal WebSocket server (RFC 6455) ---

const wsMagicGUID = "258EAFA5-E914-47DA-95CA-5AB5CD63F04B"

func wsAcceptKey(key string) string {
	h := sha1.New()
	h.Write([]byte(key + wsMagicGUID))
	return base64.StdEncoding.EncodeToString(h.Sum(nil))
}

// wsReadFrame reads one WebSocket frame from conn.
// Returns opcode, payload, and error.
func wsReadFrame(conn net.Conn) (opcode byte, payload []byte, err error) {
	header := make([]byte, 2)
	if _, err = io.ReadFull(conn, header); err != nil {
		return
	}
	opcode = header[0] & 0x0F
	masked := (header[1] & 0x80) != 0
	length := uint64(header[1] & 0x7F)

	if length == 126 {
		ext := make([]byte, 2)
		if _, err = io.ReadFull(conn, ext); err != nil {
			return
		}
		length = uint64(ext[0])<<8 | uint64(ext[1])
	} else if length == 127 {
		ext := make([]byte, 8)
		if _, err = io.ReadFull(conn, ext); err != nil {
			return
		}
		length = uint64(ext[0])<<56 | uint64(ext[1])<<48 | uint64(ext[2])<<40 | uint64(ext[3])<<32 |
			uint64(ext[4])<<24 | uint64(ext[5])<<16 | uint64(ext[6])<<8 | uint64(ext[7])
	}

	var mask [4]byte
	if masked {
		if _, err = io.ReadFull(conn, mask[:]); err != nil {
			return
		}
	}

	payload = make([]byte, length)
	if _, err = io.ReadFull(conn, payload); err != nil {
		return
	}
	if masked {
		for i := range payload {
			payload[i] ^= mask[i%4]
		}
	}
	return
}

// wsWriteFrame writes a WebSocket frame (server->client, unmasked).
func wsWriteFrame(conn net.Conn, opcode byte, payload []byte) error {
	var frame []byte
	length := len(payload)
	fin := byte(0x80)

	if length < 126 {
		frame = make([]byte, 2+length)
		frame[0] = fin | opcode
		frame[1] = byte(length)
		copy(frame[2:], payload)
	} else if length < 65536 {
		frame = make([]byte, 4+length)
		frame[0] = fin | opcode
		frame[1] = 126
		frame[2] = byte(length >> 8)
		frame[3] = byte(length)
		copy(frame[4:], payload)
	} else {
		frame = make([]byte, 10+length)
		frame[0] = fin | opcode
		frame[1] = 127
		l := uint64(length)
		frame[2] = byte(l >> 56)
		frame[3] = byte(l >> 48)
		frame[4] = byte(l >> 40)
		frame[5] = byte(l >> 32)
		frame[6] = byte(l >> 24)
		frame[7] = byte(l >> 16)
		frame[8] = byte(l >> 8)
		frame[9] = byte(l)
		copy(frame[10:], payload)
	}
	_, err := conn.Write(frame)
	return err
}

func handleWsConn(conn net.Conn, upgradeHeaders ...map[string]string) {
	defer conn.Close()
	var headers map[string]string
	if len(upgradeHeaders) > 0 {
		headers = upgradeHeaders[0]
	}
	for {
		opcode, payload, err := wsReadFrame(conn)
		if err != nil {
			return
		}
		switch opcode {
		case 0x1, 0x2: // text, binary — echo back
			if opcode == 0x1 && string(payload) == "force_close" {
				conn.Close() // drop TCP without close frame (simulates abnormal disconnect)
				return
			}
			if opcode == 0x1 && string(payload) == "get_headers" && headers != nil {
				resp, _ := json.Marshal(headers)
				if err := wsWriteFrame(conn, 0x1, resp); err != nil {
					return
				}
				continue
			}
			if err := wsWriteFrame(conn, opcode, payload); err != nil {
				return
			}
		case 0x8: // close
			// Echo close frame back
			wsWriteFrame(conn, 0x8, payload)
			return
		case 0x9: // ping — reply with pong
			wsWriteFrame(conn, 0xA, payload)
		case 0xA: // pong — ignore
		}
	}
}

func main() {
	port := 8787
	if len(os.Args) > 1 {
		if p, err := strconv.Atoi(os.Args[1]); err == nil {
			port = p
		}
	}
	httpsPort := port + 1

	mux := http.NewServeMux()
	mux.HandleFunc("/ws", func(w http.ResponseWriter, r *http.Request) {
		// WebSocket upgrade via HTTP mux (for wss:// on the HTTPS port)
		wsKey := r.Header.Get("Sec-WebSocket-Key")
		if wsKey == "" {
			http.Error(w, "Not a WebSocket request", http.StatusBadRequest)
			return
		}
		hj, ok := w.(http.Hijacker)
		if !ok {
			http.Error(w, "Hijacking not supported", http.StatusInternalServerError)
			return
		}
		conn, bufrw, err := hj.Hijack()
		if err != nil {
			http.Error(w, err.Error(), http.StatusInternalServerError)
			return
		}
		resp := "HTTP/1.1 101 Switching Protocols\r\n" +
			"Upgrade: websocket\r\n" +
			"Connection: Upgrade\r\n" +
			"Sec-WebSocket-Accept: " + wsAcceptKey(wsKey) + "\r\n\r\n"
		bufrw.WriteString(resp)
		bufrw.Flush()
		handleWsConn(conn)
	})
	mux.HandleFunc("/", handler)

	// HTTP server
	go func() {
		addr := fmt.Sprintf("127.0.0.1:%d", port)
		fmt.Printf("HTTP test server running on http://%s\n", addr)
		if err := http.ListenAndServe(addr, mux); err != nil {
			fmt.Fprintf(os.Stderr, "HTTP server error: %v\n", err)
			os.Exit(1)
		}
	}()

	// TCP echo server on port 8789: echoes data back exactly as received
	tcpEchoPort := port + 2
	go func() {
		addr := fmt.Sprintf("127.0.0.1:%d", tcpEchoPort)
		ln, err := net.Listen("tcp", addr)
		if err != nil {
			fmt.Fprintf(os.Stderr, "TCP echo server error: %v\n", err)
			return
		}
		fmt.Printf("TCP echo server running on %s\n", addr)
		for {
			conn, err := ln.Accept()
			if err != nil {
				continue
			}
			go func(c net.Conn) {
				defer c.Close()
				buf := make([]byte, 65536)
				for {
					n, err := c.Read(buf)
					if err != nil {
						return
					}
					_, err = c.Write(buf[:n])
					if err != nil {
						return
					}
				}
			}(conn)
		}
	}()

	// TCP prefix-length echo server on port 8790: reads 4-byte big-endian length
	// prefix, then that many bytes of payload. Echoes back in the same format.
	tcpFramedPort := port + 3
	go func() {
		addr := fmt.Sprintf("127.0.0.1:%d", tcpFramedPort)
		ln, err := net.Listen("tcp", addr)
		if err != nil {
			fmt.Fprintf(os.Stderr, "TCP framed echo server error: %v\n", err)
			return
		}
		fmt.Printf("TCP framed echo server running on %s\n", addr)
		for {
			conn, err := ln.Accept()
			if err != nil {
				continue
			}
			go func(c net.Conn) {
				defer c.Close()
				for {
					// Read 4-byte big-endian length prefix
					var length uint32
					if err := binary.Read(c, binary.BigEndian, &length); err != nil {
						return
					}
					if length > 10*1024*1024 { // 10 MB sanity limit
						return
					}

					// Read the payload
					payload := make([]byte, length)
					if _, err := io.ReadFull(c, payload); err != nil {
						return
					}

					// Echo back: 4-byte length + payload
					if err := binary.Write(c, binary.BigEndian, length); err != nil {
						return
					}
					if _, err := c.Write(payload); err != nil {
						return
					}
				}
			}(conn)
		}
	}()

	// UDP echo server on port 8791: echoes datagrams back to sender
	udpEchoPort := port + 4
	go func() {
		addr := fmt.Sprintf("127.0.0.1:%d", udpEchoPort)
		udpAddr, err := net.ResolveUDPAddr("udp", addr)
		if err != nil {
			fmt.Fprintf(os.Stderr, "UDP echo server resolve error: %v\n", err)
			return
		}
		conn, err := net.ListenUDP("udp", udpAddr)
		if err != nil {
			fmt.Fprintf(os.Stderr, "UDP echo server error: %v\n", err)
			return
		}
		defer conn.Close()
		fmt.Printf("UDP echo server running on %s\n", addr)
		buf := make([]byte, 65536)
		for {
			n, remoteAddr, err := conn.ReadFromUDP(buf)
			if err != nil {
				continue
			}
			conn.WriteToUDP(buf[:n], remoteAddr)
		}
	}()

	// WebSocket echo server on port 8792
	wsPort := port + 5
	go func() {
		addr := fmt.Sprintf("127.0.0.1:%d", wsPort)
		ln, err := net.Listen("tcp", addr)
		if err != nil {
			fmt.Fprintf(os.Stderr, "WebSocket echo server error: %v\n", err)
			return
		}
		fmt.Printf("WebSocket echo server running on ws://%s\n", addr)
		for {
			conn, err := ln.Accept()
			if err != nil {
				continue
			}
			go func(c net.Conn) {
				// Read HTTP upgrade request
				buf := make([]byte, 4096)
				n, err := c.Read(buf)
				if err != nil {
					c.Close()
					return
				}
				req := string(buf[:n])

				// Extract headers from upgrade request
				var wsKey string
				hdrs := make(map[string]string)
				for _, line := range strings.Split(req, "\r\n") {
					if idx := strings.Index(line, ":"); idx > 0 {
						key := strings.TrimSpace(line[:idx])
						val := strings.TrimSpace(line[idx+1:])
						lk := strings.ToLower(key)
						hdrs[lk] = val
						if lk == "sec-websocket-key" {
							wsKey = val
						}
					}
				}
				if wsKey == "" {
					c.Close()
					return
				}

				// Send upgrade response
				resp := "HTTP/1.1 101 Switching Protocols\r\n" +
					"Upgrade: websocket\r\n" +
					"Connection: Upgrade\r\n" +
					"Sec-WebSocket-Accept: " + wsAcceptKey(wsKey) + "\r\n\r\n"
				c.Write([]byte(resp))

				handleWsConn(c, hdrs)
			}(conn)
		}
	}()

	// HTTPS server with self-signed cert (pure Go, no openssl needed)
	cert, err := generateSelfSignedCert()
	if err != nil {
		fmt.Fprintf(os.Stderr, "WARNING: could not generate TLS cert: %v, HTTPS tests will be skipped\n", err)
		select {} // block forever, HTTP server runs in goroutine
	}

	tlsConfig := &tls.Config{Certificates: []tls.Certificate{cert}}
	httpsAddr := fmt.Sprintf("127.0.0.1:%d", httpsPort)
	listener, err := tls.Listen("tcp", httpsAddr, tlsConfig)
	if err != nil {
		fmt.Fprintf(os.Stderr, "WARNING: could not start HTTPS: %v\n", err)
		select {}
	}
	fmt.Printf("HTTPS test server running on https://%s\n", httpsAddr)
	if err := http.Serve(listener, mux); err != nil {
		fmt.Fprintf(os.Stderr, "HTTPS server error: %v\n", err)
		os.Exit(1)
	}
}
