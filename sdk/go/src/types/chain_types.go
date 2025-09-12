// Package types provides core type definitions for the MCP Filter SDK.
package types

import (
	"fmt"
	"time"
)

// ExecutionMode defines how filters in a chain are executed.
type ExecutionMode int

const (
	// Sequential processes filters one by one in order.
	// Each filter must complete before the next one starts.
	Sequential ExecutionMode = iota

	// Parallel processes filters concurrently.
	// Results are aggregated after all filters complete.
	Parallel

	// Pipeline processes filters in a streaming pipeline.
	// Data flows through filters using channels.
	Pipeline

	// Adaptive chooses execution mode based on load and filter characteristics.
	// The system dynamically selects the optimal mode.
	Adaptive
)

// String returns a human-readable string representation of the ExecutionMode.
func (m ExecutionMode) String() string {
	switch m {
	case Sequential:
		return "Sequential"
	case Parallel:
		return "Parallel"
	case Pipeline:
		return "Pipeline"
	case Adaptive:
		return "Adaptive"
	default:
		return fmt.Sprintf("ExecutionMode(%d)", m)
	}
}

// ChainConfig contains configuration settings for a filter chain.
type ChainConfig struct {
	// Name is the unique identifier for the chain.
	Name string `json:"name"`

	// ExecutionMode determines how filters are executed.
	ExecutionMode ExecutionMode `json:"execution_mode"`

	// MaxConcurrency limits concurrent filter execution in parallel mode.
	MaxConcurrency int `json:"max_concurrency"`

	// BufferSize sets the channel buffer size for pipeline mode.
	BufferSize int `json:"buffer_size"`

	// ErrorHandling defines how errors are handled: "fail-fast", "continue", "isolate".
	ErrorHandling string `json:"error_handling"`

	// Timeout is the maximum time for chain execution.
	Timeout time.Duration `json:"timeout"`

	// EnableMetrics enables performance metrics collection.
	EnableMetrics bool `json:"enable_metrics"`

	// EnableTracing enables execution tracing for debugging.
	EnableTracing bool `json:"enable_tracing"`
	
	// BypassOnError allows chain to continue on errors.
	BypassOnError bool `json:"bypass_on_error"`
}

// Validate checks if the ChainConfig contains valid values.
// It returns descriptive errors for any validation failures.
func (c *ChainConfig) Validate() []error {
	var errors []error

	// Check Name is not empty
	if c.Name == "" {
		errors = append(errors, fmt.Errorf("chain name cannot be empty"))
	}

	// Check MaxConcurrency for parallel mode
	if c.ExecutionMode == Parallel && c.MaxConcurrency <= 0 {
		errors = append(errors, fmt.Errorf("max concurrency must be > 0 for parallel mode"))
	}

	// Check BufferSize for pipeline mode
	if c.ExecutionMode == Pipeline && c.BufferSize <= 0 {
		errors = append(errors, fmt.Errorf("buffer size must be > 0 for pipeline mode"))
	}

	// Validate ErrorHandling
	validErrorHandling := map[string]bool{
		"fail-fast": true,
		"continue":  true,
		"isolate":   true,
	}
	if c.ErrorHandling != "" && !validErrorHandling[c.ErrorHandling] {
		errors = append(errors, fmt.Errorf("invalid error handling: %s (must be fail-fast, continue, or isolate)", c.ErrorHandling))
	}

	// Check Timeout is reasonable
	if c.Timeout < 0 {
		errors = append(errors, fmt.Errorf("timeout cannot be negative"))
	}
	if c.Timeout > 0 && c.Timeout < time.Millisecond {
		errors = append(errors, fmt.Errorf("timeout too small: %v (minimum 1ms)", c.Timeout))
	}

	return errors
}

// ChainStatistics tracks performance metrics for a filter chain.
type ChainStatistics struct {
	// TotalExecutions is the total number of chain executions.
	TotalExecutions uint64 `json:"total_executions"`

	// SuccessCount is the number of successful executions.
	SuccessCount uint64 `json:"success_count"`

	// ErrorCount is the number of failed executions.
	ErrorCount uint64 `json:"error_count"`

	// AverageLatency is the average execution time.
	AverageLatency time.Duration `json:"average_latency"`

	// P50Latency is the 50th percentile latency.
	P50Latency time.Duration `json:"p50_latency"`

	// P90Latency is the 90th percentile latency.
	P90Latency time.Duration `json:"p90_latency"`

	// P99Latency is the 99th percentile latency.
	P99Latency time.Duration `json:"p99_latency"`

	// CurrentLoad is the current number of active executions.
	CurrentLoad int32 `json:"current_load"`

	// FilterStats contains statistics for each filter in the chain.
	FilterStats map[string]FilterStatistics `json:"filter_stats"`
}

// ChainState represents the lifecycle state of a filter chain.
type ChainState int

const (
	// Uninitialized means the chain is not ready to process data.
	// The chain is in this state before initialization completes.
	Uninitialized ChainState = iota

	// Ready means the chain is initialized and can process data.
	// All filters are configured and ready to receive data.
	Ready

	// Running means the chain is currently processing data.
	// One or more filters are actively processing.
	Running

	// Stopped means the chain has been shut down.
	// The chain cannot process data and must be reinitialized.
	Stopped
)

