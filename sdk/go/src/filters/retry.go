// Package filters provides built-in filters for the MCP Filter SDK.
package filters

import (
	"context"
	"errors"
	"fmt"
	"math"
	"math/rand"
	"sync"
	"sync/atomic"
	"time"

	"github.com/GopherSecurity/gopher-mcp/src/types"
)

// BackoffStrategy defines the interface for retry delay calculation.
type BackoffStrategy interface {
	NextDelay(attempt int) time.Duration
	Reset()
}

// RetryExhaustedException is returned when all retry attempts fail.
type RetryExhaustedException struct {
	// Attempts is the number of retry attempts made
	Attempts int

	// LastError is the final error encountered
	LastError error

	// TotalDuration is the total time spent retrying
	TotalDuration time.Duration

	// Delays contains all backoff delays used
	Delays []time.Duration

	// Errors contains all errors encountered (if tracking enabled)
	Errors []error
}

// Error implements the error interface.
func (e *RetryExhaustedException) Error() string {
	return fmt.Sprintf("retry exhausted after %d attempts (took %v): %v",
		e.Attempts, e.TotalDuration, e.LastError)
}

// Unwrap returns the underlying error for errors.Is/As support.
func (e *RetryExhaustedException) Unwrap() error {
	return e.LastError
}

// RetryStatistics tracks retry filter performance metrics.
type RetryStatistics struct {
	TotalAttempts     uint64
	SuccessfulRetries uint64
	FailedRetries     uint64
	RetryReasons      map[string]uint64
	BackoffDelays     []time.Duration
	AverageDelay      time.Duration
	MaxDelay          time.Duration
	RetrySuccessRate  float64
}

// RetryCondition is a custom function to determine if retry should occur.
type RetryCondition func(error, *types.FilterResult) bool

// RetryConfig configures the retry behavior.
type RetryConfig struct {
	// MaxAttempts is the maximum number of retry attempts.
	// Set to 0 for infinite retries (use with Timeout).
	MaxAttempts int

	// InitialDelay is the delay before the first retry.
	InitialDelay time.Duration

	// MaxDelay is the maximum delay between retries.
	MaxDelay time.Duration

	// Multiplier for exponential backoff (e.g., 2.0 for doubling).
	Multiplier float64

	// RetryableErrors is a list of errors that trigger retry.
	// If empty, all errors are retryable.
	RetryableErrors []error

	// RetryableStatusCodes is a list of HTTP-like status codes that trigger retry.
	RetryableStatusCodes []int

	// Timeout is the maximum total time for all retry attempts.
	// If exceeded, retries stop regardless of MaxAttempts.
	Timeout time.Duration

	// RetryCondition is a custom function to determine retry eligibility.
	// If set, it overrides default retry logic.
	RetryCondition RetryCondition
}

// DefaultRetryConfig returns a sensible default configuration.
func DefaultRetryConfig() RetryConfig {
	return RetryConfig{
		MaxAttempts:  3,
		InitialDelay: 1 * time.Second,
		MaxDelay:     30 * time.Second,
		Multiplier:   2.0,
		Timeout:      1 * time.Minute,
		RetryableStatusCodes: []int{
			429, // Too Many Requests
			500, // Internal Server Error
			502, // Bad Gateway
			503, // Service Unavailable
			504, // Gateway Timeout
		},
	}
}

// RetryFilter implements retry logic with configurable backoff strategies.
type RetryFilter struct {
	*FilterBase

	// Configuration
	config RetryConfig

	// Current retry count
	retryCount atomic.Int64

	// Last error encountered
	lastError atomic.Value

	// Statistics tracking
	stats   RetryStatistics
	statsMu sync.RWMutex

	// Backoff strategy
	backoff BackoffStrategy
}

// NewRetryFilter creates a new retry filter.
func NewRetryFilter(config RetryConfig, backoff BackoffStrategy) *RetryFilter {
	return &RetryFilter{
		FilterBase: NewFilterBase("retry", "resilience"),
		config:     config,
		stats: RetryStatistics{
			RetryReasons: make(map[string]uint64),
		},
		backoff: backoff,
	}
}

