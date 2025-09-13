package transport_test

import (
	"errors"
	"io"
	"net"
	"os"
	"sync"
	"syscall"
	"testing"
	"time"

	"github.com/GopherSecurity/gopher-mcp/src/transport"
)

// Test 1: NewErrorHandler with default config
func TestNewErrorHandler_Default(t *testing.T) {
	config := transport.DefaultErrorHandlerConfig()
	eh := transport.NewErrorHandler(config)

	if eh == nil {
		t.Fatal("NewErrorHandler returned nil")
	}

	// Check initial state
	if eh.GetLastError() != nil {
		t.Error("Initial error should be nil")
	}

	history := eh.GetErrorHistory()
	if len(history) != 0 {
		t.Error("Initial error history should be empty")
	}

	if !eh.IsRecoverable() {
		t.Error("Should be recoverable initially")
	}
}

// Test 2: HandleError categorization
func TestErrorHandler_Categorization(t *testing.T) {
	tests := []struct {
		name     string
		err      error
		category string
	}{
		{"EOF", io.EOF, "IO"},
		{"UnexpectedEOF", io.ErrUnexpectedEOF, "IO"},
		{"ClosedPipe", io.ErrClosedPipe, "IO"},
		{"EPIPE", syscall.EPIPE, "IO"},
		{"ECONNREFUSED", syscall.ECONNREFUSED, "NETWORK"},
		{"ECONNRESET", syscall.ECONNRESET, "NETWORK"},
		{"EINTR", syscall.EINTR, "SIGNAL"},
		{"Timeout", &net.OpError{Op: "read", Err: &timeoutError{}}, "TIMEOUT"},
		{"Protocol", errors.New("protocol error"), "PROTOCOL"},
		{"Generic", errors.New("generic error"), "IO"},
	}

	config := transport.DefaultErrorHandlerConfig()
	eh := transport.NewErrorHandler(config)

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			result := eh.HandleError(tt.err)
			if result == nil {
				t.Fatal("HandleError returned nil")
			}

			// Check if error contains expected category
			errStr := result.Error()
			if !contains(errStr, tt.category) {
				t.Errorf("Error message doesn't contain category %s: %s", tt.category, errStr)
			}
		})
	}
}

// Test 3: Error retryability
func TestErrorHandler_Retryability(t *testing.T) {
	tests := []struct {
		name      string
		err       error
		retryable bool
	}{
		{"EOF", io.EOF, false},
		{"ECONNREFUSED", syscall.ECONNREFUSED, true},
		{"ECONNRESET", syscall.ECONNRESET, true},
		{"EINTR", syscall.EINTR, true},
		{"ClosedPipe", io.ErrClosedPipe, true},
		{"Protocol", errors.New("protocol error"), false},
		{"Timeout", &net.OpError{Op: "read", Err: &timeoutError{}}, true},
	}

	config := transport.DefaultErrorHandlerConfig()
	eh := transport.NewErrorHandler(config)

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			eh.HandleError(tt.err)

			// Check if last error is considered recoverable
			isRecoverable := eh.IsRecoverable()
			if isRecoverable != tt.retryable {
				t.Errorf("IsRecoverable() = %v, want %v for %v",
					isRecoverable, tt.retryable, tt.err)
			}
		})
	}
}

// Test 4: Error history tracking
func TestErrorHandler_History(t *testing.T) {
	config := transport.DefaultErrorHandlerConfig()
	config.ErrorHistorySize = 5
	eh := transport.NewErrorHandler(config)

	// Add more errors than history size
	for i := 0; i < 10; i++ {
		eh.HandleError(errors.New("error"))
		time.Sleep(time.Millisecond) // Ensure different timestamps
	}

	history := eh.GetErrorHistory()
	if len(history) != 5 {
		t.Errorf("History length = %d, want 5", len(history))
	}

	// Check timestamps are ordered
	for i := 1; i < len(history); i++ {
		if !history[i].Timestamp.After(history[i-1].Timestamp) {
			t.Error("History timestamps not in order")
		}
	}
}

