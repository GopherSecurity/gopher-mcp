// Package transport provides communication transports for the MCP Filter SDK.
package transport

import (
	"context"
	"fmt"
	"net/http"
	"sync"
	"time"

	"github.com/gorilla/websocket"
)

// WebSocketTransport implements Transport using WebSocket.
type WebSocketTransport struct {
	TransportBase

	// Connection
	conn     *websocket.Conn
	dialer   *websocket.Dialer
	upgrader *websocket.Upgrader

	// Configuration
	config WebSocketConfig

	// Message handling
	messageType int
	readBuffer  chan []byte
	writeBuffer chan []byte

	// Health monitoring
	pingTicker   *time.Ticker
	pongReceived chan struct{}
	lastPong     time.Time

	// Reconnection
	reconnecting bool
	reconnectMu  sync.Mutex

	mu sync.RWMutex
}

// WebSocketConfig configures WebSocket transport behavior.
type WebSocketConfig struct {
	URL          string
	Subprotocols []string
	Headers      http.Header

	// Message types
	MessageType int // websocket.TextMessage or websocket.BinaryMessage

	// Ping/Pong
	EnablePingPong bool
	PingInterval   time.Duration
	PongTimeout    time.Duration

	// Compression
	EnableCompression bool
	CompressionLevel  int

	// Reconnection
	EnableReconnection   bool
	ReconnectInterval    time.Duration
	MaxReconnectAttempts int

	// Buffering
	ReadBufferSize   int
	WriteBufferSize  int
	MessageQueueSize int

	// Server mode
	ServerMode    bool
	ListenAddress string
}

// DefaultWebSocketConfig returns default WebSocket configuration.
func DefaultWebSocketConfig() WebSocketConfig {
	return WebSocketConfig{
		URL:                  "ws://localhost:8080/ws",
		MessageType:          websocket.BinaryMessage,
		EnablePingPong:       true,
		PingInterval:         30 * time.Second,
		PongTimeout:          10 * time.Second,
		EnableCompression:    true,
		CompressionLevel:     1,
		EnableReconnection:   true,
		ReconnectInterval:    5 * time.Second,
		MaxReconnectAttempts: 10,
		ReadBufferSize:       4096,
		WriteBufferSize:      4096,
		MessageQueueSize:     100,
		ServerMode:           false,
	}
}

// NewWebSocketTransport creates a new WebSocket transport.
func NewWebSocketTransport(config WebSocketConfig) *WebSocketTransport {
	baseConfig := DefaultTransportConfig()

	dialer := &websocket.Dialer{
		ReadBufferSize:    config.ReadBufferSize,
		WriteBufferSize:   config.WriteBufferSize,
		HandshakeTimeout:  10 * time.Second,
		Subprotocols:      config.Subprotocols,
		EnableCompression: config.EnableCompression,
	}

	upgrader := &websocket.Upgrader{
		ReadBufferSize:    config.ReadBufferSize,
		WriteBufferSize:   config.WriteBufferSize,
		CheckOrigin:       func(r *http.Request) bool { return true },
		EnableCompression: config.EnableCompression,
		Subprotocols:      config.Subprotocols,
	}

	return &WebSocketTransport{
		TransportBase: NewTransportBase(baseConfig),
		dialer:        dialer,
		upgrader:      upgrader,
		config:        config,
		messageType:   config.MessageType,
		readBuffer:    make(chan []byte, config.MessageQueueSize),
		writeBuffer:   make(chan []byte, config.MessageQueueSize),
		pongReceived:  make(chan struct{}, 1),
	}
}

