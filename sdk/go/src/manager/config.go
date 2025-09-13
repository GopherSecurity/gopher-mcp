// Package manager provides filter and chain management for the MCP Filter SDK.
package manager

import "time"

// FilterManagerConfig configures the filter manager behavior.
type FilterManagerConfig struct {
	// Metrics configuration
	EnableMetrics   bool
	MetricsInterval time.Duration

	// Capacity limits
	MaxFilters int
	MaxChains  int

	// Timeouts
	DefaultTimeout time.Duration

	// Tracing
	EnableTracing bool

	// Advanced options
	EnableAutoRecovery  bool
	RecoveryAttempts    int
	HealthCheckInterval time.Duration

	// Event configuration
	EventBufferSize    int
	EventFlushInterval time.Duration
}

// DefaultFilterManagerConfig returns default configuration.
func DefaultFilterManagerConfig() FilterManagerConfig {
	return FilterManagerConfig{
		EnableMetrics:       true,
		MetricsInterval:     10 * time.Second,
		MaxFilters:          1000,
		MaxChains:           100,
		DefaultTimeout:      30 * time.Second,
		EnableTracing:       false,
		EnableAutoRecovery:  true,
		RecoveryAttempts:    3,
		HealthCheckInterval: 30 * time.Second,
		EventBufferSize:     1000,
		EventFlushInterval:  time.Second,
	}
}
