// Package filters provides built-in filters for the MCP Filter SDK.
package filters

import (
	"container/ring"
	"context"
	"fmt"
	"sync"
	"sync/atomic"
	"time"

	"github.com/GopherSecurity/gopher-mcp/src/types"
)

// State represents the state of the circuit breaker.
type State int

// CircuitBreakerMetrics tracks circuit breaker performance metrics.
type CircuitBreakerMetrics struct {
	// State tracking
	CurrentState      State
	StateChanges      uint64
	TimeInClosed      time.Duration
	TimeInOpen        time.Duration
	TimeInHalfOpen    time.Duration
	LastStateChange   time.Time
	
	// Success/Failure rates
	TotalRequests     uint64
	SuccessfulRequests uint64
	FailedRequests    uint64
	RejectedRequests  uint64
	SuccessRate       float64
	FailureRate       float64
	
	// Recovery metrics
	LastOpenTime      time.Time
	LastRecoveryTime  time.Duration
	AverageRecoveryTime time.Duration
	RecoveryAttempts  uint64
}

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

// StateChangeCallback is called when circuit breaker state changes.
type StateChangeCallback func(from, to State)

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
	
	// OnStateChange is an optional callback for state transitions.
	OnStateChange StateChangeCallback
	
	// Logger for logging state transitions (optional).
	Logger func(format string, args ...interface{})
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
	
	// Half-open state limiter
	halfOpenAttempts atomic.Int32
	
	// Metrics tracking
	metrics CircuitBreakerMetrics
	metricsMu sync.RWMutex
	stateStartTime time.Time
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
	f.stateStartTime = time.Now()
	
	// Initialize metrics
	f.metrics.CurrentState = Closed
	f.metrics.LastStateChange = time.Now()
	
	return f
}

// transitionTo performs thread-safe state transitions with logging and callbacks.
func (f *CircuitBreakerFilter) transitionTo(newState State) bool {
	currentState := f.state.Load().(State)
	
	// Validate transition
	if !f.isValidTransition(currentState, newState) {
		// Log invalid transition attempt
		if f.config.Logger != nil {
			f.config.Logger("Circuit breaker: invalid transition from %s to %s", 
				currentState.String(), newState.String())
		}
		return false
	}
	
	// Atomic state change
	if !f.state.CompareAndSwap(currentState, newState) {
		// State changed by another goroutine
		return false
	}
	
	// Log successful transition
	if f.config.Logger != nil {
		f.config.Logger("Circuit breaker: state changed from %s to %s", 
			currentState.String(), newState.String())
	}
	
	// Update metrics (would integrate with actual metrics system)
	f.updateMetrics(currentState, newState)
	
	// Handle transition side effects
	switch newState {
	case Open:
		// Record when we opened the circuit
		f.lastFailureTime.Store(time.Now())
		f.failures.Store(0)
		f.successes.Store(0)
		
		if f.config.Logger != nil {
			f.config.Logger("Circuit breaker opened at %v", time.Now())
		}
		
	case HalfOpen:
		// Reset counters for testing phase
		f.failures.Store(0)
		f.successes.Store(0)
		
		if f.config.Logger != nil {
			f.config.Logger("Circuit breaker entering half-open state for testing")
		}
		
	case Closed:
		// Reset all counters
		f.failures.Store(0)
		f.successes.Store(0)
		f.lastFailureTime.Store(time.Time{})
		
		if f.config.Logger != nil {
			f.config.Logger("Circuit breaker closed - normal operation resumed")
		}
	}
	
	// Call optional state change callback
	if f.config.OnStateChange != nil {
		go f.config.OnStateChange(currentState, newState)
	}
	
	return true
}