// Connect establishes WebSocket connection.
func (wst *WebSocketTransport) Connect(ctx context.Context) error {
	if !wst.SetConnected(true) {
		return ErrAlreadyConnected
	}

	if wst.config.ServerMode {
		return wst.startServer(ctx)
	}

	// Connect to WebSocket server
	conn, resp, err := wst.dialer.DialContext(ctx, wst.config.URL, wst.config.Headers)
	if err != nil {
		wst.SetConnected(false)
		return &TransportError{
			Code:    "WS_CONNECT_ERROR",
			Message: fmt.Sprintf("failed to connect to %s", wst.config.URL),
			Cause:   err,
		}
	}

	if resp != nil && resp.StatusCode != http.StatusSwitchingProtocols {
		wst.SetConnected(false)
		return fmt.Errorf("unexpected status code: %d", resp.StatusCode)
	}

	wst.mu.Lock()
	wst.conn = conn
	wst.mu.Unlock()

	// Configure connection
	if wst.config.EnableCompression {
		conn.EnableWriteCompression(true)
		conn.SetCompressionLevel(wst.config.CompressionLevel)
	}

	// Set handlers
	conn.SetPongHandler(wst.handlePong)
	conn.SetCloseHandler(wst.handleClose)

	// Start goroutines
	go wst.readLoop()
	go wst.writeLoop()

	if wst.config.EnablePingPong {
		wst.startPingPong()
	}

	wst.UpdateConnectTime()
	return nil
}

// startServer starts WebSocket server.
func (wst *WebSocketTransport) startServer(ctx context.Context) error {
	http.HandleFunc("/ws", func(w http.ResponseWriter, r *http.Request) {
		conn, err := wst.upgrader.Upgrade(w, r, nil)
		if err != nil {
			return
		}

		wst.mu.Lock()
		wst.conn = conn
		wst.mu.Unlock()

		// Configure connection
		if wst.config.EnableCompression {
			conn.EnableWriteCompression(true)
			conn.SetCompressionLevel(wst.config.CompressionLevel)
		}

		// Set handlers
		conn.SetPongHandler(wst.handlePong)
		conn.SetCloseHandler(wst.handleClose)

		// Start processing
		go wst.readLoop()
		go wst.writeLoop()

		if wst.config.EnablePingPong {
			wst.startPingPong()
		}
	})

	go http.ListenAndServe(wst.config.ListenAddress, nil)
	return nil
}

// Send sends data via WebSocket.
func (wst *WebSocketTransport) Send(data []byte) error {
	if !wst.IsConnected() {
		return ErrNotConnected
	}

	select {
	case wst.writeBuffer <- data:
		return nil
	case <-time.After(time.Second):
		return fmt.Errorf("write buffer full")
	}
}

// Receive receives data from WebSocket.
func (wst *WebSocketTransport) Receive() ([]byte, error) {
	if !wst.IsConnected() {
		return nil, ErrNotConnected
	}

	select {
	case data := <-wst.readBuffer:
		wst.RecordBytesReceived(len(data))
		return data, nil
	case <-time.After(time.Second):
		return nil, fmt.Errorf("no data available")
	}
}

// readLoop continuously reads from WebSocket.
func (wst *WebSocketTransport) readLoop() {
	defer wst.handleDisconnection()

	for {
		wst.mu.RLock()
		conn := wst.conn
		wst.mu.RUnlock()

		if conn == nil {
			return
		}

		messageType, data, err := conn.ReadMessage()
		if err != nil {
			if websocket.IsUnexpectedCloseError(err, websocket.CloseGoingAway, websocket.CloseAbnormalClosure) {
				wst.RecordReceiveError()
			}
			return
		}

		// Handle different message types
		switch messageType {
		case websocket.TextMessage, websocket.BinaryMessage:
			select {
			case wst.readBuffer <- data:
			default:
				// Buffer full, drop message
			}
		case websocket.PingMessage:
			// Pong is sent automatically by the library
		case websocket.PongMessage:
			// Handled by PongHandler
		}
	}
}