// ExponentialBackoff implements exponential backoff with optional jitter.
type ExponentialBackoff struct {
	InitialDelay time.Duration
	MaxDelay     time.Duration
	Multiplier   float64
	JitterFactor float64 // 0.0 to 1.0, 0 = no jitter
}

// NewExponentialBackoff creates a new exponential backoff strategy.
func NewExponentialBackoff(initial, max time.Duration, multiplier float64) *ExponentialBackoff {
	return &ExponentialBackoff{
		InitialDelay: initial,
		MaxDelay:     max,
		Multiplier:   multiplier,
		JitterFactor: 0.1, // 10% jitter by default
	}
}

// NextDelay calculates the next retry delay.
func (eb *ExponentialBackoff) NextDelay(attempt int) time.Duration {
	if attempt <= 0 {
		return 0
	}

	// Calculate exponential delay: initialDelay * (multiplier ^ attempt)
	delay := float64(eb.InitialDelay) * math.Pow(eb.Multiplier, float64(attempt-1))

	// Cap at max delay
	if delay > float64(eb.MaxDelay) {
		delay = float64(eb.MaxDelay)
	}

	// Add jitter to prevent thundering herd
	if eb.JitterFactor > 0 {
		delay = eb.addJitter(delay, eb.JitterFactor)
	}

	return time.Duration(delay)
}

// addJitter adds random jitter to prevent synchronized retries.
func (eb *ExponentialBackoff) addJitter(delay float64, factor float64) float64 {
	// Jitter range: delay ± (delay * factor * random)
	jitterRange := delay * factor
	jitter := (rand.Float64()*2 - 1) * jitterRange // -jitterRange to +jitterRange

	result := delay + jitter
	if result < 0 {
		result = 0
	}

	return result
}

// Reset resets the backoff state (no-op for stateless strategy).
func (eb *ExponentialBackoff) Reset() {
	// Stateless strategy, nothing to reset
}

// LinearBackoff implements linear backoff strategy.
type LinearBackoff struct {
	InitialDelay time.Duration
	Increment    time.Duration
	MaxDelay     time.Duration
	JitterFactor float64
}

// NewLinearBackoff creates a new linear backoff strategy.
func NewLinearBackoff(initial, increment, max time.Duration) *LinearBackoff {
	return &LinearBackoff{
		InitialDelay: initial,
		Increment:    increment,
		MaxDelay:     max,
		JitterFactor: 0.1, // 10% jitter by default
	}
}

// NextDelay calculates the next retry delay.
func (lb *LinearBackoff) NextDelay(attempt int) time.Duration {
	if attempt <= 0 {
		return 0
	}

	// Calculate linear delay: initialDelay + (increment * attempt)
	delay := lb.InitialDelay + time.Duration(attempt-1)*lb.Increment

	// Cap at max delay
	if delay > lb.MaxDelay {
		delay = lb.MaxDelay
	}

	// Add jitter if configured
	if lb.JitterFactor > 0 {
		delayFloat := float64(delay)
		delayFloat = lb.addJitter(delayFloat, lb.JitterFactor)
		delay = time.Duration(delayFloat)
	}

	return delay
}

// addJitter adds random jitter to the delay.
func (lb *LinearBackoff) addJitter(delay float64, factor float64) float64 {
	jitterRange := delay * factor
	jitter := (rand.Float64()*2 - 1) * jitterRange

	result := delay + jitter
	if result < 0 {
		result = 0
	}

	return result
}

// Reset resets the backoff state (no-op for stateless strategy).
func (lb *LinearBackoff) Reset() {
	// Stateless strategy, nothing to reset
}

// addJitter adds random jitter to prevent thundering herd problem.
// factor should be between 0.0 and 1.0, where 0 = no jitter, 1 = ±100% jitter.
func addJitter(delay time.Duration, factor float64) time.Duration {
	if factor <= 0 {
		return delay
	}

	if factor > 1.0 {
		factor = 1.0
	}

	delayFloat := float64(delay)
	jitterRange := delayFloat * factor

	// Generate random jitter in range [-jitterRange, +jitterRange]
	jitter := (rand.Float64()*2 - 1) * jitterRange

	result := delayFloat + jitter
	if result < 0 {
		result = 0
	}

	return time.Duration(result)
}

