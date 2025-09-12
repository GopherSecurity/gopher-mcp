// Package core provides the core interfaces and types for the MCP Filter SDK.
package core

import (
	"context"
	"sync"
	"sync/atomic"

	"github.com/GopherSecurity/gopher-mcp/src/types"
)

// FilterChain manages a sequence of filters and coordinates their execution.
// It supports different execution modes and provides thread-safe operations
// for managing filters and processing data through the chain.
//
// FilterChain features:
//   - Multiple execution modes (Sequential, Parallel, Pipeline, Adaptive)
//   - Thread-safe filter management
//   - Performance statistics collection
//   - Graceful lifecycle management
//   - Context-based cancellation
//
// Example usage:
//
//	chain := &FilterChain{
//	    config: types.ChainConfig{
//	        Name: "processing-chain",
//	        ExecutionMode: types.Sequential,
//	    },
//	}
//	chain.Add(filter1)
//	chain.Add(filter2)
//	result := chain.Process(ctx, data)
type FilterChain struct {
	// filters is the ordered list of filters in this chain.
	// Protected by mu for thread-safe access.
	filters []Filter

	// mode determines how filters are executed.
	mode types.ExecutionMode

	// mu protects concurrent access to filters and chain state.
	// Lock ordering to prevent deadlocks:
	//   1. Always acquire mu before any filter-specific locks
	//   2. Never hold mu while calling filter.Process()
	//   3. Use RLock for read operations (getting filters, stats)
	//   4. Use Lock for modifications (add, remove, state changes)
	// Common patterns:
	//   - Read filters: mu.RLock() -> copy slice -> mu.RUnlock() -> process
	//   - Modify chain: mu.Lock() -> validate -> modify -> mu.Unlock()
	mu sync.RWMutex

	// stats tracks performance metrics for the chain.
	stats types.ChainStatistics

	// config stores the chain's configuration.
	config types.ChainConfig

	// state holds the current lifecycle state of the chain.
	// Use atomic operations for thread-safe access.
	state atomic.Value

	// ctx is the context for this chain's lifecycle.
	ctx context.Context

	// cancel is the cancellation function for the chain's context.
	cancel context.CancelFunc
}

// NewFilterChain creates a new filter chain with the given configuration.
func NewFilterChain(config types.ChainConfig) *FilterChain {
	ctx, cancel := context.WithCancel(context.Background())
	
	chain := &FilterChain{
		filters: make([]Filter, 0),
		mode:    config.ExecutionMode,
		config:  config,
		stats:   types.ChainStatistics{
			FilterStats: make(map[string]types.FilterStatistics),
		},
		ctx:     ctx,
		cancel:  cancel,
	}
	
	// Initialize state to Uninitialized
	chain.state.Store(types.Uninitialized)
	
	return chain
}

// getState returns the current state of the chain.
func (fc *FilterChain) getState() types.ChainState {
	if state, ok := fc.state.Load().(types.ChainState); ok {
		return state
	}
	return types.Uninitialized
}

// setState updates the chain's state if the transition is valid.
func (fc *FilterChain) setState(newState types.ChainState) bool {
	currentState := fc.getState()
	if currentState.CanTransitionTo(newState) {
		fc.state.Store(newState)
		return true
	}
	return false
}

// GetExecutionMode returns the current execution mode of the chain.
// This is safe to call concurrently.
func (fc *FilterChain) GetExecutionMode() types.ExecutionMode {
	fc.mu.RLock()
	defer fc.mu.RUnlock()
	return fc.mode
}

// SetExecutionMode updates the chain's execution mode.
// Mode changes are only allowed when the chain is not running.
//
// Parameters:
//   - mode: The new execution mode to set
//
// Returns:
//   - error: Returns an error if the chain is running or the mode is invalid
func (fc *FilterChain) SetExecutionMode(mode types.ExecutionMode) error {
	fc.mu.Lock()
	defer fc.mu.Unlock()

	// Check if chain is running
	state := fc.getState()
	if state == types.Running {
		return types.FilterError(types.ChainError)
	}

	// Validate the mode based on chain configuration
	if err := fc.validateExecutionMode(mode); err != nil {
		return err
	}

	// Update the mode
	fc.mode = mode
	fc.config.ExecutionMode = mode

	return nil
}

// validateExecutionMode checks if the execution mode is valid for the current chain.
func (fc *FilterChain) validateExecutionMode(mode types.ExecutionMode) error {
	// Check if mode requires specific configuration
	switch mode {
	case types.Parallel:
		if fc.config.MaxConcurrency <= 0 {
			fc.config.MaxConcurrency = 10 // Set default
		}
	case types.Pipeline:
		if fc.config.BufferSize <= 0 {
			fc.config.BufferSize = 100 // Set default
		}
	case types.Sequential, types.Adaptive:
		// No special requirements
	default:
		return types.FilterError(types.InvalidConfiguration)
	}

	return nil
}

// Add appends a filter to the end of the chain.
// The filter must not be nil and must have a unique name within the chain.
// Adding filters is only allowed when the chain is not running.
//
// Parameters:
//   - filter: The filter to add to the chain
//
// Returns:
//   - error: Returns an error if the filter is invalid or the chain is running
func (fc *FilterChain) Add(filter Filter) error {
	if filter == nil {
		return types.FilterError(types.InvalidConfiguration)
	}

	fc.mu.Lock()
	defer fc.mu.Unlock()

	// Check if chain is running
	state := fc.getState()
	if state == types.Running {
		return types.FilterError(types.ChainError)
	}

	// Check if filter with same name already exists
	filterName := filter.Name()
	for _, existing := range fc.filters {
		if existing.Name() == filterName {
			return types.FilterError(types.FilterAlreadyExists)
		}
	}

	// Add the filter to the chain
	fc.filters = append(fc.filters, filter)

	// Update chain state if necessary
	if state == types.Uninitialized && len(fc.filters) > 0 {
		fc.setState(types.Ready)
	}

	// Update statistics
	fc.stats.FilterStats[filterName] = filter.GetStats()

	return nil
}