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
	// FailureThreshold is the number of consecutive failures before opening the circuit.
	// Once this threshold is reached, the circuit breaker transitions to Open state.
	FailureThreshold int
	
	// SuccessThreshold is the number of consecutive successes required to close
	// the circuit from half-open state.
	SuccessThreshold int
	
	// Timeout is the duration to wait before transitioning from Open to HalfOpen state.
	// After this timeout, the circuit breaker will allow test requests.
	Timeout time.Duration
	
	// HalfOpenMaxAttempts limits the number of concurrent requests allowed
	// when the circuit is in half-open state.
	HalfOpenMaxAttempts int
	
	// FailureRate is the failure rate threshold (0.0 to 1.0).
	// If the failure rate exceeds this threshold, the circuit opens.
	FailureRate float64
	
	// MinimumRequestVolume is the minimum number of requests required
	// before the failure rate is calculated and considered.
	MinimumRequestVolume int
}

// DefaultCircuitBreakerConfig returns a default configuration.
func DefaultCircuitBreakerConfig() CircuitBreakerConfig {
	return CircuitBreakerConfig{
		FailureThreshold:     5,
		SuccessThreshold:     2,
		Timeout:              30 * time.Second,
		HalfOpenMaxAttempts:  3,
		FailureRate:          0.5,
		MinimumRequestVolume: 10,
	}
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