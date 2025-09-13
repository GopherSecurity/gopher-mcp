// Package filters provides built-in filters for the MCP Filter SDK.
package filters

import (
	"errors"
	"sync"
	"sync/atomic"
	"time"

	"github.com/GopherSecurity/gopher-mcp/src/types"
)

// ErrFilterDisposed is returned when operations are attempted on a disposed filter.
var ErrFilterDisposed = errors.New("filter has been disposed")

// FilterBase provides a base implementation for filters.
// It's designed to be embedded in concrete filter implementations.
type FilterBase struct {
	// name is the unique identifier for this filter
	name string

	// filterType categorizes the filter (e.g., "security", "transform")
	filterType string

	// stats tracks filter performance metrics
	stats types.FilterStatistics

	// disposed indicates if the filter has been closed (0=active, 1=disposed)
	disposed int32

	// mu protects concurrent access to filter state
	mu sync.RWMutex

	// config stores the filter configuration
	config types.FilterConfig
}

// NewFilterBase creates a new FilterBase instance.
func NewFilterBase(name, filterType string) *FilterBase {
	return &FilterBase{
		name:       name,
		filterType: filterType,
		stats:      types.FilterStatistics{},
		disposed:   0,
	}
}

// Name returns the filter's unique name.
// Thread-safe with read lock protection.
func (fb *FilterBase) Name() string {
	if err := fb.ThrowIfDisposed(); err != nil {
		return ""
	}
	fb.mu.RLock()
	defer fb.mu.RUnlock()
	return fb.name
}

// Type returns the filter's category type.
// Used for metrics collection and logging.
func (fb *FilterBase) Type() string {
	if err := fb.ThrowIfDisposed(); err != nil {
		return ""
	}
	fb.mu.RLock()
	defer fb.mu.RUnlock()
	return fb.filterType
}

// updateStats atomically updates filter statistics.
// Tracks processing metrics including min/max/average times.
func (fb *FilterBase) updateStats(processed int64, errors int64, duration time.Duration) {
	fb.mu.Lock()
	defer fb.mu.Unlock()

	// Update counters
	if processed > 0 {
		fb.stats.BytesProcessed += uint64(processed)
		fb.stats.PacketsProcessed++
	}

	if errors > 0 {
		fb.stats.ErrorCount += uint64(errors)
	}

	fb.stats.ProcessCount++

	// Update timing statistics
	durationUs := uint64(duration.Microseconds())
	fb.stats.ProcessingTimeUs += durationUs

	// Update min processing time
	if fb.stats.MinProcessingTimeUs == 0 || durationUs < fb.stats.MinProcessingTimeUs {
		fb.stats.MinProcessingTimeUs = durationUs
	}

	// Update max processing time
	if durationUs > fb.stats.MaxProcessingTimeUs {
		fb.stats.MaxProcessingTimeUs = durationUs
	}

	// Calculate average processing time
	if fb.stats.ProcessCount > 0 {
		fb.stats.AverageProcessingTimeUs = float64(fb.stats.ProcessingTimeUs) / float64(fb.stats.ProcessCount)
	}

	// Calculate throughput
	if fb.stats.ProcessingTimeUs > 0 {
		fb.stats.ThroughputBps = float64(fb.stats.BytesProcessed) * 1000000.0 / float64(fb.stats.ProcessingTimeUs)
	}
}

// Initialize sets up the filter with the provided configuration.
// Returns error if already initialized or disposed.
func (fb *FilterBase) Initialize(config types.FilterConfig) error {
	// Check if disposed
	if err := fb.ThrowIfDisposed(); err != nil {
		return err
	}

	fb.mu.Lock()
	defer fb.mu.Unlock()

	// Check if already initialized
	if fb.config.Name != "" {
		return types.FilterError(types.FilterAlreadyExists)
	}

	// Validate configuration
	if errs := config.Validate(); len(errs) > 0 {
		return errs[0]
	}

	// Store configuration
	fb.config = config

	// Update name if provided
	if config.Name != "" {
		fb.name = config.Name
	}

	// Update type if provided
	if config.Type != "" {
		fb.filterType = config.Type
	}

	return nil
}

// Close performs cleanup and sets the disposed flag.
// Idempotent - safe to call multiple times.
func (fb *FilterBase) Close() error {
	// Atomically set disposed flag
	if !atomic.CompareAndSwapInt32(&fb.disposed, 0, 1) {
		// Already disposed
		return nil
	}

	fb.mu.Lock()
	defer fb.mu.Unlock()

	// Clear resources
	fb.stats = types.FilterStatistics{}
	fb.config = types.FilterConfig{}

	return nil
}

// GetStats returns the current filter statistics.
// Returns a copy with calculated derived metrics like average processing time.
func (fb *FilterBase) GetStats() types.FilterStatistics {
	if err := fb.ThrowIfDisposed(); err != nil {
		return types.FilterStatistics{}
	}
	fb.mu.RLock()
	defer fb.mu.RUnlock()

	// Create a copy of statistics
	statsCopy := fb.stats

	// Calculate derived metrics
	if statsCopy.ProcessCount > 0 {
		// Recalculate average processing time
		statsCopy.AverageProcessingTimeUs = float64(statsCopy.ProcessingTimeUs) / float64(statsCopy.ProcessCount)

		// Calculate throughput in bytes per second
		if statsCopy.ProcessingTimeUs > 0 {
			statsCopy.ThroughputBps = float64(statsCopy.BytesProcessed) * 1000000.0 / float64(statsCopy.ProcessingTimeUs)
		}

		// Calculate error rate as percentage
		statsCopy.ErrorRate = float64(statsCopy.ErrorCount) / float64(statsCopy.ProcessCount) * 100.0
	}

	return statsCopy
}

// IsDisposed checks if the filter has been disposed.
func (fb *FilterBase) IsDisposed() bool {
	return atomic.LoadInt32(&fb.disposed) != 0
}

// ThrowIfDisposed checks if filter is disposed and returns error if true.
// This should be called at the start of all public operations.
func (fb *FilterBase) ThrowIfDisposed() error {
	if atomic.LoadInt32(&fb.disposed) != 0 {
		return ErrFilterDisposed
	}
	return nil
}
