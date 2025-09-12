// Package types provides core type definitions for the MCP Filter SDK.
package types

import (
	"fmt"
	"time"
)

// FilterStatus represents the result status of a filter's processing operation.
// It determines how the filter chain should proceed after processing.
type FilterStatus int

const (
	// Continue indicates the filter processed successfully and the chain should continue.
	// The next filter in the chain will receive the processed data.
	Continue FilterStatus = iota

	// StopIteration indicates the filter processed successfully but the chain should stop.
	// No further filters will be executed, and the current result will be returned.
	StopIteration

	// Error indicates the filter encountered an error during processing.
	// The chain will stop and return the error unless configured to bypass errors.
	Error

	// NeedMoreData indicates the filter needs more data to complete processing.
	// Used for filters that work with streaming or chunked data.
	NeedMoreData

	// Buffered indicates the filter has buffered the data for later processing.
	// The chain may continue with empty data or wait based on configuration.
	Buffered
)

// String returns a human-readable string representation of the FilterStatus.
func (s FilterStatus) String() string {
	switch s {
	case Continue:
		return "Continue"
	case StopIteration:
		return "StopIteration"
	case Error:
		return "Error"
	case NeedMoreData:
		return "NeedMoreData"
	case Buffered:
		return "Buffered"
	default:
		return fmt.Sprintf("FilterStatus(%d)", s)
	}
}

// IsTerminal returns true if the status indicates chain termination.
func (s FilterStatus) IsTerminal() bool {
	return s == StopIteration || s == Error
}

// IsSuccess returns true if the status indicates successful processing.
func (s FilterStatus) IsSuccess() bool {
	return s == Continue || s == StopIteration || s == Buffered
}

// FilterPosition indicates where a filter should be placed in a chain.
// It determines the relative position when adding filters dynamically.
type FilterPosition int

const (
	// First indicates the filter should be placed at the beginning of the chain.
	First FilterPosition = iota

	// Last indicates the filter should be placed at the end of the chain.
	Last

	// Before indicates the filter should be placed before a specific filter.
	// Requires a reference filter name or ID.
	Before

	// After indicates the filter should be placed after a specific filter.
	// Requires a reference filter name or ID.
	After
)

// String returns a human-readable string representation of the FilterPosition.
func (p FilterPosition) String() string {
	switch p {
	case First:
		return "First"
	case Last:
		return "Last"
	case Before:
		return "Before"
	case After:
		return "After"
	default:
		return fmt.Sprintf("FilterPosition(%d)", p)
	}
}

// IsValid validates that the position is within the valid range.
func (p FilterPosition) IsValid() bool {
	return p >= First && p <= After
}

// RequiresReference returns true if the position requires a reference filter.
func (p FilterPosition) RequiresReference() bool {
	return p == Before || p == After
}

// FilterError represents specific error codes for filter operations.
// These codes provide detailed information about filter failures.
type FilterError int

const (
	// InvalidConfiguration indicates the filter configuration is invalid.
	InvalidConfiguration FilterError = 1001

	// FilterNotFound indicates the specified filter was not found in the chain.
	FilterNotFound FilterError = 1002

	// FilterAlreadyExists indicates a filter with the same name already exists.
	FilterAlreadyExists FilterError = 1003

	// InitializationFailed indicates the filter failed to initialize.
	InitializationFailed FilterError = 1004

	// ProcessingFailed indicates the filter failed during data processing.
	ProcessingFailed FilterError = 1005

	// ChainError indicates an error in the filter chain execution.
	ChainError FilterError = 1006

	// BufferOverflow indicates the buffer size limit was exceeded.
	BufferOverflow FilterError = 1007

	// Timeout indicates the operation exceeded the time limit.
	Timeout FilterError = 1010

	// ResourceExhausted indicates system resources were exhausted.
	ResourceExhausted FilterError = 1011

	// TooManyRequests indicates rate limiting was triggered.
	TooManyRequests FilterError = 1018

	// AuthenticationFailed indicates authentication failed.
	AuthenticationFailed FilterError = 1019

	// ServiceUnavailable indicates the service is temporarily unavailable.
	ServiceUnavailable FilterError = 1021
)

