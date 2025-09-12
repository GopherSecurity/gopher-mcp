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

// Deadline returns the deadline from the embedded context.
// Implements context.Context interface.
func (pc *ProcessingContext) Deadline() (deadline time.Time, ok bool) {
	return pc.Context.Deadline()
}

// Done returns the done channel from the embedded context.
// Implements context.Context interface.
func (pc *ProcessingContext) Done() <-chan struct{} {
	return pc.Context.Done()
}

// Err returns any error from the embedded context.
// Implements context.Context interface.
func (pc *ProcessingContext) Err() error {
	return pc.Context.Err()
}

// Value first checks the embedded context, then the properties map.
// This allows both standard context values and custom properties.
// Implements context.Context interface.
func (pc *ProcessingContext) Value(key interface{}) interface{} {
	// First check the embedded context
	if val := pc.Context.Value(key); val != nil {
		return val
	}

	// Then check properties map if key is a string
	if strKey, ok := key.(string); ok {
		if val, ok := pc.properties.Load(strKey); ok {
			return val
		}
	}

	return nil
}

// SetProperty stores a key-value pair in the properties map.
// The key must be non-empty. The value can be nil.
// This provides thread-safe property storage without external locking.
//
// Parameters:
//   - key: The property key (must be non-empty)
//   - value: The property value (can be nil)
func (pc *ProcessingContext) SetProperty(key string, value interface{}) {
	if key == "" {
		return
	}
	pc.properties.Store(key, value)
}

// GetProperty retrieves a value from the properties map.
// Returns the value and true if found, nil and false otherwise.
//
// Parameters:
//   - key: The property key to retrieve
//
// Returns:
//   - interface{}: The property value if found
//   - bool: True if the property exists
func (pc *ProcessingContext) GetProperty(key string) (interface{}, bool) {
	return pc.properties.Load(key)
}