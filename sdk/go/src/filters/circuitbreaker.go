// Package filters provides built-in filters for the MCP Filter SDK.
package filters

import (
	"container/ring"
	"context"
	"sync"
	"sync/atomic"
	"time"

	"github.com/GopherSecurity/gopher-mcp/src/types"
)

// State represents the state of the circuit breaker.
type State int

const (
	// Closed state - normal operation, requests pass through.
	// The circuit breaker monitors for failures.
	Closed State = iota
	
	// Open state - circuit is open, rejecting all requests immediately.
	// This protects the downstream service from overload.
	Open
	
	// HalfOpen state - testing recovery, allowing limited requests.
	// Used to check if the downstream service has recovered.
	HalfOpen
)

// String returns a string representation of the state for logging.
func (s State) String() string {
	switch s {
	case Closed:
		return "CLOSED"
	case Open:
		return "OPEN"
	case HalfOpen:
		return "HALF_OPEN"
	default:
		return "UNKNOWN"
	}
}

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
	
	// Sliding window for failure rate calculation
	slidingWindow *ring.Ring
	windowMu      sync.Mutex
}

// NewCircuitBreakerFilter creates a new circuit breaker filter.
func NewCircuitBreakerFilter(config CircuitBreakerConfig) *CircuitBreakerFilter {
	f := &CircuitBreakerFilter{
		FilterBase:    NewFilterBase("circuit-breaker", "resilience"),
		config:        config,
		slidingWindow: ring.New(100), // Last 100 requests for rate calculation
	}
	
	// Initialize state
	f.state.Store(Closed)
	f.lastFailureTime.Store(time.Time{})
	
	return f
}

// transitionTo performs thread-safe state transitions.
func (f *CircuitBreakerFilter) transitionTo(newState State) bool {
	currentState := f.state.Load().(State)
	
	// Validate transition
	if !f.isValidTransition(currentState, newState) {
		return false
	}
	
	// Atomic state change
	if !f.state.CompareAndSwap(currentState, newState) {
		// State changed by another goroutine
		return false
	}
	
	// Handle transition side effects
	switch newState {
	case Open:
		// Record when we opened the circuit
		f.lastFailureTime.Store(time.Now())
		f.failures.Store(0)
		f.successes.Store(0)
	case HalfOpen:
		// Reset counters for testing phase
		f.failures.Store(0)
		f.successes.Store(0)
	case Closed:
		// Reset all counters
		f.failures.Store(0)
		f.successes.Store(0)
		f.lastFailureTime.Store(time.Time{})
	}
	
	return true
}

// isValidTransition checks if a state transition is allowed.
func (f *CircuitBreakerFilter) isValidTransition(from, to State) bool {
	switch from {
	case Closed:
		// Can only go to Open from Closed
		return to == Open
	case Open:
		// Can only go to HalfOpen from Open
		return to == HalfOpen
	case HalfOpen:
		// Can go to either Closed or Open from HalfOpen
		return to == Closed || to == Open
	default:
		return false
	}
}

// shouldTransitionToOpen checks if we should open the circuit.
func (f *CircuitBreakerFilter) shouldTransitionToOpen() bool {
	failures := f.failures.Load()
	
	// Check absolute failure threshold
	if failures >= int64(f.config.FailureThreshold) {
		return true
	}
	
	// Check failure rate if we have enough volume
	total := f.failures.Load() + f.successes.Load()
	if total >= int64(f.config.MinimumRequestVolume) {
		failureRate := float64(failures) / float64(total)
		if failureRate >= f.config.FailureRate {
			return true
		}
	}
	
	return false
}

// shouldTransitionToHalfOpen checks if timeout has elapsed for half-open transition.
func (f *CircuitBreakerFilter) shouldTransitionToHalfOpen() bool {
	lastFailure := f.lastFailureTime.Load().(time.Time)
	if lastFailure.IsZero() {
		return false
	}
	
	return time.Since(lastFailure) >= f.config.Timeout
}

// shouldTransitionToClosed checks if we should close from half-open.
func (f *CircuitBreakerFilter) shouldTransitionToClosed() bool {
	return f.successes.Load() >= int64(f.config.SuccessThreshold)
}

// recordFailure records a failure and checks if circuit should open.
func (f *CircuitBreakerFilter) recordFailure() {
	// Increment failure counter
	f.failures.Add(1)
	
	// Add to sliding window
	f.windowMu.Lock()
	f.slidingWindow.Value = false // false = failure
	f.slidingWindow = f.slidingWindow.Next()
	f.windowMu.Unlock()
	
	// Check state and thresholds
	currentState := f.state.Load().(State)
	
	switch currentState {
	case Closed:
		// Check if we should open the circuit
		if f.shouldTransitionToOpen() {
			f.transitionTo(Open)
		}
	case HalfOpen:
		// Any failure in half-open immediately opens the circuit
		f.transitionTo(Open)
	}
}

// recordSuccess records a success and checks state transitions.
func (f *CircuitBreakerFilter) recordSuccess() {
	// Increment success counter
	f.successes.Add(1)
	
	// Add to sliding window
	f.windowMu.Lock()
	f.slidingWindow.Value = true // true = success
	f.slidingWindow = f.slidingWindow.Next()
	f.windowMu.Unlock()
	
	// Check state
	currentState := f.state.Load().(State)
	
	if currentState == HalfOpen {
		// Check if we should close the circuit
		if f.shouldTransitionToClosed() {
			f.transitionTo(Closed)
		}
	}
}

// calculateFailureRate calculates the current failure rate from sliding window.
func (f *CircuitBreakerFilter) calculateFailureRate() float64 {
	f.windowMu.Lock()
	defer f.windowMu.Unlock()
	
	var failures, total int
	f.slidingWindow.Do(func(v interface{}) {
		if v != nil {
			total++
			if success, ok := v.(bool); ok && !success {
				failures++
			}
		}
	})
	
	if total == 0 {
		return 0
	}
	
	return float64(failures) / float64(total)
}