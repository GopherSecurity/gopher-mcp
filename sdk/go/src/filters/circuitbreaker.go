// Package filters provides built-in filters for the MCP Filter SDK.
package filters

import (
	"context"
	"sync/atomic"
	"time"

	"github.com/GopherSecurity/gopher-mcp/src/types"
)

// State represents the state of the circuit breaker.
type State int

// CircuitBreakerConfig configures the circuit breaker behavior.
type CircuitBreakerConfig struct {
	// FailureThreshold is the number of failures before opening
	FailureThreshold int
	
	// SuccessThreshold is the number of successes to close from half-open
	SuccessThreshold int
	
	// Timeout before trying half-open state
	Timeout time.Duration
	
	// HalfOpenMaxAttempts limits concurrent attempts in half-open state
	HalfOpenMaxAttempts int
	
	// FailureRate threshold (0.0 to 1.0)
	FailureRate float64
	
	// MinimumRequestVolume before failure rate is calculated
	MinimumRequestVolume int
}

// CircuitBreakerFilter implements the circuit breaker pattern.
type CircuitBreakerFilter struct {
	*FilterBase
	
	// Current state (atomic.Value stores State)
	state atomic.Value
	
	// Failure counter
	failures atomic.Int64
	
	// Success counter
	successes atomic.Int64
	
	// Last failure time (atomic.Value stores time.Time)
	lastFailureTime atomic.Value
	
	// Configuration
	config CircuitBreakerConfig
}

// NewCircuitBreakerFilter creates a new circuit breaker filter.
func NewCircuitBreakerFilter(config CircuitBreakerConfig) *CircuitBreakerFilter {
	f := &CircuitBreakerFilter{
		FilterBase: NewFilterBase("circuit-breaker", "resilience"),
		config:     config,
	}
	
	// Initialize state
	f.state.Store(State(0)) // Closed state
	f.lastFailureTime.Store(time.Time{})
	
	return f
}