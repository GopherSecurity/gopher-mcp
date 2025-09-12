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
}