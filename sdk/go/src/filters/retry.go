// Package filters provides built-in filters for the MCP Filter SDK.
package filters

import (
	"context"
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