// Error implements the error interface for FilterError.
func (e FilterError) Error() string {
	switch e {
	case InvalidConfiguration:
		return "invalid filter configuration"
	case FilterNotFound:
		return "filter not found"
	case FilterAlreadyExists:
		return "filter already exists"
	case InitializationFailed:
		return "filter initialization failed"
	case ProcessingFailed:
		return "filter processing failed"
	case ChainError:
		return "filter chain error"
	case BufferOverflow:
		return "buffer overflow"
	case Timeout:
		return "operation timeout"
	case ResourceExhausted:
		return "resource exhausted"
	case TooManyRequests:
		return "too many requests"
	case AuthenticationFailed:
		return "authentication failed"
	case ServiceUnavailable:
		return "service unavailable"
	default:
		return fmt.Sprintf("filter error: %d", e)
	}
}

// String returns a human-readable string representation of the FilterError.
func (e FilterError) String() string {
	return e.Error()
}

// Code returns the numeric error code.
func (e FilterError) Code() int {
	return int(e)
}

// IsRetryable returns true if the error is potentially retryable.
func (e FilterError) IsRetryable() bool {
	switch e {
	case Timeout, ResourceExhausted, TooManyRequests, ServiceUnavailable:
		return true
	default:
		return false
	}
}

// FilterLayer represents the OSI layer at which a filter operates.
// This helps organize filters by their processing level.
type FilterLayer int

const (
	// Transport represents OSI Layer 4 (Transport Layer).
	// Handles TCP, UDP, and other transport protocols.
	Transport FilterLayer = 4

	// Session represents OSI Layer 5 (Session Layer).
	// Manages sessions and connections between applications.
	Session FilterLayer = 5

	// Presentation represents OSI Layer 6 (Presentation Layer).
	// Handles data encoding, encryption, and compression.
	Presentation FilterLayer = 6

	// Application represents OSI Layer 7 (Application Layer).
	// Processes application-specific protocols like HTTP, gRPC.
	Application FilterLayer = 7

	// Custom represents a custom layer outside the OSI model.
	// Used for filters that don't fit standard layer classifications.
	Custom FilterLayer = 99
)

// String returns a human-readable string representation of the FilterLayer.
func (l FilterLayer) String() string {
	switch l {
	case Transport:
		return "Transport (L4)"
	case Session:
		return "Session (L5)"
	case Presentation:
		return "Presentation (L6)"
	case Application:
		return "Application (L7)"
	case Custom:
		return "Custom"
	default:
		return fmt.Sprintf("FilterLayer(%d)", l)
	}
}

// IsValid validates that the layer is a recognized value.
func (l FilterLayer) IsValid() bool {
	return l == Transport || l == Session || l == Presentation || l == Application || l == Custom
}

// OSILayer returns the OSI model layer number (4-7) or 0 for custom.
func (l FilterLayer) OSILayer() int {
	if l >= Transport && l <= Application {
		return int(l)
	}
	return 0
}

// FilterConfig contains configuration settings for a filter.
// It provides all necessary parameters to initialize and operate a filter.
type FilterConfig struct {
	// Name is the unique identifier for the filter instance.
	Name string `json:"name"`

	// Type specifies the filter type (e.g., "http", "auth", "log").
	Type string `json:"type"`

	// Settings contains filter-specific configuration as key-value pairs.
	Settings map[string]interface{} `json:"settings,omitempty"`

	// Layer indicates the OSI layer at which the filter operates.
	Layer FilterLayer `json:"layer"`

	// Enabled determines if the filter is active in the chain.
	Enabled bool `json:"enabled"`

	// Priority determines the filter's execution order (lower = higher priority).
	Priority int `json:"priority"`

	// TimeoutMs specifies the maximum processing time in milliseconds.
	TimeoutMs int `json:"timeout_ms"`

	// BypassOnError allows the chain to continue if this filter fails.
	BypassOnError bool `json:"bypass_on_error"`

	// MaxBufferSize sets the maximum buffer size in bytes.
	MaxBufferSize int `json:"max_buffer_size"`

	// EnableStatistics enables performance metrics collection.
	EnableStatistics bool `json:"enable_statistics"`
}

