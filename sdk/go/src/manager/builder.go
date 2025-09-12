// Package manager provides filter and chain management for the MCP Filter SDK.
package manager

import (
	"fmt"
	"time"

	"github.com/GopherSecurity/gopher-mcp/src/core"
	"github.com/GopherSecurity/gopher-mcp/src/types"
)

// ChainBuilder provides a fluent interface for constructing filter chains.
type ChainBuilder struct {
	filters    []core.Filter
	config     types.ChainConfig
	validators []Validator
	errors     []error
}

// Validator validates filter chains during construction.
type Validator interface {
	Validate(filters []core.Filter, config types.ChainConfig) error
}

// MetricsCollector collects metrics from filter chains.
type MetricsCollector interface {
	RecordLatency(name string, duration time.Duration)
	IncrementCounter(name string, delta int64)
	SetGauge(name string, value float64)
	RecordHistogram(name string, value float64)
}

// NewChainBuilder creates a new chain builder with default configuration.
func NewChainBuilder(name string) *ChainBuilder {
	return &ChainBuilder{
		filters: make([]core.Filter, 0),
		config: types.ChainConfig{
			Name:         name,
			ExecutionMode: types.Sequential,
			MaxConcurrency: 1,
			BufferSize:   1000,
			ErrorHandling: "fail-fast",
			Timeout:      30 * time.Second,
			EnableMetrics: false,
			EnableTracing: false,
		},
		validators: make([]Validator, 0),
		errors:     make([]error, 0),
	}
}

// Add appends a filter to the chain and returns the builder for chaining.
func (cb *ChainBuilder) Add(filter core.Filter) *ChainBuilder {
	if filter == nil {
		cb.errors = append(cb.errors, fmt.Errorf("filter cannot be nil"))
		return cb
	}
	
	// Check for duplicate filter names
	filterName := filter.Name()
	if filterName == "" {
		cb.errors = append(cb.errors, fmt.Errorf("filter name cannot be empty"))
		return cb
	}
	
	for _, existing := range cb.filters {
		if existing.Name() == filterName {
			cb.errors = append(cb.errors, fmt.Errorf("filter with name '%s' already exists in chain", filterName))
			return cb
		}
	}
	
	cb.filters = append(cb.filters, filter)
	return cb
}

// WithMode sets the execution mode for the chain.
func (cb *ChainBuilder) WithMode(mode types.ExecutionMode) *ChainBuilder {
	cb.config.ExecutionMode = mode
	
	// Validate mode with current filters
	if mode == types.Parallel && len(cb.filters) > 0 {
		// Check if all filters support parallel execution
		for _, filter := range cb.filters {
			// This is a simplified check - in reality you'd need a way to determine
			// if a filter supports parallel execution
			_ = filter // Use the filter variable to avoid unused variable error
		}
	}
	
	return cb
}

// WithTimeout sets the timeout for the entire chain execution.
func (cb *ChainBuilder) WithTimeout(timeout time.Duration) *ChainBuilder {
	if timeout <= 0 {
		cb.errors = append(cb.errors, fmt.Errorf("timeout must be positive, got %v", timeout))
		return cb
	}
	
	cb.config.Timeout = timeout
	return cb
}

// WithMetrics enables metrics collection for the chain.
func (cb *ChainBuilder) WithMetrics(collector MetricsCollector) *ChainBuilder {
	if collector == nil {
		cb.errors = append(cb.errors, fmt.Errorf("metrics collector cannot be nil"))
		return cb
	}
	
	cb.config.EnableMetrics = true
	// Store the collector in the config (would need to extend ChainConfig)
	// For now, just enable metrics
	return cb
}

// WithMaxConcurrency sets the maximum concurrency for parallel execution.
func (cb *ChainBuilder) WithMaxConcurrency(maxConcurrency int) *ChainBuilder {
	if maxConcurrency <= 0 {
		cb.errors = append(cb.errors, fmt.Errorf("max concurrency must be positive, got %d", maxConcurrency))
		return cb
	}
	
	cb.config.MaxConcurrency = maxConcurrency
	return cb
}

// WithBufferSize sets the buffer size for pipeline execution.
func (cb *ChainBuilder) WithBufferSize(bufferSize int) *ChainBuilder {
	if bufferSize <= 0 {
		cb.errors = append(cb.errors, fmt.Errorf("buffer size must be positive, got %d", bufferSize))
		return cb
	}
	
	cb.config.BufferSize = bufferSize
	return cb
}

// WithErrorHandling sets the error handling strategy.
func (cb *ChainBuilder) WithErrorHandling(strategy string) *ChainBuilder {
	validStrategies := []string{"fail-fast", "continue", "isolate"}
	valid := false
	for _, s := range validStrategies {
		if s == strategy {
			valid = true
			break
		}
	}
	
	if !valid {
		cb.errors = append(cb.errors, fmt.Errorf("invalid error handling strategy '%s', must be one of: %v", strategy, validStrategies))
		return cb
	}
	
	cb.config.ErrorHandling = strategy
	return cb
}

// WithTracing enables tracing for the chain.
func (cb *ChainBuilder) WithTracing(enabled bool) *ChainBuilder {
	cb.config.EnableTracing = enabled
	return cb
}

// AddValidator adds a validator to check the chain during build.
func (cb *ChainBuilder) AddValidator(validator Validator) *ChainBuilder {
	if validator != nil {
		cb.validators = append(cb.validators, validator)
	}
	return cb
}