// FullJitterBackoff adds full jitter to any base strategy.
type FullJitterBackoff struct {
	BaseStrategy BackoffStrategy
}

// NewFullJitterBackoff wraps a base strategy with full jitter.
func NewFullJitterBackoff(base BackoffStrategy) *FullJitterBackoff {
	return &FullJitterBackoff{
		BaseStrategy: base,
	}
}

// NextDelay returns delay with full jitter (0 to base delay).
func (fjb *FullJitterBackoff) NextDelay(attempt int) time.Duration {
	baseDelay := fjb.BaseStrategy.NextDelay(attempt)
	// Full jitter: random value between 0 and baseDelay
	return time.Duration(rand.Float64() * float64(baseDelay))
}

// Reset resets the underlying strategy.
func (fjb *FullJitterBackoff) Reset() {
	fjb.BaseStrategy.Reset()
}

// DecorrelatedJitterBackoff implements AWS-style decorrelated jitter.
type DecorrelatedJitterBackoff struct {
	BaseDelay     time.Duration
	MaxDelay      time.Duration
	previousDelay time.Duration
}

// NewDecorrelatedJitterBackoff creates decorrelated jitter backoff.
func NewDecorrelatedJitterBackoff(base, max time.Duration) *DecorrelatedJitterBackoff {
	return &DecorrelatedJitterBackoff{
		BaseDelay: base,
		MaxDelay:  max,
	}
}

// NextDelay calculates decorrelated jitter delay.
func (djb *DecorrelatedJitterBackoff) NextDelay(attempt int) time.Duration {
	if attempt <= 1 {
		djb.previousDelay = djb.BaseDelay
		return djb.BaseDelay
	}

	// Decorrelated jitter: random between baseDelay and 3 * previousDelay
	minDelay := float64(djb.BaseDelay)
	maxDelay := float64(djb.previousDelay) * 3

	if maxDelay > float64(djb.MaxDelay) {
		maxDelay = float64(djb.MaxDelay)
	}

	delay := minDelay + rand.Float64()*(maxDelay-minDelay)
	djb.previousDelay = time.Duration(delay)

	return djb.previousDelay
}

// Reset resets the previous delay.
func (djb *DecorrelatedJitterBackoff) Reset() {
	djb.previousDelay = 0
}

// Process implements the Filter interface with retry logic.
func (f *RetryFilter) Process(ctx context.Context, data []byte) (*types.FilterResult, error) {
	var lastErr error
	var lastResult *types.FilterResult

	// Reset retry count for new request
	f.retryCount.Store(0)

	// Wrap with timeout if configured
	var cancel context.CancelFunc
	if f.config.Timeout > 0 {
		ctx, cancel = context.WithTimeout(ctx, f.config.Timeout)
		defer cancel()
	}

	// Track start time for timeout calculation
	startTime := time.Now()

	// Main retry loop
	for attempt := 1; attempt <= f.config.MaxAttempts || f.config.MaxAttempts == 0; attempt++ {
		// Check context cancellation
		select {
		case <-ctx.Done():
			return nil, ctx.Err()
		default:
		}

		// Check if we've exceeded total timeout
		if f.config.Timeout > 0 && time.Since(startTime) >= f.config.Timeout {
			f.recordFailure(attempt, "timeout")
			return nil, fmt.Errorf("retry timeout exceeded after %v", time.Since(startTime))
		}

		// Calculate remaining time for this attempt
		var attemptCtx context.Context
		if f.config.Timeout > 0 {
			remaining := f.config.Timeout - time.Since(startTime)
			if remaining <= 0 {
				f.recordFailure(attempt, "timeout")
				return nil, context.DeadlineExceeded
			}
			var attemptCancel context.CancelFunc
			attemptCtx, attemptCancel = context.WithTimeout(ctx, remaining)
			defer attemptCancel()
		} else {
			attemptCtx = ctx
		}

		// Process attempt
		result, err := f.processAttempt(attemptCtx, data)

		// Success - return immediately
		if err == nil && result != nil && result.Status != types.Error {
			f.recordSuccess(attempt)
			return result, nil
		}

		// Store last error and result
		lastErr = err
		lastResult = result
		f.lastError.Store(lastErr)

		// Check if we should retry
		if !f.shouldRetry(err, result, attempt) {
			f.recordFailure(attempt, "not_retryable")
			break
		}

		// Don't sleep after last attempt
		if attempt >= f.config.MaxAttempts && f.config.MaxAttempts > 0 {
			f.recordFailure(attempt, "max_attempts")
			break
		}

		// Calculate backoff delay
		delay := f.backoff.NextDelay(attempt)

		// Check if delay would exceed timeout
		if f.config.Timeout > 0 {
			remaining := f.config.Timeout - time.Since(startTime)
			if remaining <= delay {
				f.recordFailure(attempt, "timeout_before_retry")
				return nil, fmt.Errorf("timeout would be exceeded before next retry")
			}
		}

		// Record delay in statistics
		f.recordDelay(delay)

		// Sleep with context cancellation check
		timer := time.NewTimer(delay)
		select {
		case <-ctx.Done():
			timer.Stop()
			return nil, ctx.Err()
		case <-timer.C:
			// Continue to next attempt
		}

		// Increment retry count
		f.retryCount.Add(1)
	}

	// All attempts failed - return detailed exception
	totalDuration := time.Since(startTime)
	attempts := int(f.retryCount.Load()) + 1

	exception := &RetryExhaustedException{
		Attempts:      attempts,
		LastError:     lastErr,
		TotalDuration: totalDuration,
	}

	// Add delays from statistics
	f.statsMu.RLock()
	if len(f.stats.BackoffDelays) > 0 {
		exception.Delays = make([]time.Duration, len(f.stats.BackoffDelays))
		copy(exception.Delays, f.stats.BackoffDelays)
	}
	f.statsMu.RUnlock()

	if lastErr != nil {
		return nil, exception
	}

	return lastResult, nil
}

