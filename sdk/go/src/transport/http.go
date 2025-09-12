// Package transport provides communication transports for the MCP Filter SDK.
package transport

import (
	"bytes"
	"context"
	"fmt"
	"io"
	"net/http"
	"sync"
	"time"
)

// HttpTransport implements Transport using HTTP.
type HttpTransport struct {
	TransportBase
	
	// HTTP client
	client *http.Client
	
	// Configuration
	config HttpConfig
	
	// Request/response mapping
	pendingRequests map[string]chan *http.Response
	requestMu       sync.Mutex
	
	// WebSocket upgrade
	wsUpgrader WebSocketUpgrader
	
	// Server mode
	server   *http.Server
	isServer bool
}

// HttpConfig configures HTTP transport behavior.
type HttpConfig struct {
	BaseURL           string
	Endpoint          string
	Method            string
	Headers           map[string]string
	
	// Connection pooling
	MaxIdleConns        int
	MaxConnsPerHost     int
	IdleConnTimeout     time.Duration
	
	// Timeouts
	RequestTimeout      time.Duration
	ResponseTimeout     time.Duration
	
	// Streaming
	EnableStreaming     bool
	ChunkSize          int
	
	// WebSocket
	EnableWebSocketUpgrade bool
	WebSocketPath         string
	
	// Server mode
	ServerMode         bool
	ListenAddress      string
}

// DefaultHttpConfig returns default HTTP configuration.
func DefaultHttpConfig() HttpConfig {
	return HttpConfig{
		BaseURL:         "http://localhost:8080",
		Endpoint:        "/api/transport",
		Method:          "POST",
		MaxIdleConns:    100,
		MaxConnsPerHost: 10,
		IdleConnTimeout: 90 * time.Second,
		RequestTimeout:  30 * time.Second,
		ResponseTimeout: 30 * time.Second,
		ChunkSize:       4096,
		ServerMode:      false,
	}
}

// NewHttpTransport creates a new HTTP transport.
func NewHttpTransport(config HttpConfig) *HttpTransport {
	baseConfig := DefaultTransportConfig()
	
	transport := &http.Transport{
		MaxIdleConns:        config.MaxIdleConns,
		MaxConnsPerHost:     config.MaxConnsPerHost,
		IdleConnTimeout:     config.IdleConnTimeout,
		ResponseHeaderTimeout: config.ResponseTimeout,
	}
	
	client := &http.Client{
		Transport: transport,
		Timeout:   config.RequestTimeout,
	}
	
	return &HttpTransport{
		TransportBase:   NewTransportBase(baseConfig),
		client:          client,
		config:          config,
		pendingRequests: make(map[string]chan *http.Response),
		isServer:        config.ServerMode,
	}
}

// Connect establishes HTTP connection or starts server.
func (ht *HttpTransport) Connect(ctx context.Context) error {
	if !ht.SetConnected(true) {
		return ErrAlreadyConnected
	}
	
	if ht.isServer {
		return ht.startServer(ctx)
	}
	
	// For client mode, test connection
	req, err := http.NewRequestWithContext(ctx, "GET", ht.config.BaseURL+"/health", nil)
	if err != nil {
		ht.SetConnected(false)
		return err
	}
	
	resp, err := ht.client.Do(req)
	if err != nil {
		// Connection failed, but we'll keep trying
		// HTTP is connectionless
	} else {
		resp.Body.Close()
	}
	
	ht.UpdateConnectTime()
	return nil
}

// startServer starts HTTP server in server mode.
func (ht *HttpTransport) startServer(ctx context.Context) error {
	mux := http.NewServeMux()
	
	// Handle transport endpoint
	mux.HandleFunc(ht.config.Endpoint, ht.handleRequest)
	
	// Handle WebSocket upgrade if enabled
	if ht.config.EnableWebSocketUpgrade {
		mux.HandleFunc(ht.config.WebSocketPath, ht.handleWebSocketUpgrade)
	}
	
	ht.server = &http.Server{
		Addr:         ht.config.ListenAddress,
		Handler:      mux,
		ReadTimeout:  ht.config.RequestTimeout,
		WriteTimeout: ht.config.ResponseTimeout,
	}
	
	go func() {
		if err := ht.server.ListenAndServe(); err != nil && err != http.ErrServerClosed {
			// Handle server error
		}
	}()
	
	return nil
}

