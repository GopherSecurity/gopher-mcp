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
	
	// Track percentiles
	f.trackLatencyPercentiles(metricName, duration)
	
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

// ThroughputTracker tracks throughput using sliding window.
type ThroughputTracker struct {
	requestsPerSec float64
	bytesPerSec    float64
	peakRPS        float64
	peakBPS        float64
	
	window      []throughputSample
	windowSize  time.Duration
	lastUpdate  time.Time
	mu          sync.RWMutex
}

type throughputSample struct {
	timestamp time.Time
	requests  int64
	bytes     int64
}

// NewThroughputTracker creates a new throughput tracker.
func NewThroughputTracker(windowSize time.Duration) *ThroughputTracker {
	return &ThroughputTracker{
		window:     make([]throughputSample, 0, 100),
		windowSize: windowSize,
		lastUpdate: time.Now(),
	}
}

// Add adds a sample to the tracker.
func (tt *ThroughputTracker) Add(requests, bytes int64) {
	tt.mu.Lock()
	defer tt.mu.Unlock()
	
	now := time.Now()
	tt.window = append(tt.window, throughputSample{
		timestamp: now,
		requests:  requests,
		bytes:     bytes,
	})
	
	// Clean old samples
	cutoff := now.Add(-tt.windowSize)
	newWindow := make([]throughputSample, 0, len(tt.window))
	for _, s := range tt.window {
		if s.timestamp.After(cutoff) {
			newWindow = append(newWindow, s)
		}
	}
	tt.window = newWindow
	
	// Calculate rates
	if len(tt.window) > 1 {
		duration := tt.window[len(tt.window)-1].timestamp.Sub(tt.window[0].timestamp).Seconds()
		if duration > 0 {
			var totalRequests, totalBytes int64
			for _, s := range tt.window {
				totalRequests += s.requests
				totalBytes += s.bytes
			}
			
			tt.requestsPerSec = float64(totalRequests) / duration
			tt.bytesPerSec = float64(totalBytes) / duration
			
			// Update peaks
			if tt.requestsPerSec > tt.peakRPS {
				tt.peakRPS = tt.requestsPerSec
			}
			if tt.bytesPerSec > tt.peakBPS {
				tt.peakBPS = tt.bytesPerSec
			}
		}
	}
}

// updateThroughput updates throughput metrics with sliding window.
func (f *MetricsFilter) updateThroughput(name string, bytes int) {
	key := name + ".throughput"
	
	// Get or create throughput tracker
	var tracker *ThroughputTracker
	if v, ok := f.stats[key]; ok {
		tracker = v.Load().(*ThroughputTracker)
	} else {
		tracker = NewThroughputTracker(10 * time.Second) // 10 second window
		var v atomic.Value
		v.Store(tracker)
		f.mu.Lock()
		f.stats[key] = v
		f.mu.Unlock()
	}
	
	// Add sample
	tracker.Add(1, int64(bytes))
	
	// Export metrics
	f.collector.SetGauge(name+".rps", tracker.requestsPerSec)
	f.collector.SetGauge(name+".bps", tracker.bytesPerSec)
	f.collector.SetGauge(name+".peak_rps", tracker.peakRPS)
	f.collector.SetGauge(name+".peak_bps", tracker.peakBPS)
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

// PercentileTracker tracks latency percentiles.
type PercentileTracker struct {
	values []float64
	mu     sync.RWMutex
	sorted bool
}

// NewPercentileTracker creates a new percentile tracker.
func NewPercentileTracker() *PercentileTracker {
	return &PercentileTracker{
		values: make([]float64, 0, 1000),
	}
}

// Add adds a value to the tracker.
func (pt *PercentileTracker) Add(value float64) {
	pt.mu.Lock()
	defer pt.mu.Unlock()
	pt.values = append(pt.values, value)
	pt.sorted = false
}

// GetPercentile calculates the given percentile (0-100).
func (pt *PercentileTracker) GetPercentile(p float64) float64 {
	pt.mu.Lock()
	defer pt.mu.Unlock()
	
	if len(pt.values) == 0 {
		return 0
	}
	
	if !pt.sorted {
		// Sort values for percentile calculation
		for i := 0; i < len(pt.values); i++ {
			for j := i + 1; j < len(pt.values); j++ {
				if pt.values[i] > pt.values[j] {
					pt.values[i], pt.values[j] = pt.values[j], pt.values[i]
				}
			}
		}
		pt.sorted = true
	}
	
	index := int(float64(len(pt.values)-1) * p / 100.0)
	return pt.values[index]
}

// trackLatencyPercentiles tracks P50, P90, P95, P99.
func (f *MetricsFilter) trackLatencyPercentiles(name string, duration time.Duration) {
	if !f.config.IncludePercentiles {
		return
	}
	
	key := name + ".percentiles"
	
	// Get or create percentile tracker
	var tracker *PercentileTracker
	if v, ok := f.stats[key]; ok {
		tracker = v.Load().(*PercentileTracker)
	} else {
		tracker = NewPercentileTracker()
		var v atomic.Value
		v.Store(tracker)
		f.mu.Lock()
		f.stats[key] = v
		f.mu.Unlock()
	}
	
	// Add value
	tracker.Add(float64(duration.Microseconds()))
	
	// Export percentiles
	f.collector.SetGauge(name+".p50", tracker.GetPercentile(50))
	f.collector.SetGauge(name+".p90", tracker.GetPercentile(90))
	f.collector.SetGauge(name+".p95", tracker.GetPercentile(95))
	f.collector.SetGauge(name+".p99", tracker.GetPercentile(99))
}