// String returns a human-readable string representation of the ChainState.
func (s ChainState) String() string {
	switch s {
	case Uninitialized:
		return "Uninitialized"
	case Ready:
		return "Ready"
	case Running:
		return "Running"
	case Stopped:
		return "Stopped"
	default:
		return fmt.Sprintf("ChainState(%d)", s)
	}
}

// CanTransitionTo validates if a state transition is allowed.
// It enforces the state machine rules for chain lifecycle.
func (s ChainState) CanTransitionTo(target ChainState) bool {
	switch s {
	case Uninitialized:
		// Can only transition to Ready or Stopped
		return target == Ready || target == Stopped
	case Ready:
		// Can transition to Running or Stopped
		return target == Running || target == Stopped
	case Running:
		// Can only transition to Ready or Stopped
		return target == Ready || target == Stopped
	case Stopped:
		// Can only transition to Uninitialized to restart
		return target == Uninitialized
	default:
		return false
	}
}

// IsActive returns true if the chain is in an active state.
// Active states are Ready and Running.
func (s ChainState) IsActive() bool {
	return s == Ready || s == Running
}

// IsTerminal returns true if the chain is in a terminal state.
// Terminal state is Stopped.
func (s ChainState) IsTerminal() bool {
	return s == Stopped
}

// ChainEventType represents the type of event that occurred in a filter chain.
type ChainEventType int

const (
	// ChainStarted indicates the chain has started processing.
	ChainStarted ChainEventType = iota

	// ChainCompleted indicates the chain has completed processing successfully.
	ChainCompleted

	// ChainError indicates the chain encountered an error during processing.
	ChainError

	// FilterAdded indicates a filter was added to the chain.
	FilterAdded

	// FilterRemoved indicates a filter was removed from the chain.
	FilterRemoved

	// StateChanged indicates the chain's state has changed.
	StateChanged
)

// String returns a human-readable string representation of the ChainEventType.
func (e ChainEventType) String() string {
	switch e {
	case ChainStarted:
		return "ChainStarted"
	case ChainCompleted:
		return "ChainCompleted"
	case ChainError:
		return "ChainError"
	case FilterAdded:
		return "FilterAdded"
	case FilterRemoved:
		return "FilterRemoved"
	case StateChanged:
		return "StateChanged"
	default:
		return fmt.Sprintf("ChainEventType(%d)", e)
	}
}

// ChainEventData contains data associated with a chain event.
// Different event types may use different fields.
type ChainEventData struct {
	// ChainName is the name of the chain that generated the event.
	ChainName string `json:"chain_name"`

	// EventType is the type of event that occurred.
	EventType ChainEventType `json:"event_type"`

	// Timestamp is when the event occurred.
	Timestamp time.Time `json:"timestamp"`

	// OldState is the previous state (for StateChanged events).
	OldState ChainState `json:"old_state,omitempty"`

	// NewState is the new state (for StateChanged events).
	NewState ChainState `json:"new_state,omitempty"`

	// FilterName is the name of the filter (for FilterAdded/FilterRemoved events).
	FilterName string `json:"filter_name,omitempty"`

	// FilterPosition is the position of the filter in the chain.
	FilterPosition int `json:"filter_position,omitempty"`

	// Error contains any error that occurred (for ChainError events).
	Error error `json:"error,omitempty"`

	// Duration is the processing time (for ChainCompleted events).
	Duration time.Duration `json:"duration,omitempty"`

	// ProcessedBytes is the number of bytes processed (for ChainCompleted events).
	ProcessedBytes uint64 `json:"processed_bytes,omitempty"`

	// Metadata contains additional event-specific data.
	Metadata map[string]interface{} `json:"metadata,omitempty"`
}

// ChainEventArgs provides context for chain events.
// It contains essential information about the chain and execution.
type ChainEventArgs struct {
	// ChainName is the unique identifier of the chain.
	ChainName string `json:"chain_name"`

	// State is the current state of the chain.
	State ChainState `json:"state"`

	// ExecutionID is a unique identifier for this execution instance.
	ExecutionID string `json:"execution_id"`

	// Timestamp is when the event was created.
	Timestamp time.Time `json:"timestamp"`

	// Metadata contains additional context-specific data.
	Metadata map[string]interface{} `json:"metadata,omitempty"`
}

// NewChainEventArgs creates a new ChainEventArgs with the provided details.
// It automatically sets the timestamp to the current time.
func NewChainEventArgs(chainName string, state ChainState, executionID string) *ChainEventArgs {
	return &ChainEventArgs{
		ChainName:   chainName,
		State:       state,
		ExecutionID: executionID,
		Timestamp:   time.Now(),
		Metadata:    make(map[string]interface{}),
	}
}

// WithMetadata adds metadata to the event args and returns the args for chaining.
func (e *ChainEventArgs) WithMetadata(key string, value interface{}) *ChainEventArgs {
	if e.Metadata == nil {
		e.Metadata = make(map[string]interface{})
	}
	e.Metadata[key] = value
	return e
}

// String returns a string representation of the ChainEventArgs.
func (e *ChainEventArgs) String() string {
	return fmt.Sprintf("ChainEvent{Chain: %s, State: %s, ExecutionID: %s, Time: %s}",
		e.ChainName, e.State, e.ExecutionID, e.Timestamp.Format(time.RFC3339))
}