// Send sends data via HTTP.
func (ht *HttpTransport) Send(data []byte) error {
	if !ht.IsConnected() {
		return ErrNotConnected
	}
	
	ctx, cancel := context.WithTimeout(context.Background(), ht.config.RequestTimeout)
	defer cancel()
	
	url := ht.config.BaseURL + ht.config.Endpoint
	req, err := http.NewRequestWithContext(ctx, ht.config.Method, url, bytes.NewReader(data))
	if err != nil {
		return err
	}
	
	// Add headers
	for key, value := range ht.config.Headers {
		req.Header.Set(key, value)
	}
	req.Header.Set("Content-Type", "application/octet-stream")
	
	// Send request
	resp, err := ht.client.Do(req)
	if err != nil {
		ht.RecordSendError()
		return err
	}
	defer resp.Body.Close()
	
	if resp.StatusCode >= 400 {
		return fmt.Errorf("HTTP error: %d", resp.StatusCode)
	}
	
	ht.RecordBytesSent(len(data))
	
	// Map response if needed
	if ht.config.EnableStreaming {
		ht.mapResponse(req.Header.Get("X-Request-ID"), resp)
	}
	
	return nil
}

// Receive receives data via HTTP.
func (ht *HttpTransport) Receive() ([]byte, error) {
	if !ht.IsConnected() {
		return nil, ErrNotConnected
	}
	
	// For streaming mode, wait for mapped response
	if ht.config.EnableStreaming {
		return ht.receiveStreaming()
	}
	
	// For request-response mode, make GET request
	ctx, cancel := context.WithTimeout(context.Background(), ht.config.RequestTimeout)
	defer cancel()
	
	url := ht.config.BaseURL + ht.config.Endpoint
	req, err := http.NewRequestWithContext(ctx, "GET", url, nil)
	if err != nil {
		return nil, err
	}
	
	resp, err := ht.client.Do(req)
	if err != nil {
		ht.RecordReceiveError()
		return nil, err
	}
	defer resp.Body.Close()
	
	data, err := io.ReadAll(resp.Body)
	if err != nil {
		return nil, err
	}
	
	ht.RecordBytesReceived(len(data))
	return data, nil
}

// receiveStreaming receives data in streaming mode.
func (ht *HttpTransport) receiveStreaming() ([]byte, error) {
	// Implementation for streaming mode
	// Would handle chunked transfer encoding
	buffer := make([]byte, ht.config.ChunkSize)
	
	// Simplified implementation
	return buffer, nil
}

// mapResponse maps HTTP response to request.
func (ht *HttpTransport) mapResponse(requestID string, resp *http.Response) {
	ht.requestMu.Lock()
	defer ht.requestMu.Unlock()
	
	if ch, exists := ht.pendingRequests[requestID]; exists {
		ch <- resp
	}
}

// handleRequest handles incoming HTTP requests in server mode.
func (ht *HttpTransport) handleRequest(w http.ResponseWriter, r *http.Request) {
	// Read request body
	data, err := io.ReadAll(r.Body)
	if err != nil {
		http.Error(w, err.Error(), http.StatusBadRequest)
		return
	}
	defer r.Body.Close()
	
	// Process data
	ht.RecordBytesReceived(len(data))
	
	// Send response
	w.WriteHeader(http.StatusOK)
	w.Write([]byte("OK"))
}

// handleWebSocketUpgrade handles WebSocket upgrade requests.
func (ht *HttpTransport) handleWebSocketUpgrade(w http.ResponseWriter, r *http.Request) {
	if ht.wsUpgrader != nil {
		ht.wsUpgrader.Upgrade(w, r)
	}
}

// Disconnect closes HTTP connection or stops server.
func (ht *HttpTransport) Disconnect() error {
	if !ht.SetConnected(false) {
		return nil
	}
	
	if ht.server != nil {
		ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
		defer cancel()
		ht.server.Shutdown(ctx)
	}
	
	ht.UpdateDisconnectTime()
	return nil
}

// WebSocketUpgrader handles WebSocket upgrades.
type WebSocketUpgrader interface {
	Upgrade(w http.ResponseWriter, r *http.Request)
}

// EnableConnectionPooling configures connection pooling.
func (ht *HttpTransport) EnableConnectionPooling(maxIdle, maxPerHost int) {
	transport := ht.client.Transport.(*http.Transport)
	transport.MaxIdleConns = maxIdle
	transport.MaxConnsPerHost = maxPerHost
}

// SetRequestMapping enables request/response correlation.
func (ht *HttpTransport) SetRequestMapping(enabled bool) {
	if enabled {
		// Enable request ID generation
		ht.config.Headers["X-Request-ID"] = generateRequestID()
	}
}

// generateRequestID generates unique request ID.
func generateRequestID() string {
	return fmt.Sprintf("%d", time.Now().UnixNano())
}