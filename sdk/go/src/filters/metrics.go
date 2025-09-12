// Package filters provides built-in filters for the MCP Filter SDK.
package filters

import (
	"context"
	"sync"
	"sync/atomic"
	"time"

	"github.com/GopherSecurity/gopher-mcp/src/types"
)

// MetricsCollector defines the interface for metrics collection backends.
type MetricsCollector interface {
	// RecordLatency records a latency measurement
	RecordLatency(name string, duration time.Duration)
	
	// IncrementCounter increments a counter metric
	IncrementCounter(name string, delta int64)
	
	// SetGauge sets a gauge metric to a specific value
	SetGauge(name string, value float64)
	
	// RecordHistogram records a value in a histogram
	RecordHistogram(name string, value float64)
	
	// Flush forces export of buffered metrics
	Flush() error
	
	// Close shuts down the collector
	Close() error
}

// MetricsConfig configures metrics collection behavior.
type MetricsConfig struct {
	// Enabled determines if metrics collection is active
	Enabled bool
	
	// ExportInterval defines how often metrics are exported
	ExportInterval time.Duration
	
	// IncludeHistograms enables histogram metrics (more memory)
	IncludeHistograms bool
	
	// IncludePercentiles enables percentile calculations (P50, P90, P95, P99)
	IncludePercentiles bool
	
	// MetricPrefix is prepended to all metric names
	MetricPrefix string
	
	// Tags are added to all metrics for grouping/filtering
	Tags map[string]string
	
	// BufferSize for metric events (0 = unbuffered)
	BufferSize int
	
	// FlushOnClose ensures all metrics are exported on shutdown
	FlushOnClose bool
}

// DefaultMetricsConfig returns a sensible default configuration.
func DefaultMetricsConfig() MetricsConfig {
	return MetricsConfig{
		Enabled:            true,
		ExportInterval:     10 * time.Second,
		IncludeHistograms:  true,
		IncludePercentiles: true,
		MetricPrefix:       "filter",
		Tags:               make(map[string]string),
		BufferSize:         1000,
		FlushOnClose:       true,
	}
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
	f := &MetricsFilter{
		FilterBase: NewFilterBase("metrics", "monitoring"),
		collector:  collector,
		config:     config,
		stats:      make(map[string]atomic.Value),
	}
	
	// Start export timer if configured
	if config.Enabled && config.ExportInterval > 0 {
		go f.exportLoop()
	}
	
	return f
}

// Process implements the Filter interface with metrics collection.
func (f *MetricsFilter) Process(ctx context.Context, data []byte) (*types.FilterResult, error) {
	if !f.config.Enabled {
		// Pass through without metrics if disabled
		return types.ContinueWith(data), nil
	}
	
	// Record start time
	startTime := time.Now()
	
	// Get metric name from context or use default
	metricName := f.getMetricName(ctx)
	
	// Increment request counter
	f.collector.IncrementCounter(metricName+".requests", 1)
	
	// Process the actual data (would call next filter in real implementation)
	result, err := f.processNext(ctx, data)
	
	// Calculate duration
	duration := time.Since(startTime)
	
	// Record latency
	f.collector.RecordLatency(metricName+".latency", duration)
	
	// Record in histogram if enabled
	if f.config.IncludeHistograms {
		f.collector.RecordHistogram(metricName+".duration_ms", float64(duration.Milliseconds()))
	}
	
	// Track success/error rates
	if err != nil || (result != nil && result.Status == types.Error) {
		f.collector.IncrementCounter(metricName+".errors", 1)
		f.recordErrorRate(metricName, true)
	} else {
		f.collector.IncrementCounter(metricName+".success", 1)
		f.recordErrorRate(metricName, false)
	}
	
	// Track data size
	f.collector.RecordHistogram(metricName+".request_size", float64(len(data)))
	if result != nil && result.Data != nil {
		f.collector.RecordHistogram(metricName+".response_size", float64(len(result.Data)))
	}
	
	// Update throughput metrics
	f.updateThroughput(metricName, len(data))
	
	return result, err
}

// processNext simulates calling the next filter in the chain.
func (f *MetricsFilter) processNext(ctx context.Context, data []byte) (*types.FilterResult, error) {
	// In real implementation, this would delegate to the next filter
	return types.ContinueWith(data), nil
}

// getMetricName extracts metric name from context or returns default.
func (f *MetricsFilter) getMetricName(ctx context.Context) string {
	if name, ok := ctx.Value("metric_name").(string); ok {
		return f.config.MetricPrefix + "." + name
	}
	return f.config.MetricPrefix + ".default"
}

// recordErrorRate tracks error rate over time.
func (f *MetricsFilter) recordErrorRate(name string, isError bool) {
	key := name + ".error_rate"
	
	// Get or create error rate tracker
	var tracker errorRateTracker
	if v, ok := f.stats[key]; ok {
		tracker = v.Load().(errorRateTracker)
	} else {
		tracker = errorRateTracker{}
	}
	
	// Update tracker
	tracker.total++
	if isError {
		tracker.errors++
	}
	
	// Calculate rate
	rate := float64(0)
	if tracker.total > 0 {
		rate = float64(tracker.errors) / float64(tracker.total) * 100.0
	}
	
	// Store updated tracker
	var v atomic.Value
	v.Store(tracker)
	f.mu.Lock()
	f.stats[key] = v
	f.mu.Unlock()
	
	// Record as gauge
	f.collector.SetGauge(key, rate)
}

// updateThroughput updates throughput metrics.
func (f *MetricsFilter) updateThroughput(name string, bytes int) {
	// Implementation would track bytes/sec and requests/sec
	f.collector.IncrementCounter(name+".bytes", int64(bytes))
}

// exportLoop periodically exports metrics.
func (f *MetricsFilter) exportLoop() {
	ticker := time.NewTicker(f.config.ExportInterval)
	defer ticker.Stop()
	
	for range ticker.C {
		if err := f.collector.Flush(); err != nil {
			// Log error (would use actual logger)
			_ = err
		}
	}
}

// errorRateTracker tracks error rate.
type errorRateTracker struct {
	total  uint64
	errors uint64
}