// processAttempt simulates processing (would call actual downstream).
func (f *RetryFilter) processAttempt(ctx context.Context, data []byte) (*types.FilterResult, error) {
	// In real implementation, this would call the next filter or service
	// For now, simulate with a simple pass-through
	return types.ContinueWith(data), nil
}

// shouldRetry determines if an error is retryable.
func (f *RetryFilter) shouldRetry(err error, result *types.FilterResult, attempt int) bool {
	if err == nil && result != nil && result.Status != types.Error {
		return false // Success, no retry needed
	}

	// Use custom retry condition if provided
	if f.config.RetryCondition != nil {
		return f.config.RetryCondition(err, result)
	}

	// Default retry logic
	return f.defaultRetryCondition(err, result)
}

// defaultRetryCondition is the default retry logic.
func (f *RetryFilter) defaultRetryCondition(err error, result *types.FilterResult) bool {
	// Check if error is in retryable list
	if len(f.config.RetryableErrors) > 0 {
		for _, retryableErr := range f.config.RetryableErrors {
			if errors.Is(err, retryableErr) {
				return true
			}
		}
		return false // Not in retryable list
	}

	// Check status codes if result available
	if result != nil && len(f.config.RetryableStatusCodes) > 0 {
		if statusCode, ok := result.Metadata["status_code"].(int); ok {
			for _, code := range f.config.RetryableStatusCodes {
				if statusCode == code {
					return true
				}
			}
			return false
		}
	}

	// Default: retry all errors
	return err != nil || (result != nil && result.Status == types.Error)
}

// Common retry conditions for convenience

// RetryOnError retries only on errors.
func RetryOnError(err error, result *types.FilterResult) bool {
	return err != nil || (result != nil && result.Status == types.Error)
}

// RetryOnStatusCodes returns a condition that retries on specific status codes.
func RetryOnStatusCodes(codes ...int) RetryCondition {
	return func(err error, result *types.FilterResult) bool {
		if result == nil || result.Metadata == nil {
			return err != nil
		}

		if statusCode, ok := result.Metadata["status_code"].(int); ok {
			for _, code := range codes {
				if statusCode == code {
					return true
				}
			}
		}
		return false
	}
}

// RetryOnTimeout retries on timeout errors.
func RetryOnTimeout(err error, result *types.FilterResult) bool {
	if err == nil {
		return false
	}

	// Check for context timeout
	if errors.Is(err, context.DeadlineExceeded) {
		return true
	}

	// Check error string for timeout indication
	errStr := err.Error()
	return errors.Is(err, context.DeadlineExceeded) ||
		errors.Is(err, context.Canceled) ||
		contains(errStr, "timeout") ||
		contains(errStr, "deadline")
}