// writeLoop continuously writes to WebSocket.
func (wst *WebSocketTransport) writeLoop() {
	ticker := time.NewTicker(time.Second)
	defer ticker.Stop()

	for {
		select {
		case data := <-wst.writeBuffer:
			wst.mu.RLock()
			conn := wst.conn
			wst.mu.RUnlock()

			if conn == nil {
				return
			}

			conn.SetWriteDeadline(time.Now().Add(10 * time.Second))
			if err := conn.WriteMessage(wst.messageType, data); err != nil {
				wst.RecordSendError()
				return
			}
			wst.RecordBytesSent(len(data))

		case <-ticker.C:
			// Periodic flush or keepalive
		}
	}
}

// startPingPong starts ping/pong health monitoring.
func (wst *WebSocketTransport) startPingPong() {
	wst.pingTicker = time.NewTicker(wst.config.PingInterval)

	go func() {
		for range wst.pingTicker.C {
			wst.mu.RLock()
			conn := wst.conn
			wst.mu.RUnlock()

			if conn == nil {
				return
			}

			conn.SetWriteDeadline(time.Now().Add(10 * time.Second))
			if err := conn.WriteMessage(websocket.PingMessage, nil); err != nil {
				wst.handleDisconnection()
				return
			}

			// Wait for pong
			select {
			case <-wst.pongReceived:
				wst.lastPong = time.Now()
			case <-time.After(wst.config.PongTimeout):
				// Pong timeout, connection unhealthy
				wst.handleDisconnection()
				return
			}
		}
	}()
}

// handlePong handles pong messages.
func (wst *WebSocketTransport) handlePong(appData string) error {
	select {
	case wst.pongReceived <- struct{}{}:
	default:
	}
	return nil
}

// handleClose handles connection close.
func (wst *WebSocketTransport) handleClose(code int, text string) error {
	wst.handleDisconnection()
	return nil
}

// handleDisconnection handles disconnection and reconnection.
func (wst *WebSocketTransport) handleDisconnection() {
	wst.reconnectMu.Lock()
	if wst.reconnecting {
		wst.reconnectMu.Unlock()
		return
	}
	wst.reconnecting = true
	wst.reconnectMu.Unlock()

	// Close current connection
	wst.mu.Lock()
	if wst.conn != nil {
		wst.conn.Close()
		wst.conn = nil
	}
	wst.mu.Unlock()

	wst.SetConnected(false)

	// Attempt reconnection if enabled
	if wst.config.EnableReconnection {
		go wst.attemptReconnection()
	}
}

// attemptReconnection attempts to reconnect.
func (wst *WebSocketTransport) attemptReconnection() {
	defer func() {
		wst.reconnectMu.Lock()
		wst.reconnecting = false
		wst.reconnectMu.Unlock()
	}()

	for i := 0; i < wst.config.MaxReconnectAttempts; i++ {
		time.Sleep(wst.config.ReconnectInterval)

		ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
		err := wst.Connect(ctx)
		cancel()

		if err == nil {
			return
		}
	}
}

// Disconnect closes WebSocket connection.
func (wst *WebSocketTransport) Disconnect() error {
	if !wst.SetConnected(false) {
		return nil
	}

	// Stop ping/pong
	if wst.pingTicker != nil {
		wst.pingTicker.Stop()
	}

	wst.mu.Lock()
	if wst.conn != nil {
		// Send close message
		wst.conn.WriteMessage(websocket.CloseMessage, websocket.FormatCloseMessage(websocket.CloseNormalClosure, ""))
		wst.conn.Close()
		wst.conn = nil
	}
	wst.mu.Unlock()

	wst.UpdateDisconnectTime()
	return nil
}

// SetMessageType sets the WebSocket message type.
func (wst *WebSocketTransport) SetMessageType(messageType int) {
	wst.messageType = messageType
}

// IsHealthy checks if connection is healthy.
func (wst *WebSocketTransport) IsHealthy() bool {
	if !wst.IsConnected() {
		return false
	}

	if wst.config.EnablePingPong {
		return time.Since(wst.lastPong) < wst.config.PongTimeout*2
	}

	return true
}
