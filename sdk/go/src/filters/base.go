// Package filters provides built-in filters for the MCP Filter SDK.
package filters

import (
	"sync"
	"sync/atomic"
	"time"

	"github.com/GopherSecurity/gopher-mcp/src/types"
)

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
	fb.mu.RLock()
	defer fb.mu.RUnlock()
	return fb.name
}

// Type returns the filter's category type.
// Used for metrics collection and logging.
func (fb *FilterBase) Type() string {
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