// updateMetrics updates metrics for state transitions.
func (f *CircuitBreakerFilter) updateMetrics(from, to State) {
	f.metricsMu.Lock()
	defer f.metricsMu.Unlock()
	
	now := time.Now()
	elapsed := now.Sub(f.stateStartTime)
	
	// Update time in state
	switch from {
	case Closed:
		f.metrics.TimeInClosed += elapsed
	case Open:
		f.metrics.TimeInOpen += elapsed
		// Track recovery time when leaving Open
		if to == HalfOpen || to == Closed {
			f.metrics.LastRecoveryTime = elapsed
			f.metrics.RecoveryAttempts++
			// Update average recovery time
			if f.metrics.RecoveryAttempts > 0 {
				total := f.metrics.AverageRecoveryTime * time.Duration(f.metrics.RecoveryAttempts-1)
				f.metrics.AverageRecoveryTime = (total + elapsed) / time.Duration(f.metrics.RecoveryAttempts)
			}
		}
	case HalfOpen:
		f.metrics.TimeInHalfOpen += elapsed
	}
	
	// Update state tracking
	f.metrics.CurrentState = to
	f.metrics.StateChanges++
	f.metrics.LastStateChange = now
	f.stateStartTime = now
	
	// Record open time
	if to == Open {
		f.metrics.LastOpenTime = now
	}
	
	// Update filter base statistics if available
	if f.FilterBase != nil {
		stats := f.FilterBase.GetStats()
		stats.CustomMetrics = map[string]interface{}{
			"state":           to.String(),
			"transitions":     f.metrics.StateChanges,
			"last_transition": now,
		}
	}
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

// tryTransitionToHalfOpen attempts atomic transition from Open to HalfOpen.
func (f *CircuitBreakerFilter) tryTransitionToHalfOpen() bool {
	// Only transition if we're currently in Open state
	expectedState := Open
	newState := HalfOpen
	
	// Check timeout first to avoid unnecessary CAS operations
	if !f.shouldTransitionToHalfOpen() {
		return false
	}
	
	// Atomic compare-and-swap for race-free transition
	return f.state.CompareAndSwap(expectedState, newState)
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

// Process implements the Filter interface with circuit breaker logic.
func (f *CircuitBreakerFilter) Process(ctx context.Context, data []byte) (*types.FilterResult, error) {
	currentState := f.state.Load().(State)
	
	switch currentState {
	case Open:
		// Try atomic transition to half-open if timeout elapsed
		if f.tryTransitionToHalfOpen() {
			// Successfully transitioned, continue with half-open processing
			currentState = HalfOpen
			// Reset counters for testing phase
			f.failures.Store(0)
			f.successes.Store(0)
		} else {
			// Circuit is open, reject immediately
			f.updateRequestMetrics(false, true)
			return nil, fmt.Errorf("circuit breaker is open")
		}
	}
	
	// Handle half-open state with limited attempts
	if currentState == HalfOpen {
		// Check concurrent attempt limit
		attempts := f.halfOpenAttempts.Add(1)
		defer f.halfOpenAttempts.Add(-1)
		
		if attempts > int32(f.config.HalfOpenMaxAttempts) {
			// Too many concurrent attempts, reject
			f.updateRequestMetrics(false, true)
			return nil, fmt.Errorf("circuit breaker half-open limit exceeded")
		}
	}
	
	// Process the request (would normally call downstream)
	// For now, we'll simulate processing
	result := f.processDownstream(ctx, data)
	
	// Record outcome
	if result.Status == types.Error {
		f.recordFailure()
		f.updateRequestMetrics(false, false)
		// Handle state transition based on failure
		if f.state.Load().(State) == Open {
			return nil, fmt.Errorf("circuit breaker opened due to failures")
		}
	} else {
		f.recordSuccess()
		f.updateRequestMetrics(true, false)
	}
	
	return result, nil
}

// processDownstream simulates calling the downstream service.
// In a real implementation, this would delegate to another filter or service.
func (f *CircuitBreakerFilter) processDownstream(ctx context.Context, data []byte) *types.FilterResult {
	// Simulate processing - in real use, this would call the next filter
	// For demonstration, we'll just pass through
	return types.ContinueWith(data)
}

// RecordSuccess records a successful operation externally.
// Public method to record outcomes from external sources.
func (f *CircuitBreakerFilter) RecordSuccess() {
	currentState := f.state.Load().(State)
	
	switch currentState {
	case Closed:
		// In closed state, reset failure count on success
		if f.failures.Load() > 0 {
			f.failures.Store(0)
		}
		// Increment success counter
		f.successes.Add(1)
		
	case HalfOpen:
		// In half-open, increment success counter
		f.successes.Add(1)
		
		// Check if we should transition to closed
		if f.shouldTransitionToClosed() {
			f.transitionTo(Closed)
		}
	}
	
	// Update sliding window
	f.windowMu.Lock()
	f.slidingWindow.Value = true
	f.slidingWindow = f.slidingWindow.Next()
	f.windowMu.Unlock()
}

// RecordFailure records a failed operation externally.
// Public method to record outcomes from external sources.
func (f *CircuitBreakerFilter) RecordFailure() {
	currentState := f.state.Load().(State)
	
	// Increment failure counter
	f.failures.Add(1)
	
	// Update sliding window
	f.windowMu.Lock()
	f.slidingWindow.Value = false
	f.slidingWindow = f.slidingWindow.Next()
	f.windowMu.Unlock()
	
	switch currentState {
	case Closed:
		// Check thresholds for opening
		if f.shouldTransitionToOpen() {
			f.transitionTo(Open)
		}
		
	case HalfOpen:
		// Any failure in half-open immediately opens
		f.transitionTo(Open)
		
	case Open:
		// Already open, just record the failure
	}
}

// GetMetrics returns current circuit breaker metrics.
func (f *CircuitBreakerFilter) GetMetrics() CircuitBreakerMetrics {
	f.metricsMu.RLock()
	defer f.metricsMu.RUnlock()
	
	// Create a copy of metrics
	metricsCopy := f.metrics
	
	// Calculate current rates
	if metricsCopy.TotalRequests > 0 {
		metricsCopy.SuccessRate = float64(metricsCopy.SuccessfulRequests) / float64(metricsCopy.TotalRequests)
		metricsCopy.FailureRate = float64(metricsCopy.FailedRequests) / float64(metricsCopy.TotalRequests)
	}
	
	// Update time in current state
	currentState := f.state.Load().(State)
	elapsed := time.Since(f.stateStartTime)
	switch currentState {
	case Closed:
		metricsCopy.TimeInClosed += elapsed
	case Open:
		metricsCopy.TimeInOpen += elapsed
	case HalfOpen:
		metricsCopy.TimeInHalfOpen += elapsed
	}
	
	return metricsCopy
}

// updateRequestMetrics updates request counters.
func (f *CircuitBreakerFilter) updateRequestMetrics(success bool, rejected bool) {
	f.metricsMu.Lock()
	defer f.metricsMu.Unlock()
	
	f.metrics.TotalRequests++
	
	if rejected {
		f.metrics.RejectedRequests++
	} else if success {
		f.metrics.SuccessfulRequests++
	} else {
		f.metrics.FailedRequests++
	}
}