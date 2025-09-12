// Package filters provides built-in filters for the MCP Filter SDK.
package filters

import (
	"context"
	"sync"
	"sync/atomic"
	"time"

	"github.com/GopherSecurity/gopher-mcp/src/types"
)

// MetricsCollector defines the interface for metrics collection.
type MetricsCollector interface {
	RecordLatency(name string, duration time.Duration)
	IncrementCounter(name string, delta int64)
	SetGauge(name string, value float64)
	RecordHistogram(name string, value float64)
}

// MetricsConfig configures metrics collection behavior.
type MetricsConfig struct {
	Enabled             bool
	ExportInterval      time.Duration
	IncludeHistograms   bool
	IncludePercentiles  bool
	MetricPrefix        string
	Tags                map[string]string
}

// MetricsFilter collects metrics for filter processing.
type MetricsFilter struct {
	*FilterBase
	
	// Metrics collector implementation
	collector MetricsCollector
	
	// Configuration
	config MetricsConfig
	
	// Statistics storage
	stats map[string]atomic.Value
	
	// Mutex for map access
	mu sync.RWMutex
}

// NewMetricsFilter creates a new metrics collection filter.
func NewMetricsFilter(config MetricsConfig, collector MetricsCollector) *MetricsFilter {
	return &MetricsFilter{
		FilterBase: NewFilterBase("metrics", "monitoring"),
		collector:  collector,
		config:     config,
		stats:      make(map[string]atomic.Value),
	}
}