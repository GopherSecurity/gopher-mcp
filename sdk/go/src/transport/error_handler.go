// Package transport provides communication transports for the MCP Filter SDK.
package transport

import (
	"context"
	"errors"
	"fmt"
	"io"
	"net"
	"os"
	"sync"
	"sync/atomic"
	"syscall"
	"time"
)

// errorWrapper wraps an error to ensure consistent type for atomic.Value
type errorWrapper struct {
	err error
}

// timeWrapper wraps a time.Time to ensure consistent type for atomic.Value
type timeWrapper struct {
	t time.Time
}

// ErrorHandler manages error handling and recovery for transport operations.
type ErrorHandler struct {
	// Configuration
	config ErrorHandlerConfig
	
	// Error tracking
	errorCount     atomic.Int64
	lastError      atomic.Value // stores *errorWrapper
	errorHistory   []ErrorRecord
	
	// Reconnection state
	reconnecting   atomic.Bool
	reconnectCount atomic.Int64
	lastReconnect  atomic.Value // stores *timeWrapper
	
	// Callbacks
	onError      func(error)
	onReconnect  func()
	onFatalError func(error)
	
	mu sync.RWMutex
}

// ErrorHandlerConfig configures error handling behavior.
type ErrorHandlerConfig struct {
	MaxReconnectAttempts int
	ReconnectDelay       time.Duration
	ReconnectBackoff     float64
	MaxReconnectDelay    time.Duration
	ErrorHistorySize     int
	EnableAutoReconnect  bool
}

// DefaultErrorHandlerConfig returns default configuration.
func DefaultErrorHandlerConfig() ErrorHandlerConfig {
	return ErrorHandlerConfig{
		MaxReconnectAttempts: 5,
		ReconnectDelay:       time.Second,
		ReconnectBackoff:     2.0,
		MaxReconnectDelay:    30 * time.Second,
		ErrorHistorySize:     100,
		EnableAutoReconnect:  true,
	}
}

// ErrorRecord tracks error occurrences.
type ErrorRecord struct {
	Error     error
	Timestamp time.Time
	Category  ErrorCategory
	Retryable bool
}

// ErrorCategory classifies error types.
type ErrorCategory int

const (
	NetworkError ErrorCategory = iota
	IOError
	ProtocolError
	TimeoutError
	SignalError
	FatalError
)

// NewErrorHandler creates a new error handler.
func NewErrorHandler(config ErrorHandlerConfig) *ErrorHandler {
	eh := &ErrorHandler{
		config:       config,
		errorHistory: make([]ErrorRecord, 0, config.ErrorHistorySize),
	}
	// Initialize atomic values with proper types
	eh.lastError.Store(&errorWrapper{err: nil})
	eh.lastReconnect.Store(&timeWrapper{t: time.Time{}})
	return eh
}

// HandleError processes and categorizes errors.
func (eh *ErrorHandler) HandleError(err error) error {
	if err == nil {
		return nil
	}
	
	eh.errorCount.Add(1)
	eh.lastError.Store(&errorWrapper{err: err})
	
	// Categorize error
	category := eh.categorizeError(err)
	retryable := eh.isRetryable(err)
	
	// Record error
	eh.recordError(ErrorRecord{
		Error:     err,
		Timestamp: time.Now(),
		Category:  category,
		Retryable: retryable,
	})
	
	// Create meaningful error message
	enhancedErr := eh.enhanceError(err, category)
	
	// Trigger callback
	if eh.onError != nil {
		eh.onError(enhancedErr)
	}
	
	// Check if fatal
	if category == FatalError {
		if eh.onFatalError != nil {
			eh.onFatalError(enhancedErr)
		}
		return enhancedErr
	}
	
	// Attempt recovery if retryable
	if retryable && eh.config.EnableAutoReconnect {
		go eh.attemptReconnection()
	}
	
	return enhancedErr
}