// Validate validates the current chain configuration and filters.
func (cb *ChainBuilder) Validate() error {
	// Check for accumulated errors
	if len(cb.errors) > 0 {
		return fmt.Errorf("builder has validation errors: %v", cb.errors)
	}
	
	// Validate configuration
	if err := cb.config.Validate(); err != nil {
		return fmt.Errorf("invalid chain config: %w", err)
	}
	
	// Check if we have any filters
	if len(cb.filters) == 0 {
		return fmt.Errorf("chain must have at least one filter")
	}
	
	// Run custom validators
	for _, validator := range cb.validators {
		if err := validator.Validate(cb.filters, cb.config); err != nil {
			return fmt.Errorf("validation failed: %w", err)
		}
	}
	
	// Mode-specific validation
	switch cb.config.ExecutionMode {
	case types.Parallel:
		if cb.config.MaxConcurrency <= 0 {
			return fmt.Errorf("parallel mode requires MaxConcurrency > 0")
		}
	case types.Pipeline:
		if cb.config.BufferSize <= 0 {
			return fmt.Errorf("pipeline mode requires BufferSize > 0")
		}
	}
	
	return nil
}

// Build creates and returns a ready-to-use filter chain.
func (cb *ChainBuilder) Build() (*core.FilterChain, error) {
	// Validate before building
	if err := cb.Validate(); err != nil {
		return nil, err
	}
	
	// Apply optimizations if requested
	optimizedFilters := cb.optimize(cb.filters)
	
	// Create the chain
	chain := core.NewFilterChain(cb.config)
	if chain == nil {
		return nil, fmt.Errorf("failed to create filter chain")
	}
	
	// Add all filters to the chain
	for _, filter := range optimizedFilters {
		if err := chain.Add(filter); err != nil {
			return nil, fmt.Errorf("failed to add filter '%s' to chain: %w", filter.Name(), err)
		}
	}
	
	// Initialize the chain
	if err := chain.Initialize(); err != nil {
		return nil, fmt.Errorf("failed to initialize chain: %w", err)
	}
	
	return chain, nil
}

// optimize applies optimizations to the filter arrangement.
func (cb *ChainBuilder) optimize(filters []core.Filter) []core.Filter {
	// This is a placeholder for optimization logic
	// In a real implementation, you might:
	// 1. Combine compatible filters
	// 2. Reorder filters for better performance
	// 3. Parallelize independent filters
	// 4. Minimize data copying
	
	// For now, just return the filters as-is
	return filters
}

// Preset builder functions

// DefaultChain creates a builder with default settings optimized for general use.
func DefaultChain(name string) *ChainBuilder {
	return NewChainBuilder(name).
		WithMode(types.Sequential).
		WithTimeout(30 * time.Second).
		WithErrorHandling("fail-fast")
}

// HighThroughputChain creates a builder optimized for high throughput scenarios.
func HighThroughputChain(name string) *ChainBuilder {
	return NewChainBuilder(name).
		WithMode(types.Parallel).
		WithMaxConcurrency(10).
		WithTimeout(5 * time.Second).
		WithErrorHandling("continue").
		WithBufferSize(10000)
}

// SecureChain creates a builder with security-focused defaults.
func SecureChain(name string) *ChainBuilder {
	return NewChainBuilder(name).
		WithMode(types.Sequential).
		WithTimeout(60 * time.Second).
		WithErrorHandling("fail-fast").
		WithTracing(true)
}

// ResilientChain creates a builder optimized for fault tolerance.
func ResilientChain(name string) *ChainBuilder {
	return NewChainBuilder(name).
		WithMode(types.Sequential).
		WithTimeout(120 * time.Second).
		WithErrorHandling("isolate").
		WithTracing(true)
}

// CompatibilityValidator checks if filters are compatible with each other.
type CompatibilityValidator struct{}

// Validate checks filter compatibility.
func (cv *CompatibilityValidator) Validate(filters []core.Filter, config types.ChainConfig) error {
	// Check for conflicting filters
	for i, filter1 := range filters {
		for j, filter2 := range filters {
			if i != j && cv.areIncompatible(filter1, filter2) {
				return fmt.Errorf("filters '%s' and '%s' are incompatible", filter1.Name(), filter2.Name())
			}
		}
	}
	
	return nil
}

// areIncompatible checks if two filters are incompatible.
func (cv *CompatibilityValidator) areIncompatible(filter1, filter2 core.Filter) bool {
	// This is a simplified implementation
	// In reality, you'd have more sophisticated compatibility checking
	
	// Example: two rate limiters might be redundant
	if filter1.Type() == "rate-limit" && filter2.Type() == "rate-limit" {
		return true
	}
	
	return false
}

// ResourceValidator checks if the chain configuration is within resource limits.
type ResourceValidator struct {
	MaxFilters int
	MaxMemory  int64
}

// Validate checks resource requirements.
func (rv *ResourceValidator) Validate(filters []core.Filter, config types.ChainConfig) error {
	if len(filters) > rv.MaxFilters {
		return fmt.Errorf("too many filters: %d exceeds maximum of %d", len(filters), rv.MaxFilters)
	}
	
	// Check memory requirements (simplified)
	totalMemory := int64(len(filters) * 1024) // Assume 1KB per filter
	if totalMemory > rv.MaxMemory {
		return fmt.Errorf("estimated memory usage %d exceeds maximum of %d", totalMemory, rv.MaxMemory)
	}
	
	return nil
}