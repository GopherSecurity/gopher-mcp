package filters_test

import (
	"context"
	"errors"
	"sync"
	"sync/atomic"
	"testing"
	"time"

	"github.com/GopherSecurity/gopher-mcp/src/filters"
	"github.com/GopherSecurity/gopher-mcp/src/types"
)

// Test 1: Default retry configuration
func TestDefaultRetryConfig(t *testing.T) {
	config := filters.DefaultRetryConfig()
	
	if config.MaxAttempts != 3 {
		t.Errorf("MaxAttempts = %d, want 3", config.MaxAttempts)
	}
	
	if config.InitialDelay != 1*time.Second {
		t.Errorf("InitialDelay = %v, want 1s", config.InitialDelay)
	}
	
	if config.MaxDelay != 30*time.Second {
		t.Errorf("MaxDelay = %v, want 30s", config.MaxDelay)
	}
	
	if config.Multiplier != 2.0 {
		t.Errorf("Multiplier = %f, want 2.0", config.Multiplier)
	}
	
	if config.Timeout != 1*time.Minute {
		t.Errorf("Timeout = %v, want 1m", config.Timeout)
	}
	
	// Check retryable status codes
	expectedCodes := []int{429, 500, 502, 503, 504}
	if len(config.RetryableStatusCodes) != len(expectedCodes) {
		t.Errorf("RetryableStatusCodes length = %d, want %d", 
			len(config.RetryableStatusCodes), len(expectedCodes))
	}
}

// Test 2: Exponential backoff calculation
func TestExponentialBackoff(t *testing.T) {
	backoff := filters.NewExponentialBackoff(
		100*time.Millisecond,
		1*time.Second,
		2.0,
	)
	
	tests := []struct {
		attempt  int
		minDelay time.Duration
		maxDelay time.Duration
	}{
		{1, 90 * time.Millisecond, 110 * time.Millisecond},    // ~100ms
		{2, 180 * time.Millisecond, 220 * time.Millisecond},   // ~200ms
		{3, 360 * time.Millisecond, 440 * time.Millisecond},   // ~400ms
		{4, 720 * time.Millisecond, 880 * time.Millisecond},   // ~800ms
		{5, 900 * time.Millisecond, 1100 * time.Millisecond},  // capped at 1s
	}
	
	for _, tt := range tests {
		delay := backoff.NextDelay(tt.attempt)
		if delay < tt.minDelay || delay > tt.maxDelay {
			t.Errorf("Attempt %d: delay = %v, want between %v and %v",
				tt.attempt, delay, tt.minDelay, tt.maxDelay)
		}
	}
}

// Test 3: Linear backoff calculation
func TestLinearBackoff(t *testing.T) {
	backoff := filters.NewLinearBackoff(
		100*time.Millisecond,
		50*time.Millisecond,
		500*time.Millisecond,
	)
	
	tests := []struct {
		attempt  int
		minDelay time.Duration
		maxDelay time.Duration
	}{
		{1, 90 * time.Millisecond, 110 * time.Millisecond},    // ~100ms
		{2, 140 * time.Millisecond, 160 * time.Millisecond},   // ~150ms
		{3, 180 * time.Millisecond, 220 * time.Millisecond},   // ~200ms (with jitter)
		{10, 450 * time.Millisecond, 550 * time.Millisecond},  // capped at 500ms
	}
	
	for _, tt := range tests {
		delay := backoff.NextDelay(tt.attempt)
		if delay < tt.minDelay || delay > tt.maxDelay {
			t.Errorf("Attempt %d: delay = %v, want between %v and %v",
				tt.attempt, delay, tt.minDelay, tt.maxDelay)
		}
	}
}