// categorizeError determines the error category.
func (eh *ErrorHandler) categorizeError(err error) ErrorCategory {
	// Check for EOF
	if errors.Is(err, io.EOF) || errors.Is(err, io.ErrUnexpectedEOF) {
		return IOError
	}
	
	// Check for closed pipe
	if errors.Is(err, io.ErrClosedPipe) || errors.Is(err, syscall.EPIPE) {
		return IOError
	}
	
	// Check for signal interrupts first (before network errors)
	if errors.Is(err, syscall.EINTR) {
		return SignalError
	}
	
	// Check for network errors
	var netErr net.Error
	if errors.As(err, &netErr) {
		if netErr.Timeout() {
			return TimeoutError
		}
		return NetworkError
	}
	
	// Check for connection refused
	if errors.Is(err, syscall.ECONNREFUSED) {
		return NetworkError
	}
	
	// Check for connection reset
	if errors.Is(err, syscall.ECONNRESET) {
		return NetworkError
	}
	
	// Check for broken pipe
	if errors.Is(err, syscall.EPIPE) {
		return IOError
	}
	
	// Check for protocol errors
	if isProtocolError(err) {
		return ProtocolError
	}
	
	// Default to IO error
	return IOError
}

// isRetryable determines if an error is retryable.
func (eh *ErrorHandler) isRetryable(err error) bool {
	// EOF is not retryable
	if errors.Is(err, io.EOF) {
		return false
	}
	
	// Protocol errors are not retryable
	if isProtocolError(err) {
		return false
	}
	
	// Signal interrupts are retryable
	if errors.Is(err, syscall.EINTR) {
		return true
	}
	
	// Connection errors are retryable (check before net.Error)
	if errors.Is(err, syscall.ECONNREFUSED) || 
	   errors.Is(err, syscall.ECONNRESET) ||
	   errors.Is(err, io.ErrClosedPipe) {
		return true
	}
	
	// Network errors are generally retryable
	var netErr net.Error
	if errors.As(err, &netErr) {
		return netErr.Temporary() || netErr.Timeout()
	}
	
	return false
}

// enhanceError creates a meaningful error message.
func (eh *ErrorHandler) enhanceError(err error, category ErrorCategory) error {
	var prefix string
	
	switch category {
	case NetworkError:
		prefix = "network error"
	case IOError:
		prefix = "I/O error"
	case ProtocolError:
		prefix = "protocol error"
	case TimeoutError:
		prefix = "timeout error"
	case SignalError:
		prefix = "signal interrupt"
	case FatalError:
		prefix = "fatal error"
	default:
		prefix = "transport error"
	}
	
	// Add context about error state
	errorCount := eh.errorCount.Load()
	reconnectCount := eh.reconnectCount.Load()
	
	msg := fmt.Sprintf("%s: %v (errors: %d, reconnects: %d)",
		prefix, err, errorCount, reconnectCount)
	
	// Add recovery suggestion
	if eh.isRetryable(err) {
		msg += " - will attempt reconnection"
	} else {
		msg += " - not retryable"
	}
	
	return &TransportError{
		Code:    fmt.Sprintf("TRANSPORT_%s", category.String()),
		Message: msg,
		Cause:   err,
	}
}

// attemptReconnection tries to recover from connection errors.
func (eh *ErrorHandler) attemptReconnection() {
	// Check if already reconnecting
	if !eh.reconnecting.CompareAndSwap(false, true) {
		return
	}
	defer eh.reconnecting.Store(false)
	
	delay := eh.config.ReconnectDelay
	
	for attempt := 1; attempt <= eh.config.MaxReconnectAttempts; attempt++ {
		eh.reconnectCount.Add(1)
		eh.lastReconnect.Store(&timeWrapper{t: time.Now()})
		
		// Trigger reconnect callback
		if eh.onReconnect != nil {
			eh.onReconnect()
		}
		
		// Wait before next attempt
		time.Sleep(delay)
		
		// Increase delay with backoff
		delay = time.Duration(float64(delay) * eh.config.ReconnectBackoff)
		if delay > eh.config.MaxReconnectDelay {
			delay = eh.config.MaxReconnectDelay
		}
	}
}

// recordError adds error to history.
func (eh *ErrorHandler) recordError(record ErrorRecord) {
	eh.mu.Lock()
	defer eh.mu.Unlock()
	
	eh.errorHistory = append(eh.errorHistory, record)
	
	// Trim history if needed
	if len(eh.errorHistory) > eh.config.ErrorHistorySize {
		eh.errorHistory = eh.errorHistory[len(eh.errorHistory)-eh.config.ErrorHistorySize:]
	}
}

// HandleEOF handles EOF errors specifically.
func (eh *ErrorHandler) HandleEOF() error {
	return eh.HandleError(io.EOF)
}

