package transport_test

import (
	"context"
	"fmt"
	"net"
	"testing"
	"time"

	"github.com/GopherSecurity/gopher-mcp/src/transport"
)

// Test helper to create a test TCP server
func startTestTCPServer(t *testing.T, handler func(net.Conn)) (string, func()) {
	listener, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatalf("Failed to start test server: %v", err)
	}
	
	go func() {
		for {
			conn, err := listener.Accept()
			if err != nil {
				return
			}
			go handler(conn)
		}
	}()
	
	return listener.Addr().String(), func() {
		listener.Close()
	}
}

// Test 1: NewTcpTransport with default config
func TestNewTcpTransport_Default(t *testing.T) {
	config := transport.DefaultTcpConfig()
	tcp := transport.NewTcpTransport(config)
	
	if tcp == nil {
		t.Fatal("NewTcpTransport returned nil")
	}
	
	// Should start disconnected
	if tcp.IsConnected() {
		t.Error("New TCP transport should not be connected")
	}
}

// Test 2: Client connection to server
func TestTcpTransport_ClientConnect(t *testing.T) {
	// Start test server
	serverAddr, cleanup := startTestTCPServer(t, func(conn net.Conn) {
		// Simple echo server
		buf := make([]byte, 1024)
		n, _ := conn.Read(buf)
		conn.Write(buf[:n])
		conn.Close()
	})
	defer cleanup()
	
	// Parse address
	host, port, _ := net.SplitHostPort(serverAddr)
	
	// Create client
	config := transport.DefaultTcpConfig()
	config.Address = host
	config.Port = parsePort(port)
	config.ServerMode = false
	
	tcp := transport.NewTcpTransport(config)
	
	// Connect
	ctx := context.Background()
	err := tcp.Connect(ctx)
	if err != nil {
		t.Fatalf("Connect failed: %v", err)
	}
	
	if !tcp.IsConnected() {
		t.Error("Should be connected after Connect")
	}
	
	// Send and receive
	testData := []byte("Hello TCP")
	err = tcp.Send(testData)
	if err != nil {
		t.Fatalf("Send failed: %v", err)
	}
	
	received, err := tcp.Receive()
	if err != nil {
		t.Fatalf("Receive failed: %v", err)
	}
	
	if string(received) != string(testData) {
		t.Errorf("Received = %s, want %s", received, testData)
	}
	
	// Disconnect
	err = tcp.Disconnect()
	if err != nil {
		t.Fatalf("Disconnect failed: %v", err)
	}
	
	if tcp.IsConnected() {
		t.Error("Should not be connected after Disconnect")
	}
}

// Test 3: Connection timeout
func TestTcpTransport_ConnectTimeout(t *testing.T) {
	config := transport.DefaultTcpConfig()
	config.Address = "192.0.2.1" // Non-routable address
	config.Port = 8080
	config.ConnectTimeout = 100 * time.Millisecond
	
	tcp := transport.NewTcpTransport(config)
	
	ctx := context.Background()
	start := time.Now()
	err := tcp.Connect(ctx)
	duration := time.Since(start)
	
	if err == nil {
		t.Error("Connect to non-routable address should fail")
		tcp.Disconnect()
	}
	
	// Should timeout quickly
	if duration > 500*time.Millisecond {
		t.Errorf("Connect took %v, should timeout faster", duration)
	}
}

// Test 4: Context cancellation
func TestTcpTransport_ContextCancellation(t *testing.T) {
	config := transport.DefaultTcpConfig()
	config.Address = "192.0.2.1"
	config.Port = 8080
	config.ConnectTimeout = 10 * time.Second
	
	tcp := transport.NewTcpTransport(config)
	
	ctx, cancel := context.WithCancel(context.Background())
	
	// Cancel after short delay
	go func() {
		time.Sleep(50 * time.Millisecond)
		cancel()
	}()
	
	start := time.Now()
	err := tcp.Connect(ctx)
	duration := time.Since(start)
	
	if err == nil {
		t.Error("Connect should fail when context cancelled")
		tcp.Disconnect()
	}
	
	// Should cancel quickly
	if duration > 200*time.Millisecond {
		t.Errorf("Connect took %v after cancel", duration)
	}
}

// Test 5: Send when not connected
func TestTcpTransport_SendNotConnected(t *testing.T) {
	config := transport.DefaultTcpConfig()
	tcp := transport.NewTcpTransport(config)
	
	err := tcp.Send([]byte("test"))
	if err == nil {
		t.Error("Send should fail when not connected")
	}
}

// Test 6: Receive when not connected
func TestTcpTransport_ReceiveNotConnected(t *testing.T) {
	config := transport.DefaultTcpConfig()
	tcp := transport.NewTcpTransport(config)
	
	_, err := tcp.Receive()
	if err == nil {
		t.Error("Receive should fail when not connected")
	}
}