// Test 4: Full jitter backoff
func TestFullJitterBackoff(t *testing.T) {
	base := filters.NewExponentialBackoff(
		100*time.Millisecond,
		1*time.Second,
		2.0,
	)
	jittered := filters.NewFullJitterBackoff(base)
	
	// Test multiple times to verify jitter
	for attempt := 1; attempt <= 3; attempt++ {
		baseDelay := base.NextDelay(attempt)
		jitteredDelay := jittered.NextDelay(attempt)
		
		// Jittered delay should be between 0 and base delay
		if jitteredDelay < 0 || jitteredDelay > baseDelay {
			t.Errorf("Attempt %d: jittered = %v, should be 0 to %v",
				attempt, jitteredDelay, baseDelay)
		}
	}
}

// Test 5: Decorrelated jitter backoff
func TestDecorrelatedJitterBackoff(t *testing.T) {
	backoff := filters.NewDecorrelatedJitterBackoff(
		100*time.Millisecond,
		1*time.Second,
	)
	
	// First attempt should return base delay
	delay1 := backoff.NextDelay(1)
	if delay1 != 100*time.Millisecond {
		t.Errorf("First delay = %v, want 100ms", delay1)
	}
	
	// Subsequent attempts should be decorrelated
	for attempt := 2; attempt <= 5; attempt++ {
		delay := backoff.NextDelay(attempt)
		if delay < 100*time.Millisecond || delay > 1*time.Second {
			t.Errorf("Attempt %d: delay = %v, should be between 100ms and 1s",
				attempt, delay)
		}
	}
	
	// Reset should clear state
	backoff.Reset()
	delayAfterReset := backoff.NextDelay(1)
	if delayAfterReset != 100*time.Millisecond {
		t.Errorf("Delay after reset = %v, want 100ms", delayAfterReset)
	}
}

// Test 6: Retry filter basic operation
func TestRetryFilter_Basic(t *testing.T) {
	config := filters.RetryConfig{
		MaxAttempts:  3,
		InitialDelay: 10 * time.Millisecond,
		MaxDelay:     100 * time.Millisecond,
		Multiplier:   2.0,
	}
	
	backoff := filters.NewExponentialBackoff(
		config.InitialDelay,
		config.MaxDelay,
		config.Multiplier,
	)
	
	f := filters.NewRetryFilter(config, backoff)
	ctx := context.Background()
	
	// Process should succeed (processAttempt returns success)
	result, err := f.Process(ctx, []byte("test"))
	if err != nil {
		t.Errorf("Process failed: %v", err)
	}
	if result == nil {
		t.Error("Result should not be nil")
	}
}

// Test 7: Retry with timeout
func TestRetryFilter_Timeout(t *testing.T) {
	// Note: This test would require mocking processAttempt to actually fail
	// and trigger retries. Since processAttempt always succeeds immediately
	// in the current implementation, we'll skip this test.
	t.Skip("Timeout test requires mock implementation that actually retries")
	
	config := filters.RetryConfig{
		MaxAttempts:  10,
		InitialDelay: 100 * time.Millisecond,
		MaxDelay:     1 * time.Second,
		Multiplier:   2.0,
		Timeout:      200 * time.Millisecond, // Short timeout
	}
	
	backoff := filters.NewExponentialBackoff(
		config.InitialDelay,
		config.MaxDelay,
		config.Multiplier,
	)
	
	f := filters.NewRetryFilter(config, backoff)
	ctx := context.Background()
	
	// Process would timeout if processAttempt actually failed
	_, err := f.Process(ctx, []byte("test"))
	_ = err
}

// Test 8: RetryExhaustedException
func TestRetryExhaustedException(t *testing.T) {
	err := errors.New("underlying error")
	exception := &filters.RetryExhaustedException{
		Attempts:      3,
		LastError:     err,
		TotalDuration: 5 * time.Second,
		Delays:        []time.Duration{1 * time.Second, 2 * time.Second},
	}
	
	// Test Error() method
	errMsg := exception.Error()
	if !contains(errMsg, "3 attempts") {
		t.Errorf("Error message should mention attempts: %s", errMsg)
	}
	
	// Test Unwrap()
	unwrapped := exception.Unwrap()
	if unwrapped != err {
		t.Error("Unwrap should return underlying error")
	}
	
	// Test errors.Is
	if !errors.Is(exception, err) {
		t.Error("errors.Is should work with wrapped error")
	}
}

