// Package filters provides built-in filters for the MCP Filter SDK.
package filters

import (
	"context"
	"math"
	"math/rand"
	"sync/atomic"
	"time"

	"github.com/GopherSecurity/gopher-mcp/src/types"
)

// BackoffStrategy defines the interface for retry delay calculation.
type BackoffStrategy interface {
	NextDelay(attempt int) time.Duration
	Reset()
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
}

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
	stats RetryStatistics
	
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