// HandleClosedPipe handles closed pipe errors.
func (eh *ErrorHandler) HandleClosedPipe() error {
	return eh.HandleError(io.ErrClosedPipe)
}

// HandleSignalInterrupt handles signal interrupts.
func (eh *ErrorHandler) HandleSignalInterrupt(sig os.Signal) error {
	err := fmt.Errorf("interrupted by signal: %v", sig)
	return eh.HandleError(err)
}

// SetErrorCallback sets the error callback.
func (eh *ErrorHandler) SetErrorCallback(cb func(error)) {
	eh.onError = cb
}

// SetReconnectCallback sets the reconnection callback.
func (eh *ErrorHandler) SetReconnectCallback(cb func()) {
	eh.onReconnect = cb
}

// SetFatalErrorCallback sets the fatal error callback.
func (eh *ErrorHandler) SetFatalErrorCallback(cb func(error)) {
	eh.onFatalError = cb
}

// GetErrorHistory returns recent errors.
func (eh *ErrorHandler) GetErrorHistory() []ErrorRecord {
	eh.mu.RLock()
	defer eh.mu.RUnlock()
	
	result := make([]ErrorRecord, len(eh.errorHistory))
	copy(result, eh.errorHistory)
	return result
}

// GetLastError returns the most recent error.
func (eh *ErrorHandler) GetLastError() error {
	if v := eh.lastError.Load(); v != nil {
		if wrapper, ok := v.(*errorWrapper); ok {
			return wrapper.err
		}
	}
	return nil
}

// IsRecoverable checks if system can recover from current error state.
func (eh *ErrorHandler) IsRecoverable() bool {
	lastErr := eh.GetLastError()
	if lastErr == nil {
		return true
	}
	
	return eh.isRetryable(lastErr)
}

// Reset clears error state.
func (eh *ErrorHandler) Reset() {
	eh.mu.Lock()
	defer eh.mu.Unlock()
	
	eh.errorCount.Store(0)
	eh.reconnectCount.Store(0)
	eh.lastError.Store(&errorWrapper{err: nil})
	eh.errorHistory = eh.errorHistory[:0]
	eh.reconnecting.Store(false)
}

// String returns string representation of error category.
func (c ErrorCategory) String() string {
	switch c {
	case NetworkError:
		return "NETWORK"
	case IOError:
		return "IO"
	case ProtocolError:
		return "PROTOCOL"
	case TimeoutError:
		return "TIMEOUT"
	case SignalError:
		return "SIGNAL"
	case FatalError:
		return "FATAL"
	default:
		return "UNKNOWN"
	}
}

// isProtocolError checks if error is protocol-related.
func isProtocolError(err error) bool {
	// Check for common protocol error patterns
	errStr := err.Error()
	return contains(errStr, "protocol") ||
		contains(errStr, "invalid message") ||
		contains(errStr, "unexpected format") ||
		contains(errStr, "malformed")
}

// contains checks if string contains substring.
func contains(s, substr string) bool {
	return len(s) >= len(substr) && s[:len(substr)] == substr ||
		len(s) > len(substr) && containsHelper(s[1:], substr)
}

// containsHelper is a helper for contains.
func containsHelper(s, substr string) bool {
	if len(s) < len(substr) {
		return false
	}
	if s[:len(substr)] == substr {
		return true
	}
	return containsHelper(s[1:], substr)
}

// ReconnectionLogic provides reconnection strategy.
type ReconnectionLogic struct {
	handler  *ErrorHandler
	transport Transport
	ctx      context.Context
	cancel   context.CancelFunc
}

// NewReconnectionLogic creates reconnection logic for a transport.
func NewReconnectionLogic(handler *ErrorHandler, transport Transport) *ReconnectionLogic {
	ctx, cancel := context.WithCancel(context.Background())
	return &ReconnectionLogic{
		handler:   handler,
		transport: transport,
		ctx:       ctx,
		cancel:    cancel,
	}
}

// Start begins monitoring for reconnection.
func (rl *ReconnectionLogic) Start() {
	rl.handler.SetReconnectCallback(func() {
		// Attempt to reconnect transport
		if err := rl.transport.Connect(rl.ctx); err != nil {
			rl.handler.HandleError(err)
		}
	})
}

// Stop stops reconnection monitoring.
func (rl *ReconnectionLogic) Stop() {
	rl.cancel()
}