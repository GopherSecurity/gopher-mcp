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
	MaxAttempts          int
	InitialDelay         time.Duration
	MaxDelay             time.Duration
	Multiplier           float64
	RetryableErrors      []error
	RetryableStatusCodes []int
	Timeout              time.Duration
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