// contains checks if string contains substring (case-insensitive).
func contains(s, substr string) bool {
	s = fmt.Sprintf("%v", s)
	return len(s) > 0 && len(substr) > 0 &&
		(s == substr ||
			len(s) > len(substr) &&
				(s[:len(substr)] == substr || s[len(s)-len(substr):] == substr))
}

// recordSuccess records successful retry.
func (f *RetryFilter) recordSuccess(attempts int) {
	f.statsMu.Lock()
	defer f.statsMu.Unlock()

	f.stats.TotalAttempts += uint64(attempts)
	if attempts > 1 {
		f.stats.SuccessfulRetries++
	}
}

// recordFailure records failed retry.
func (f *RetryFilter) recordFailure(attempts int, reason string) {
	f.statsMu.Lock()
	defer f.statsMu.Unlock()

	f.stats.TotalAttempts += uint64(attempts)
	f.stats.FailedRetries++

	if f.stats.RetryReasons == nil {
		f.stats.RetryReasons = make(map[string]uint64)
	}
	f.stats.RetryReasons[reason]++
}

// recordDelay records backoff delay.
func (f *RetryFilter) recordDelay(delay time.Duration) {
	f.statsMu.Lock()
	defer f.statsMu.Unlock()

	f.stats.BackoffDelays = append(f.stats.BackoffDelays, delay)

	// Update max delay
	if delay > f.stats.MaxDelay {
		f.stats.MaxDelay = delay
	}

	// Calculate average
	var total time.Duration
	for _, d := range f.stats.BackoffDelays {
		total += d
	}
	if len(f.stats.BackoffDelays) > 0 {
		f.stats.AverageDelay = total / time.Duration(len(f.stats.BackoffDelays))
	}
}

// GetStatistics returns current retry statistics with calculated metrics.
func (f *RetryFilter) GetStatistics() RetryStatistics {
	f.statsMu.RLock()
	defer f.statsMu.RUnlock()

	// Create a copy of statistics
	statsCopy := RetryStatistics{
		TotalAttempts:     f.stats.TotalAttempts,
		SuccessfulRetries: f.stats.SuccessfulRetries,
		FailedRetries:     f.stats.FailedRetries,
		MaxDelay:          f.stats.MaxDelay,
		AverageDelay:      f.stats.AverageDelay,
	}

	// Copy retry reasons
	if f.stats.RetryReasons != nil {
		statsCopy.RetryReasons = make(map[string]uint64)
		for reason, count := range f.stats.RetryReasons {
			statsCopy.RetryReasons[reason] = count
		}
	}

	// Copy backoff delays (limit to last 100 for memory)
	if len(f.stats.BackoffDelays) > 0 {
		start := 0
		if len(f.stats.BackoffDelays) > 100 {
			start = len(f.stats.BackoffDelays) - 100
		}
		statsCopy.BackoffDelays = make([]time.Duration, len(f.stats.BackoffDelays[start:]))
		copy(statsCopy.BackoffDelays, f.stats.BackoffDelays[start:])
	}

	// Calculate retry success rate
	totalRetries := statsCopy.SuccessfulRetries + statsCopy.FailedRetries
	if totalRetries > 0 {
		statsCopy.RetrySuccessRate = float64(statsCopy.SuccessfulRetries) / float64(totalRetries) * 100.0
	}

	return statsCopy
}

// GetRetrySuccessRate returns the percentage of successful retries.
func (stats *RetryStatistics) GetRetrySuccessRate() float64 {
	total := stats.SuccessfulRetries + stats.FailedRetries
	if total == 0 {
		return 0
	}
	return float64(stats.SuccessfulRetries) / float64(total) * 100.0
}

// AverageAttemptsPerRequest calculates average attempts per request.
func (stats *RetryStatistics) AverageAttemptsPerRequest() float64 {
	requests := stats.SuccessfulRetries + stats.FailedRetries
	if requests == 0 {
		return 0
	}
	return float64(stats.TotalAttempts) / float64(requests)
}
