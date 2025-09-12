// Package core provides the core interfaces and types for the MCP Filter SDK.
package core

import (
	"sync"
	"sync/atomic"

	"github.com/GopherSecurity/gopher-mcp/src/types"
)

// FilterBase provides a base implementation of the Filter interface.
// It can be embedded in concrete filter implementations to provide
// common functionality and reduce boilerplate code.
//
// FilterBase handles:
//   - Name and type management
//   - Configuration storage
//   - Statistics collection with thread-safety
//   - Disposal state tracking
//
// Example usage:
//
//	type MyFilter struct {
//	    core.FilterBase
//	    // Additional fields specific to this filter
//	}
//
//	func NewMyFilter(name string) *MyFilter {
//	    f := &MyFilter{}
//	    f.name = name
//	    f.filterType = "custom"
//	    return f
//	}
type FilterBase struct {
	// name is the unique identifier for this filter instance.
	name string

	// filterType is the category of this filter.
	filterType string

	// config stores the filter's configuration.
	config types.FilterConfig

	// stats tracks performance metrics for this filter.
	// Protected by statsLock for thread-safe access.
	stats types.FilterStatistics

	// statsLock protects concurrent access to stats.
	statsLock sync.RWMutex

	// disposed indicates if this filter has been closed.
	// Use atomic operations for thread-safe access.
	// 0 = active, 1 = disposed
	disposed int32
}

// NewFilterBase creates a new FilterBase with the given name and type.
// This is a convenience constructor for embedded use.
func NewFilterBase(name, filterType string) FilterBase {
	return FilterBase{
		name:       name,
		filterType: filterType,
		stats:      types.FilterStatistics{},
		disposed:   0,
	}
}

// SetName sets the filter's name.
// This should only be called during initialization.
func (fb *FilterBase) SetName(name string) {
	fb.name = name
}

// SetType sets the filter's type category.
// This should only be called during initialization.
func (fb *FilterBase) SetType(filterType string) {
	fb.filterType = filterType
}

// GetConfig returns a copy of the filter's configuration.
// This is safe to call concurrently.
func (fb *FilterBase) GetConfig() types.FilterConfig {
	return fb.config
}

// Name returns the unique name of this filter instance.
// Implements the Filter interface.
func (fb *FilterBase) Name() string {
	return fb.name
}

// Type returns the category or type of this filter.
// Implements the Filter interface.
func (fb *FilterBase) Type() string {
	return fb.filterType
}

// GetStats returns the current performance statistics for this filter.
// Uses read lock for thread-safe access.
// Implements the Filter interface.
func (fb *FilterBase) GetStats() types.FilterStatistics {
	fb.statsLock.RLock()
	defer fb.statsLock.RUnlock()
	return fb.stats
}

// Initialize sets up the filter with the provided configuration.
// Stores the configuration for later use and validates it.
// Implements the Filter interface.
func (fb *FilterBase) Initialize(config types.FilterConfig) error {
	// Check if already disposed
	if atomic.LoadInt32(&fb.disposed) != 0 {
		return types.FilterError(types.FilterAlreadyExists)
	}

	// Validate the configuration
	if errs := config.Validate(); len(errs) > 0 {
		return errs[0]
	}

	// Store the configuration
	fb.config = config

	// Update name if provided in config
	if config.Name != "" {
		fb.name = config.Name
	}

	// Update type if provided in config
	if config.Type != "" {
		fb.filterType = config.Type
	}

	// Reset statistics
	fb.statsLock.Lock()
	fb.stats = types.FilterStatistics{}
	fb.statsLock.Unlock()

	return nil
}

// Close performs cleanup operations for the filter.
// Sets the disposed flag to prevent further operations.
// Implements the Filter interface.
func (fb *FilterBase) Close() error {
	// Set disposed flag using atomic operation
	if !atomic.CompareAndSwapInt32(&fb.disposed, 0, 1) {
		// Already disposed
		return nil
	}

	// Clear statistics
	fb.statsLock.Lock()
	fb.stats = types.FilterStatistics{}
	fb.statsLock.Unlock()

	return nil
}

// isDisposed checks if the filter has been closed.
// Returns true if the filter is disposed and should not process data.
func (fb *FilterBase) isDisposed() bool {
	return atomic.LoadInt32(&fb.disposed) != 0
}

// checkDisposed returns an error if the filter is disposed.
// This should be called at the start of any operation that requires
// the filter to be active.
func (fb *FilterBase) checkDisposed() error {
	if fb.isDisposed() {
		return types.FilterError(types.ServiceUnavailable)
	}
	return nil
}

// updateStats updates the filter statistics with new processing information.
// This method is thread-safe and can be called concurrently.
//
// Parameters:
//   - bytesProcessed: Number of bytes processed in this operation
//   - processingTimeUs: Time taken for processing in microseconds
//   - isError: Whether this operation resulted in an error
func (fb *FilterBase) updateStats(bytesProcessed uint64, processingTimeUs uint64, isError bool) {
	fb.statsLock.Lock()
	defer fb.statsLock.Unlock()

	// Update counters
	fb.stats.BytesProcessed += bytesProcessed
	fb.stats.ProcessCount++
	
	if isError {
		fb.stats.ErrorCount++
	} else {
		fb.stats.PacketsProcessed++
	}

	// Update timing statistics
	fb.stats.ProcessingTimeUs += processingTimeUs
	
	// Update average processing time
	if fb.stats.ProcessCount > 0 {
		fb.stats.AverageProcessingTimeUs = float64(fb.stats.ProcessingTimeUs) / float64(fb.stats.ProcessCount)
	}

	// Update max processing time
	if processingTimeUs > fb.stats.MaxProcessingTimeUs {
		fb.stats.MaxProcessingTimeUs = processingTimeUs
	}

	// Update min processing time (initialize on first call)
	if fb.stats.MinProcessingTimeUs == 0 || processingTimeUs < fb.stats.MinProcessingTimeUs {
		fb.stats.MinProcessingTimeUs = processingTimeUs
	}

	// Update buffer usage if applicable
	if bytesProcessed > 0 {
		fb.stats.CurrentBufferUsage = bytesProcessed
		if bytesProcessed > fb.stats.PeakBufferUsage {
			fb.stats.PeakBufferUsage = bytesProcessed
		}
	}

	// Calculate throughput (bytes per second)
	if processingTimeUs > 0 {
		fb.stats.ThroughputBps = float64(bytesProcessed) * 1000000.0 / float64(processingTimeUs)
	}
}

// ResetStats clears all statistics for this filter.
// This is useful for benchmarking or after configuration changes.
func (fb *FilterBase) ResetStats() {
	fb.statsLock.Lock()
	defer fb.statsLock.Unlock()
	fb.stats = types.FilterStatistics{}
}