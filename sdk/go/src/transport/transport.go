// Package transport provides communication transports for the MCP Filter SDK.
// It defines the Transport interface and various implementations for different
// communication protocols and mediums.
package transport

import (
	"context"
	"time"
)

// Transport defines the interface for communication transports.
// All transport implementations must provide connection lifecycle management
// and bidirectional data transfer capabilities.
//
// Transports should be:
//   - Thread-safe for concurrent use
//   - Support graceful shutdown
//   - Handle connection failures appropriately
//   - Provide meaningful error messages
//   - Support context-based cancellation
//
// Example usage:
//
//	transport := NewStdioTransport(config)
//	
//	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
//	defer cancel()
//	
//	if err := transport.Connect(ctx); err != nil {
//	    log.Fatal("Failed to connect:", err)
//	}
//	defer transport.Disconnect()
//	
//	// Send data
//	if err := transport.Send([]byte("Hello")); err != nil {
//	    log.Printf("Send failed: %v", err)
//	}
//	
//	// Receive data
//	data, err := transport.Receive()
//	if err != nil {
//	    if err == io.EOF {
//	        log.Println("Connection closed")
//	    } else {
//	        log.Printf("Receive failed: %v", err)
//	    }
//	}
type Transport interface {
	// Connect establishes a connection using the provided context.
	// The context can be used to set timeouts or cancel the connection attempt.
	//
	// Parameters:
	//   - ctx: Context for cancellation and timeout control
	//
	// Returns:
	//   - error: Connection error, or nil on success
	//
	// Errors:
	//   - context.DeadlineExceeded: Connection timeout
	//   - context.Canceled: Connection cancelled
	//   - ErrAlreadyConnected: Already connected
	//   - Transport-specific connection errors
	Connect(ctx context.Context) error

	// Disconnect gracefully closes the connection.
	// This method should:
	//   - Flush any pending data
	//   - Clean up resources
	//   - Be safe to call multiple times (idempotent)
	//
	// Returns:
	//   - error: Disconnection error, or nil on success
	Disconnect() error

	// Send transmits data through the transport.
	// The method should handle:
	//   - Partial writes by retrying
	//   - Buffering if configured
	//   - Message framing as required by the transport
	//
	// Parameters:
	//   - data: The data to send
	//
	// Returns:
	//   - error: Send error, or nil on success
	//
	// Errors:
	//   - ErrNotConnected: Transport is not connected
	//   - io.ErrShortWrite: Partial write occurred
	//   - Transport-specific send errors
	Send(data []byte) error

	// Receive reads data from the transport.
	// The method should handle:
	//   - Message framing/delimiting
	//   - Buffering for efficiency
	//   - Partial reads by accumulating data
	//
	// Returns:
	//   - []byte: Received data
	//   - error: Receive error, or nil on success
	//
	// Errors:
	//   - io.EOF: Connection closed gracefully
	//   - ErrNotConnected: Transport is not connected
	//   - Transport-specific receive errors
	Receive() ([]byte, error)

	// IsConnected returns the current connection state.
	// This method must be thread-safe and reflect the actual
	// connection status, not just a flag.
	//
	// Returns:
	//   - bool: true if connected, false otherwise
	IsConnected() bool

	// GetStats returns transport statistics for monitoring.
	// Statistics should include bytes sent/received, message counts,
	// error counts, and connection duration.
	//
	// Returns:
	//   - TransportStatistics: Current transport statistics
	GetStats() TransportStatistics

	// Close closes the transport and releases all resources.
	// This is typically called when the transport is no longer needed.
	// After Close, the transport should not be reused.
	//
	// Returns:
	//   - error: Close error, or nil on success
	Close() error
}

// TransportStatistics contains transport performance metrics.
type TransportStatistics struct {
	// Connection info
	ConnectedAt      time.Time
	DisconnectedAt   time.Time
	ConnectionCount  int64
	IsConnected      bool
	
	// Data transfer metrics
	BytesSent        int64
	BytesReceived    int64
	MessagesSent     int64
	MessagesReceived int64
	
	// Error tracking
	SendErrors       int64
	ReceiveErrors    int64
	ConnectionErrors int64
	
	// Performance metrics
	LastSendTime     time.Time
	LastReceiveTime  time.Time
	AverageLatency   time.Duration
	
	// Transport-specific metrics
	CustomMetrics    map[string]interface{}
}

// TransportConfig provides common configuration for all transports.
type TransportConfig struct {
	// Connection settings
	ConnectTimeout   time.Duration
	ReadTimeout      time.Duration
	WriteTimeout     time.Duration
	
	// Buffer settings
	ReadBufferSize   int
	WriteBufferSize  int
	
	// Retry settings
	MaxRetries       int
	RetryDelay       time.Duration
	
	// Keep-alive settings
	KeepAlive        bool
	KeepAliveInterval time.Duration
	
	// Logging
	Debug            bool
	
	// Transport-specific settings
	CustomConfig     map[string]interface{}
}

// DefaultTransportConfig returns a sensible default configuration.
func DefaultTransportConfig() TransportConfig {
	return TransportConfig{
		ConnectTimeout:   30 * time.Second,
		ReadTimeout:      30 * time.Second,
		WriteTimeout:     30 * time.Second,
		ReadBufferSize:   4096,
		WriteBufferSize:  4096,
		MaxRetries:       3,
		RetryDelay:       1 * time.Second,
		KeepAlive:        true,
		KeepAliveInterval: 30 * time.Second,
		Debug:            false,
		CustomConfig:     make(map[string]interface{}),
	}
}

// Common transport errors
var (
	// ErrNotConnected is returned when attempting operations on a disconnected transport
	ErrNotConnected = &TransportError{Code: "NOT_CONNECTED", Message: "transport is not connected"}
	
	// ErrAlreadyConnected is returned when attempting to connect an already connected transport
	ErrAlreadyConnected = &TransportError{Code: "ALREADY_CONNECTED", Message: "transport is already connected"}
	
	// ErrConnectionFailed is returned when connection establishment fails
	ErrConnectionFailed = &TransportError{Code: "CONNECTION_FAILED", Message: "failed to establish connection"}
	
	// ErrSendFailed is returned when sending data fails
	ErrSendFailed = &TransportError{Code: "SEND_FAILED", Message: "failed to send data"}
	
	// ErrReceiveFailed is returned when receiving data fails
	ErrReceiveFailed = &TransportError{Code: "RECEIVE_FAILED", Message: "failed to receive data"}
)

// TransportError represents a transport-specific error.
type TransportError struct {
	Code    string
	Message string
	Cause   error
}

// Error implements the error interface.
func (e *TransportError) Error() string {
	if e.Cause != nil {
		return e.Code + ": " + e.Message + ": " + e.Cause.Error()
	}
	return e.Code + ": " + e.Message
}

// Unwrap returns the underlying error for errors.Is/As support.
func (e *TransportError) Unwrap() error {
	return e.Cause
}