// Test 7: Statistics tracking
func TestTcpTransport_Statistics(t *testing.T) {
	// Start test server
	serverAddr, cleanup := startTestTCPServer(t, func(conn net.Conn) {
		buf := make([]byte, 1024)
		for {
			n, err := conn.Read(buf)
			if err != nil {
				break
			}
			conn.Write(buf[:n])
		}
	})
	defer cleanup()
	
	host, port, _ := net.SplitHostPort(serverAddr)
	
	config := transport.DefaultTcpConfig()
	config.Address = host
	config.Port = parsePort(port)
	
	tcp := transport.NewTcpTransport(config)
	
	// Connect
	ctx := context.Background()
	tcp.Connect(ctx)
	defer tcp.Disconnect()
	
	// Send some data
	tcp.Send([]byte("test1"))
	tcp.Send([]byte("test2"))
	
	// Receive responses
	tcp.Receive()
	tcp.Receive()
	
	// Check stats
	stats := tcp.GetStats()
	if stats.BytesSent == 0 {
		t.Error("BytesSent should be > 0")
	}
	if stats.BytesReceived == 0 {
		t.Error("BytesReceived should be > 0")
	}
	if stats.MessagesSent < 2 {
		t.Error("Should have sent at least 2 messages")
	}
	if stats.MessagesReceived < 2 {
		t.Error("Should have received at least 2 messages")
	}
}

// Test 8: Multiple connect/disconnect cycles
func TestTcpTransport_MultipleConnections(t *testing.T) {
	serverAddr, cleanup := startTestTCPServer(t, func(conn net.Conn) {
		conn.Close()
	})
	defer cleanup()
	
	host, port, _ := net.SplitHostPort(serverAddr)
	
	config := transport.DefaultTcpConfig()
	config.Address = host
	config.Port = parsePort(port)
	
	tcp := transport.NewTcpTransport(config)
	ctx := context.Background()
	
	for i := 0; i < 3; i++ {
		// Connect
		err := tcp.Connect(ctx)
		if err != nil {
			t.Errorf("Connect %d failed: %v", i, err)
		}
		
		if !tcp.IsConnected() {
			t.Errorf("Should be connected after Connect %d", i)
		}
		
		// Disconnect
		err = tcp.Disconnect()
		if err != nil {
			t.Errorf("Disconnect %d failed: %v", i, err)
		}
		
		if tcp.IsConnected() {
			t.Errorf("Should not be connected after Disconnect %d", i)
		}
		
		// Small delay between connections
		time.Sleep(10 * time.Millisecond)
	}
}

// Test 9: Close transport
func TestTcpTransport_Close(t *testing.T) {
	config := transport.DefaultTcpConfig()
	tcp := transport.NewTcpTransport(config)
	
	err := tcp.Close()
	if err != nil {
		t.Fatalf("Close failed: %v", err)
	}
	
	// After close, operations should fail
	err = tcp.Connect(context.Background())
	if err == nil {
		t.Error("Connect should fail after Close")
	}
}

// Test 10: Server mode basic
func TestTcpTransport_ServerMode(t *testing.T) {
	config := transport.DefaultTcpConfig()
	config.Address = "127.0.0.1"
	config.Port = 0 // Let OS choose port
	config.ServerMode = true
	
	tcp := transport.NewTcpTransport(config)
	
	ctx := context.Background()
	err := tcp.Connect(ctx) // In server mode, this starts the listener
	if err != nil {
		t.Fatalf("Failed to start server: %v", err)
	}
	defer tcp.Disconnect()
	
	// Server should be "connected" (listening)
	if !tcp.IsConnected() {
		t.Error("Server should be in connected state when listening")
	}
}

// Helper function to parse port string
func parsePort(portStr string) int {
	var port int
	fmt.Sscanf(portStr, "%d", &port)
	return port
}

// Benchmarks

func BenchmarkTcpTransport_Send(b *testing.B) {
	// Start server
	serverAddr, cleanup := startBenchServer()
	defer cleanup()
	
	host, port, _ := net.SplitHostPort(serverAddr)
	
	config := transport.DefaultTcpConfig()
	config.Address = host
	config.Port = parsePort(port)
	
	tcp := transport.NewTcpTransport(config)
	tcp.Connect(context.Background())
	defer tcp.Disconnect()
	
	data := make([]byte, 1024)
	
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		tcp.Send(data)
	}
}

func BenchmarkTcpTransport_Receive(b *testing.B) {
	// Start server that sends data
	serverAddr, cleanup := startBenchServer()
	defer cleanup()
	
	host, port, _ := net.SplitHostPort(serverAddr)
	
	config := transport.DefaultTcpConfig()
	config.Address = host
	config.Port = parsePort(port)
	
	tcp := transport.NewTcpTransport(config)
	tcp.Connect(context.Background())
	defer tcp.Disconnect()
	
	// Prime the server to send data
	tcp.Send([]byte("start"))
	
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		tcp.Receive()
	}
}

func startBenchServer() (string, func()) {
	listener, _ := net.Listen("tcp", "127.0.0.1:0")
	
	go func() {
		for {
			conn, err := listener.Accept()
			if err != nil {
				return
			}
			go func(c net.Conn) {
				buf := make([]byte, 1024)
				for {
					n, err := c.Read(buf)
					if err != nil {
						break
					}
					c.Write(buf[:n])
				}
				c.Close()
			}(conn)
		}
	}()
	
	return listener.Addr().String(), func() {
		listener.Close()
	}
}

