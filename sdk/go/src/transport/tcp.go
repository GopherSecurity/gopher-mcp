// Package transport provides communication transports for the MCP Filter SDK.
package transport

import (
	"context"
	"fmt"
	"net"
	"sync"
	"time"
)

// TcpTransport implements Transport using TCP sockets.
type TcpTransport struct {
	TransportBase
	
	// Connection
	conn     net.Conn
	address  string
	listener net.Listener // For server mode
	
	// Configuration
	config TcpConfig
	
	// Reconnection
	reconnectTimer *time.Timer
	reconnectMu    sync.Mutex
	
	// Mode
	isServer bool
	
	// Synchronization
	mu sync.RWMutex
}

// TcpConfig configures TCP transport behavior.
type TcpConfig struct {
	// Connection settings
	Address         string
	Port            int
	KeepAlive       bool
	KeepAlivePeriod time.Duration
	NoDelay         bool // TCP_NODELAY
	
	// Timeouts
	ConnectTimeout time.Duration
	ReadTimeout    time.Duration
	WriteTimeout   time.Duration
	
	// Buffer sizes
	ReadBufferSize  int
	WriteBufferSize int
	
	// Server mode settings
	ServerMode  bool
	MaxClients  int
	ReuseAddr   bool
	ReusePort   bool
	
	// Reconnection
	EnableReconnect   bool
	ReconnectInterval time.Duration
	MaxReconnectDelay time.Duration
}

// DefaultTcpConfig returns default TCP configuration.
func DefaultTcpConfig() TcpConfig {
	return TcpConfig{
		Address:           "localhost",
		Port:              8080,
		KeepAlive:         true,
		KeepAlivePeriod:   30 * time.Second,
		NoDelay:           true,
		ConnectTimeout:    10 * time.Second,
		ReadTimeout:       0, // No timeout
		WriteTimeout:      0, // No timeout
		ReadBufferSize:    4096,
		WriteBufferSize:   4096,
		ServerMode:        false,
		MaxClients:        100,
		ReuseAddr:         true,
		ReusePort:         false,
		EnableReconnect:   true,
		ReconnectInterval: 5 * time.Second,
		MaxReconnectDelay: 60 * time.Second,
	}
}

// NewTcpTransport creates a new TCP transport.
func NewTcpTransport(config TcpConfig) *TcpTransport {
	baseConfig := DefaultTransportConfig()
	baseConfig.ReadBufferSize = config.ReadBufferSize
	baseConfig.WriteBufferSize = config.WriteBufferSize
	
	// Format address
	address := fmt.Sprintf("%s:%d", config.Address, config.Port)
	
	return &TcpTransport{
		TransportBase: NewTransportBase(baseConfig),
		address:       address,
		config:        config,
		isServer:      config.ServerMode,
	}
}

// Connect establishes TCP connection (client mode) or starts listener (server mode).
func (t *TcpTransport) Connect(ctx context.Context) error {
	if t.isServer {
		return t.startServer(ctx)
	}
	return t.connectClient(ctx)
}

// connectClient establishes client TCP connection.
func (t *TcpTransport) connectClient(ctx context.Context) error {
	// Check if already connected
	if !t.SetConnected(true) {
		return ErrAlreadyConnected
	}
	
	// Create dialer with timeout
	dialer := &net.Dialer{
		Timeout:   t.config.ConnectTimeout,
		KeepAlive: t.config.KeepAlivePeriod,
	}
	
	// Connect with context
	conn, err := dialer.DialContext(ctx, "tcp", t.address)
	if err != nil {
		t.SetConnected(false)
		return &TransportError{
			Code:    "TCP_CONNECT_ERROR",
			Message: fmt.Sprintf("failed to connect to %s", t.address),
			Cause:   err,
		}
	}
	
	// Configure connection
	if err := t.configureConnection(conn); err != nil {
		conn.Close()
		t.SetConnected(false)
		return err
	}
	
	t.mu.Lock()
	t.conn = conn
	t.mu.Unlock()
	
	// Update statistics
	t.UpdateConnectTime()
	t.SetCustomMetric("remote_addr", conn.RemoteAddr().String())
	t.SetCustomMetric("local_addr", conn.LocalAddr().String())
	
	// Start reconnection monitoring if enabled
	if t.config.EnableReconnect {
		t.startReconnectMonitor()
	}
	
	return nil
}