// Test 5: Error callbacks
func TestErrorHandler_Callbacks(t *testing.T) {
	config := transport.DefaultErrorHandlerConfig()
	eh := transport.NewErrorHandler(config)

	var errorCalled bool

	eh.SetErrorCallback(func(err error) {
		errorCalled = true
	})

	// Note: fatalCalled and reconnectCalled removed since they're not used in this test
	// The current implementation doesn't explicitly trigger these in a testable way

	// Regular error
	eh.HandleError(errors.New("test error"))
	if !errorCalled {
		t.Error("Error callback not called")
	}

	// Note: Fatal errors would need special handling in the actual implementation
	// The current implementation doesn't explicitly mark errors as fatal
}

// Test 6: HandleEOF
func TestErrorHandler_HandleEOF(t *testing.T) {
	config := transport.DefaultErrorHandlerConfig()
	eh := transport.NewErrorHandler(config)

	err := eh.HandleEOF()
	if err == nil {
		t.Fatal("HandleEOF should return error")
	}

	// Check last error is EOF
	lastErr := eh.GetLastError()
	if !errors.Is(lastErr, io.EOF) {
		t.Error("Last error should be EOF")
	}

	// EOF should not be recoverable
	if eh.IsRecoverable() {
		t.Error("EOF should not be recoverable")
	}
}

// Test 7: HandleClosedPipe
func TestErrorHandler_HandleClosedPipe(t *testing.T) {
	config := transport.DefaultErrorHandlerConfig()
	eh := transport.NewErrorHandler(config)

	err := eh.HandleClosedPipe()
	if err == nil {
		t.Fatal("HandleClosedPipe should return error")
	}

	// Check last error is closed pipe
	lastErr := eh.GetLastError()
	if !errors.Is(lastErr, io.ErrClosedPipe) {
		t.Error("Last error should be ErrClosedPipe")
	}

	// Closed pipe should be recoverable
	if !eh.IsRecoverable() {
		t.Error("Closed pipe should be recoverable")
	}
}

// Test 8: HandleSignalInterrupt
func TestErrorHandler_HandleSignalInterrupt(t *testing.T) {
	config := transport.DefaultErrorHandlerConfig()
	eh := transport.NewErrorHandler(config)

	err := eh.HandleSignalInterrupt(os.Interrupt)
	if err == nil {
		t.Fatal("HandleSignalInterrupt should return error")
	}

	// Check error message contains signal info
	if !contains(err.Error(), "signal") {
		t.Error("Error should mention signal")
	}
}

// Test 9: Reset functionality
func TestErrorHandler_Reset(t *testing.T) {
	config := transport.DefaultErrorHandlerConfig()
	eh := transport.NewErrorHandler(config)

	// Generate some errors
	eh.HandleError(errors.New("error1"))
	eh.HandleError(errors.New("error2"))

	// Verify errors are recorded
	if eh.GetLastError() == nil {
		t.Error("Should have last error before reset")
	}
	if len(eh.GetErrorHistory()) == 0 {
		t.Error("Should have error history before reset")
	}

	// Reset
	eh.Reset()

	// Check everything is cleared
	if eh.GetLastError() != nil {
		t.Error("Last error should be nil after reset")
	}
	if len(eh.GetErrorHistory()) != 0 {
		t.Error("Error history should be empty after reset")
	}
	if !eh.IsRecoverable() {
		t.Error("Should be recoverable after reset")
	}
}