// Validate checks if the FilterConfig contains valid values.
// It returns a slice of errors for all validation failures found.
func (c *FilterConfig) Validate() []error {
	var errors []error

	// Check Name is not empty
	if c.Name == "" {
		errors = append(errors, fmt.Errorf("filter name cannot be empty"))
	}

	// Check Type is not empty
	if c.Type == "" {
		errors = append(errors, fmt.Errorf("filter type cannot be empty"))
	}

	// Check MaxBufferSize is positive if set
	if c.MaxBufferSize < 0 {
		errors = append(errors, fmt.Errorf("max buffer size cannot be negative: %d", c.MaxBufferSize))
	}
	if c.MaxBufferSize == 0 {
		// Set a default if not specified
		c.MaxBufferSize = 1024 * 1024 // 1MB default
	}

	// Check TimeoutMs is non-negative
	if c.TimeoutMs < 0 {
		errors = append(errors, fmt.Errorf("timeout cannot be negative: %d ms", c.TimeoutMs))
	}

	// Check Priority is within reasonable range (0-1000)
	if c.Priority < 0 || c.Priority > 1000 {
		errors = append(errors, fmt.Errorf("priority must be between 0 and 1000, got: %d", c.Priority))
	}

	// Validate Layer if specified
	if c.Layer != 0 && !c.Layer.IsValid() {
		errors = append(errors, fmt.Errorf("invalid filter layer: %d", c.Layer))
	}

	return errors
}

// FilterStatistics tracks performance metrics for a filter.
// All fields should be accessed atomically in concurrent environments.
type FilterStatistics struct {
	// BytesProcessed is the total number of bytes processed by the filter.
	BytesProcessed uint64 `json:"bytes_processed"`

	// PacketsProcessed is the total number of packets/messages processed.
	PacketsProcessed uint64 `json:"packets_processed"`

	// ProcessCount is the total number of times the filter has been invoked.
	ProcessCount uint64 `json:"process_count"`

	// ErrorCount is the total number of errors encountered.
	ErrorCount uint64 `json:"error_count"`

	// ProcessingTimeUs is the total processing time in microseconds.
	ProcessingTimeUs uint64 `json:"processing_time_us"`

	// AverageProcessingTimeUs is the average processing time per invocation.
	AverageProcessingTimeUs float64 `json:"average_processing_time_us"`

	// MaxProcessingTimeUs is the maximum processing time recorded.
	MaxProcessingTimeUs uint64 `json:"max_processing_time_us"`

	// MinProcessingTimeUs is the minimum processing time recorded.
	MinProcessingTimeUs uint64 `json:"min_processing_time_us"`

	// CurrentBufferUsage is the current buffer memory usage in bytes.
	CurrentBufferUsage uint64 `json:"current_buffer_usage"`

	// PeakBufferUsage is the peak buffer memory usage in bytes.
	PeakBufferUsage uint64 `json:"peak_buffer_usage"`

	// ThroughputBps is the current throughput in bytes per second.
	ThroughputBps float64 `json:"throughput_bps"`
}

// String returns a human-readable summary of the filter statistics.
func (s *FilterStatistics) String() string {
	return fmt.Sprintf(
		"FilterStats{Processed: %d bytes/%d packets, Invocations: %d, Errors: %d, "+
			"AvgTime: %.2fμs, MaxTime: %dμs, MinTime: %dμs, "+
			"BufferUsage: %d/%d bytes, Throughput: %.2f B/s}",
		s.BytesProcessed, s.PacketsProcessed, s.ProcessCount, s.ErrorCount,
		s.AverageProcessingTimeUs, s.MaxProcessingTimeUs, s.MinProcessingTimeUs,
		s.CurrentBufferUsage, s.PeakBufferUsage, s.ThroughputBps,
	)
}

// FilterResult represents the result of a filter's processing operation.
// It contains the processing status, output data, and metadata.
type FilterResult struct {
	// Status indicates the result of the filter processing.
	Status FilterStatus `json:"status"`

	// Data contains the processed output data.
	Data []byte `json:"data,omitempty"`

	// Error contains any error that occurred during processing.
	Error error `json:"error,omitempty"`

	// Metadata contains additional information about the processing.
	Metadata map[string]interface{} `json:"metadata,omitempty"`

	// StartTime marks when processing began.
	StartTime time.Time `json:"start_time"`

	// EndTime marks when processing completed.
	EndTime time.Time `json:"end_time"`

	// StopChain indicates if the filter chain should stop after this filter.
	StopChain bool `json:"stop_chain"`

	// SkipCount indicates how many filters to skip in the chain.
	SkipCount int `json:"skip_count"`
}

// Duration calculates the processing time for this result.
func (r *FilterResult) Duration() time.Duration {
	if r.EndTime.IsZero() || r.StartTime.IsZero() {
		return 0
	}
	return r.EndTime.Sub(r.StartTime)
}