// startServer starts TCP listener in server mode.
func (t *TcpTransport) startServer(ctx context.Context) error {
	// Check if already connected
	if !t.SetConnected(true) {
		return ErrAlreadyConnected
	}
	
	// Configure listener
	lc := net.ListenConfig{
		KeepAlive: t.config.KeepAlivePeriod,
	}
	
	// Set socket options
	if t.config.ReuseAddr || t.config.ReusePort {
		lc.Control = t.setSocketOptions
	}
	
	// Start listening
	listener, err := lc.Listen(ctx, "tcp", t.address)
	if err != nil {
		t.SetConnected(false)
		return &TransportError{
			Code:    "TCP_LISTEN_ERROR",
			Message: fmt.Sprintf("failed to listen on %s", t.address),
			Cause:   err,
		}
	}
	
	t.mu.Lock()
	t.listener = listener
	t.mu.Unlock()
	
	// Update statistics
	t.UpdateConnectTime()
	t.SetCustomMetric("listen_addr", listener.Addr().String())
	
	// Accept connections in background
	go t.acceptConnections(ctx)
	
	return nil
}

// configureConnection applies TCP configuration to connection.
func (t *TcpTransport) configureConnection(conn net.Conn) error {
	tcpConn, ok := conn.(*net.TCPConn)
	if !ok {
		return fmt.Errorf("not a TCP connection")
	}
	
	// Set keep-alive
	if t.config.KeepAlive {
		if err := tcpConn.SetKeepAlive(true); err != nil {
			return err
		}
		if err := tcpConn.SetKeepAlivePeriod(t.config.KeepAlivePeriod); err != nil {
			return err
		}
	}
	
	// Set no delay (disable Nagle's algorithm)
	if t.config.NoDelay {
		if err := tcpConn.SetNoDelay(true); err != nil {
			return err
		}
	}
	
	// Set buffer sizes
	if t.config.ReadBufferSize > 0 {
		if err := tcpConn.SetReadBuffer(t.config.ReadBufferSize); err != nil {
			return err
		}
	}
	if t.config.WriteBufferSize > 0 {
		if err := tcpConn.SetWriteBuffer(t.config.WriteBufferSize); err != nil {
			return err
		}
	}
	
	return nil
}

// acceptConnections accepts incoming connections in server mode.
func (t *TcpTransport) acceptConnections(ctx context.Context) {
	for {
		select {
		case <-ctx.Done():
			return
		default:
		}
		
		t.mu.RLock()
		listener := t.listener
		t.mu.RUnlock()
		
		if listener == nil {
			return
		}
		
		conn, err := listener.Accept()
		if err != nil {
			// Check if listener was closed
			if ne, ok := err.(net.Error); ok && ne.Temporary() {
				continue
			}
			return
		}
		
		// Configure new connection
		if err := t.configureConnection(conn); err != nil {
			conn.Close()
			continue
		}
		
		// Handle connection (for now, just store first connection)
		t.mu.Lock()
		if t.conn == nil {
			t.conn = conn
			t.SetCustomMetric("client_addr", conn.RemoteAddr().String())
		} else {
			// In multi-client mode, would handle differently
			conn.Close()
		}
		t.mu.Unlock()
	}
}

// Send writes data to TCP connection.
func (t *TcpTransport) Send(data []byte) error {
	t.mu.RLock()
	conn := t.conn
	t.mu.RUnlock()
	
	if conn == nil {
		return ErrNotConnected
	}
	
	// Set write timeout if configured
	if t.config.WriteTimeout > 0 {
		conn.SetWriteDeadline(time.Now().Add(t.config.WriteTimeout))
	}
	
	n, err := conn.Write(data)
	if err != nil {
		t.RecordSendError()
		t.handleConnectionError(err)
		return &TransportError{
			Code:    "TCP_WRITE_ERROR",
			Message: "failed to write to TCP connection",
			Cause:   err,
		}
	}
	
	t.RecordBytesSent(n)
	return nil
}

