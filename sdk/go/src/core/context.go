// Package core provides the core interfaces and types for the MCP Filter SDK.
package core

import (
	"context"
	"sync"
	"time"
)

// ProcessingContext extends context.Context with filter processing specific functionality.
// It provides thread-safe property storage, metrics collection, and request correlation.
//
// ProcessingContext features:
//   - Embedded context.Context for standard Go context operations
//   - Thread-safe property storage using sync.Map
//   - Correlation ID for request tracking
//   - Metrics collection for performance monitoring
//   - Processing time tracking
//
// Example usage:
//
//	ctx := &ProcessingContext{
//	    Context: context.Background(),
//	    correlationID: "req-123",
//	}
//	ctx.SetProperty("user_id", "user-456")
//	result := chain.Process(ctx, data)
type ProcessingContext struct {
	// Embed context.Context for standard context operations
	context.Context

	// properties stores key-value pairs in a thread-safe manner
	// No external locking required for access
	properties sync.Map

	// correlationID uniquely identifies this processing request
	// Used for tracing and debugging across filters
	correlationID string

	// metrics collects performance and business metrics
	metrics *MetricsCollector

	// startTime tracks when processing began
	startTime time.Time

	// mu protects non-concurrent fields like correlationID and startTime
	// Not needed for properties (sync.Map) or metrics (has own locking)
	mu sync.RWMutex
}

// MetricsCollector handles thread-safe metric collection.
type MetricsCollector struct {
	metrics map[string]float64
	mu      sync.RWMutex
}

// NewMetricsCollector creates a new metrics collector.
func NewMetricsCollector() *MetricsCollector {
	return &MetricsCollector{
		metrics: make(map[string]float64),
	}
}

// Record stores a metric value.
func (mc *MetricsCollector) Record(name string, value float64) {
	mc.mu.Lock()
	defer mc.mu.Unlock()
	mc.metrics[name] = value
}

// Get retrieves a metric value.
func (mc *MetricsCollector) Get(name string) (float64, bool) {
	mc.mu.RLock()
	defer mc.mu.RUnlock()
	val, ok := mc.metrics[name]
	return val, ok
}

// All returns a copy of all metrics.
func (mc *MetricsCollector) All() map[string]float64 {
	mc.mu.RLock()
	defer mc.mu.RUnlock()
	
	result := make(map[string]float64, len(mc.metrics))
	for k, v := range mc.metrics {
		result[k] = v
	}
	return result
}

// NewProcessingContext creates a new processing context with the given parent context.
func NewProcessingContext(parent context.Context) *ProcessingContext {
	return &ProcessingContext{
		Context:   parent,
		metrics:   NewMetricsCollector(),
		startTime: time.Now(),
	}
}

// WithCorrelationID creates a new processing context with the specified correlation ID.
func WithCorrelationID(parent context.Context, correlationID string) *ProcessingContext {
	ctx := NewProcessingContext(parent)
	ctx.correlationID = correlationID
	return ctx
}