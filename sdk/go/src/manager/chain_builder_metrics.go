// Package manager provides filter and chain management for the MCP Filter SDK.
package manager

import "time"

// MetricsCollector interface for metrics collection.
type MetricsCollector interface {
	Collect(chain string, metrics map[string]interface{})
}

// WithMetrics enables metrics collection.
func (cb *ChainBuilder) WithMetrics(collector MetricsCollector) *ChainBuilder {
	cb.config.EnableMetrics = true
	// Store collector reference
	// cb.metricsCollector = collector
	
	// Configure metrics interval
	if cb.config.MetricsInterval == 0 {
		cb.config.MetricsInterval = 10 * time.Second
	}
	
	return cb
}

// MetricsInterval sets the metrics collection interval.
type MetricsInterval time.Duration