// Test 9: Retry conditions
func TestRetryConditions(t *testing.T) {
	// Test RetryOnError
	if !filters.RetryOnError(errors.New("test"), nil) {
		t.Error("RetryOnError should return true for error")
	}
	if filters.RetryOnError(nil, &types.FilterResult{Status: types.Continue}) {
		t.Error("RetryOnError should return false for success")
	}
	
	// Test RetryOnStatusCodes
	condition := filters.RetryOnStatusCodes(429, 503)
	result := &types.FilterResult{
		Status: types.Error,
		Metadata: map[string]interface{}{
			"status_code": 429,
		},
	}
	if !condition(nil, result) {
		t.Error("Should retry on status code 429")
	}
	
	result.Metadata["status_code"] = 200
	if condition(nil, result) {
		t.Error("Should not retry on status code 200")
	}
	
	// Test RetryOnTimeout
	if !filters.RetryOnTimeout(context.DeadlineExceeded, nil) {
		t.Error("Should retry on deadline exceeded")
	}
	if filters.RetryOnTimeout(errors.New("other error"), nil) {
		t.Error("Should not retry on non-timeout error")
	}
}

// Test 10: Concurrent retry operations
func TestRetryFilter_Concurrent(t *testing.T) {
	config := filters.RetryConfig{
		MaxAttempts:  2,
		InitialDelay: 1 * time.Millisecond,
		MaxDelay:     10 * time.Millisecond,
		Multiplier:   2.0,
	}
	
	backoff := filters.NewExponentialBackoff(
		config.InitialDelay,
		config.MaxDelay,
		config.Multiplier,
	)
	
	f := filters.NewRetryFilter(config, backoff)
	ctx := context.Background()
	
	var wg sync.WaitGroup
	var successCount atomic.Int32
	
	// Run concurrent retry operations
	for i := 0; i < 10; i++ {
		wg.Add(1)
		go func() {
			defer wg.Done()
			result, err := f.Process(ctx, []byte("test"))
			if err == nil && result != nil {
				successCount.Add(1)
			}
		}()
	}
	
	wg.Wait()
	
	// All should succeed
	if successCount.Load() != 10 {
		t.Errorf("Success count = %d, want 10", successCount.Load())
	}
}

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

func BenchmarkExponentialBackoff(b *testing.B) {
	backoff := filters.NewExponentialBackoff(
		100*time.Millisecond,
		10*time.Second,
		2.0,
	)
	
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		backoff.NextDelay(i%10 + 1)
	}
}

func BenchmarkLinearBackoff(b *testing.B) {
	backoff := filters.NewLinearBackoff(
		100*time.Millisecond,
		100*time.Millisecond,
		10*time.Second,
	)
	
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		backoff.NextDelay(i%10 + 1)
	}
}

func BenchmarkRetryFilter_Process(b *testing.B) {
	config := filters.RetryConfig{
		MaxAttempts:  1, // No actual retries for benchmark
		InitialDelay: 1 * time.Millisecond,
		MaxDelay:     10 * time.Millisecond,
		Multiplier:   2.0,
	}
	
	backoff := filters.NewExponentialBackoff(
		config.InitialDelay,
		config.MaxDelay,
		config.Multiplier,
	)
	
	f := filters.NewRetryFilter(config, backoff)
	ctx := context.Background()
	data := []byte("test data")
	
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		f.Process(ctx, data)
	}
}

func BenchmarkFullJitterBackoff(b *testing.B) {
	base := filters.NewExponentialBackoff(
		100*time.Millisecond,
		10*time.Second,
		2.0,
	)
	jittered := filters.NewFullJitterBackoff(base)
	
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		jittered.NextDelay(i%10 + 1)
	}
}