// Receive reads data from TCP connection.
func (t *TcpTransport) Receive() ([]byte, error) {
	t.mu.RLock()
	conn := t.conn
	t.mu.RUnlock()
	
	if conn == nil {
		return nil, ErrNotConnected
	}
	
	// Set read timeout if configured
	if t.config.ReadTimeout > 0 {
		conn.SetReadDeadline(time.Now().Add(t.config.ReadTimeout))
	}
	
	buffer := make([]byte, t.config.ReadBufferSize)
	n, err := conn.Read(buffer)
	if err != nil {
		t.RecordReceiveError()
		t.handleConnectionError(err)
		return nil, &TransportError{
			Code:    "TCP_READ_ERROR",
			Message: "failed to read from TCP connection",
			Cause:   err,
		}
	}
	
	t.RecordBytesReceived(n)
	return buffer[:n], nil
}

// Disconnect closes TCP connection or listener.
func (t *TcpTransport) Disconnect() error {
	if !t.SetConnected(false) {
		return nil // Already disconnected
	}
	
	// Stop reconnection timer
	t.stopReconnectMonitor()
	
	t.mu.Lock()
	defer t.mu.Unlock()
	
	// Close connection
	if t.conn != nil {
		t.conn.Close()
		t.conn = nil
	}
	
	// Close listener in server mode
	if t.listener != nil {
		t.listener.Close()
		t.listener = nil
	}
	
	// Update statistics
	t.UpdateDisconnectTime()
	
	return nil
}

// handleConnectionError handles connection failures.
func (t *TcpTransport) handleConnectionError(err error) {
	if ne, ok := err.(net.Error); ok {
		if ne.Timeout() {
			t.SetCustomMetric("last_error", "timeout")
		} else {
			t.SetCustomMetric("last_error", "network_error")
		}
	}
	
	// Trigger reconnection if enabled
	if t.config.EnableReconnect && !t.isServer {
		t.scheduleReconnect()
	}
}

// startReconnectMonitor starts monitoring for reconnection.
func (t *TcpTransport) startReconnectMonitor() {
	// Monitor connection health periodically
	go func() {
		ticker := time.NewTicker(t.config.KeepAlivePeriod)
		defer ticker.Stop()
		
		for t.IsConnected() {
			<-ticker.C
			
			t.mu.RLock()
			conn := t.conn
			t.mu.RUnlock()
			
			if conn == nil {
				t.scheduleReconnect()
			}
		}
	}()
}

// stopReconnectMonitor stops reconnection monitoring.
func (t *TcpTransport) stopReconnectMonitor() {
	t.reconnectMu.Lock()
	defer t.reconnectMu.Unlock()
	
	if t.reconnectTimer != nil {
		t.reconnectTimer.Stop()
		t.reconnectTimer = nil
	}
}

// scheduleReconnect schedules a reconnection attempt.
func (t *TcpTransport) scheduleReconnect() {
	t.reconnectMu.Lock()
	defer t.reconnectMu.Unlock()
	
	if t.reconnectTimer != nil {
		return // Already scheduled
	}
	
	t.reconnectTimer = time.AfterFunc(t.config.ReconnectInterval, func() {
		t.reconnectMu.Lock()
		t.reconnectTimer = nil
		t.reconnectMu.Unlock()
		
		// Attempt reconnection
		ctx, cancel := context.WithTimeout(context.Background(), t.config.ConnectTimeout)
		defer cancel()
		
		t.Disconnect()
		t.Connect(ctx)
	})
}

// setSocketOptions sets socket-level options.
func (t *TcpTransport) setSocketOptions(network, address string, c net.Conn) error {
	// Platform-specific socket options would be set here
	// For now, just return nil
	return nil
}

// Close closes the transport.
func (t *TcpTransport) Close() error {
	return t.Disconnect()
}