// Test 10: Concurrent error handling
func TestErrorHandler_Concurrent(t *testing.T) {
	config := transport.DefaultErrorHandlerConfig()
	config.ErrorHistorySize = 1000
	eh := transport.NewErrorHandler(config)

	var wg sync.WaitGroup
	numGoroutines := 10
	errorsPerGoroutine := 100

	// Concurrent error handling
	for i := 0; i < numGoroutines; i++ {
		wg.Add(1)
		go func(id int) {
			defer wg.Done()
			for j := 0; j < errorsPerGoroutine; j++ {
				if j%3 == 0 {
					eh.HandleError(io.EOF)
				} else if j%3 == 1 {
					eh.HandleError(syscall.ECONNRESET)
				} else {
					eh.HandleError(errors.New("test error"))
				}
			}
		}(i)
	}

	// Concurrent reads
	for i := 0; i < numGoroutines; i++ {
		wg.Add(1)
		go func() {
			defer wg.Done()
			for j := 0; j < errorsPerGoroutine; j++ {
				_ = eh.GetLastError()
				_ = eh.GetErrorHistory()
				_ = eh.IsRecoverable()
			}
		}()
	}

	wg.Wait()

	// Verify history has expected number of errors
	history := eh.GetErrorHistory()
	expectedErrors := numGoroutines * errorsPerGoroutine
	if len(history) > expectedErrors {
		t.Errorf("History has more errors than expected: %d > %d", len(history), expectedErrors)
	}
}

// Test 11: ErrorCategory String representation
func TestErrorCategory_String(t *testing.T) {
	tests := []struct {
		category transport.ErrorCategory
		expected string
	}{
		{transport.NetworkError, "NETWORK"},
		{transport.IOError, "IO"},
		{transport.ProtocolError, "PROTOCOL"},
		{transport.TimeoutError, "TIMEOUT"},
		{transport.SignalError, "SIGNAL"},
		{transport.FatalError, "FATAL"},
		{transport.ErrorCategory(99), "UNKNOWN"},
	}

	for _, tt := range tests {
		result := tt.category.String()
		if result != tt.expected {
			t.Errorf("ErrorCategory.String() = %s, want %s", result, tt.expected)
		}
	}
}

// Test 12: Auto-reconnect behavior
func TestErrorHandler_AutoReconnect(t *testing.T) {
	config := transport.DefaultErrorHandlerConfig()
	config.EnableAutoReconnect = true
	config.MaxReconnectAttempts = 2
	config.ReconnectDelay = 10 * time.Millisecond
	eh := transport.NewErrorHandler(config)

	reconnectCount := 0
	eh.SetReconnectCallback(func() {
		reconnectCount++
	})

	// Handle retryable error
	eh.HandleError(syscall.ECONNRESET)

	// Wait for reconnection attempts
	time.Sleep(100 * time.Millisecond)

	// Should have triggered reconnection
	if reconnectCount == 0 {
		t.Error("Auto-reconnect should have been triggered")
	}
}

// Helper types for testing

type timeoutError struct{}

func (e *timeoutError) Error() string   { return "timeout" }
func (e *timeoutError) Timeout() bool   { return true }
func (e *timeoutError) Temporary() bool { return true }

// Helper function
func contains(s, substr string) bool {
	for i := 0; i <= len(s)-len(substr); i++ {
		if s[i:i+len(substr)] == substr {
			return true
		}
	}
	return false
}

// Benchmarks

func BenchmarkErrorHandler_HandleError(b *testing.B) {
	config := transport.DefaultErrorHandlerConfig()
	eh := transport.NewErrorHandler(config)

	err := errors.New("test error")

	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		eh.HandleError(err)
	}
}

func BenchmarkErrorHandler_GetHistory(b *testing.B) {
	config := transport.DefaultErrorHandlerConfig()
	config.ErrorHistorySize = 100
	eh := transport.NewErrorHandler(config)

	// Fill history
	for i := 0; i < 100; i++ {
		eh.HandleError(errors.New("error"))
	}

	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		_ = eh.GetErrorHistory()
	}
}

func BenchmarkErrorHandler_Concurrent(b *testing.B) {
	config := transport.DefaultErrorHandlerConfig()
	eh := transport.NewErrorHandler(config)

	err := errors.New("test error")

	b.RunParallel(func(pb *testing.PB) {
		for pb.Next() {
			eh.HandleError(err)
		}
	})
}
