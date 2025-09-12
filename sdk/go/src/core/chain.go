// Package core provides the core interfaces and types for the MCP Filter SDK.
package core

import (
	"context"
	"sync"
	"sync/atomic"
	"time"

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

// Remove removes a filter from the chain by name.
// The filter is properly closed before removal.
// Removing filters is only allowed when the chain is not running.
//
// Parameters:
//   - name: The name of the filter to remove
//
// Returns:
//   - error: Returns an error if the filter is not found or the chain is running
func (fc *FilterChain) Remove(name string) error {
	fc.mu.Lock()
	defer fc.mu.Unlock()

	// Check if chain is running
	state := fc.getState()
	if state == types.Running {
		return types.FilterError(types.ChainError)
	}

	// Find and remove the filter
	found := false
	newFilters := make([]Filter, 0, len(fc.filters))
	
	for _, filter := range fc.filters {
		if filter.Name() == name {
			// Close the filter before removing
			if err := filter.Close(); err != nil {
				// Log error but continue with removal
				// In production, consider logging this error
			}
			found = true
			// Remove from statistics
			delete(fc.stats.FilterStats, name)
		} else {
			newFilters = append(newFilters, filter)
		}
	}

	if !found {
		return types.FilterError(types.FilterNotFound)
	}

	// Update the filters slice
	fc.filters = newFilters

	// Update chain state if necessary
	if len(fc.filters) == 0 && state == types.Ready {
		fc.setState(types.Uninitialized)
	}

	return nil
}

// Clear removes all filters from the chain.
// Each filter is properly closed before removal.
// Clearing is only allowed when the chain is stopped.
//
// Returns:
//   - error: Returns an error if the chain is not stopped
func (fc *FilterChain) Clear() error {
	fc.mu.Lock()
	defer fc.mu.Unlock()

	// Check if chain is stopped
	state := fc.getState()
	if state != types.Stopped && state != types.Uninitialized {
		return types.FilterError(types.ChainError)
	}

	// Close all filters in reverse order
	for i := len(fc.filters) - 1; i >= 0; i-- {
		if err := fc.filters[i].Close(); err != nil {
			// Log error but continue with cleanup
			// In production, consider logging this error
		}
	}

	// Clear the filters slice
	fc.filters = make([]Filter, 0)

	// Reset statistics
	fc.stats = types.ChainStatistics{
		FilterStats: make(map[string]types.FilterStatistics),
	}

	// Set state to Uninitialized
	fc.setState(types.Uninitialized)

	return nil
}

// Process executes the filter chain on the input data.
// For sequential mode, each filter is processed in order.
// Processing stops on StopIteration status or based on error handling config.
//
// Parameters:
//   - ctx: Context for cancellation and timeout
//   - data: Input data to process
//
// Returns:
//   - *types.FilterResult: The final result after all filters
//   - error: Any error that occurred during processing
func (fc *FilterChain) Process(ctx context.Context, data []byte) (*types.FilterResult, error) {
	// Update state to Running
	if !fc.setState(types.Running) {
		return nil, types.FilterError(types.ChainError)
	}
	defer fc.setState(types.Ready)

	// Track processing start time
	startTime := time.Now()

	// Get a copy of filters to process
	fc.mu.RLock()
	filters := make([]Filter, len(fc.filters))
	copy(filters, fc.filters)
	mode := fc.mode
	fc.mu.RUnlock()

	// Process based on execution mode
	var result *types.FilterResult
	var err error

	switch mode {
	case types.Sequential:
		result, err = fc.processSequential(ctx, data, filters)
	case types.Parallel:
		// TODO: Implement parallel processing
		result, err = fc.processSequential(ctx, data, filters)
	case types.Pipeline:
		// TODO: Implement pipeline processing
		result, err = fc.processSequential(ctx, data, filters)
	case types.Adaptive:
		// TODO: Implement adaptive processing
		result, err = fc.processSequential(ctx, data, filters)
	default:
		result, err = fc.processSequential(ctx, data, filters)
	}

	// Update statistics
	fc.updateChainStats(startTime, err == nil)

	return result, err
}

// processSequential processes filters one by one in order.
func (fc *FilterChain) processSequential(ctx context.Context, data []byte, filters []Filter) (*types.FilterResult, error) {
	currentData := data

	for _, filter := range filters {
		// Check context cancellation
		select {
		case <-ctx.Done():
			return nil, ctx.Err()
		default:
		}

		// Process through the filter
		result, err := filter.Process(ctx, currentData)

		// Handle errors based on configuration
		if err != nil {
			if fc.config.BypassOnError {
				// Skip this filter and continue
				continue
			}
			return nil, err
		}

		// Check the result status
		if result == nil {
			result = types.ContinueWith(currentData)
		}

		switch result.Status {
		case types.StopIteration:
			// Stop processing and return current result
			return result, nil
		case types.Error:
			if !fc.config.BypassOnError {
				return result, result.Error
			}
			// Continue with original data if bypassing errors
			continue
		case types.NeedMoreData:
			// Return and wait for more data
			return result, nil
		case types.Buffered:
			// Data is buffered, continue with empty data or original
			if result.Data == nil {
				currentData = data
			} else {
				currentData = result.Data
			}
		case types.Continue:
			// Update data for next filter
			if result.Data != nil {
				currentData = result.Data
			}
		}

		// Update filter statistics
		fc.updateFilterStats(filter.Name(), filter.GetStats())
	}

	// Return the final result
	return types.ContinueWith(currentData), nil
}

// updateChainStats updates chain statistics after processing.
func (fc *FilterChain) updateChainStats(startTime time.Time, success bool) {
	fc.mu.Lock()
	defer fc.mu.Unlock()

	// Update execution counts
	fc.stats.TotalExecutions++
	if success {
		fc.stats.SuccessCount++
	} else {
		fc.stats.ErrorCount++
	}

	// Calculate latency
	latency := time.Since(startTime)
	
	// Update average latency
	if fc.stats.TotalExecutions > 0 {
		totalLatency := fc.stats.AverageLatency * time.Duration(fc.stats.TotalExecutions-1)
		fc.stats.AverageLatency = (totalLatency + latency) / time.Duration(fc.stats.TotalExecutions)
	}

	// TODO: Update percentile latencies (requires histogram)
	// For now, just update with current value as approximation
	fc.stats.P50Latency = latency
	fc.stats.P90Latency = latency
	fc.stats.P99Latency = latency
}

// updateFilterStats updates statistics for a specific filter.
func (fc *FilterChain) updateFilterStats(name string, stats types.FilterStatistics) {
	fc.mu.Lock()
	defer fc.mu.Unlock()
	fc.stats.FilterStats[name] = stats
}

// GetFilters returns a copy of the filter slice to prevent external modification.
// This method is thread-safe and can be called concurrently.
//
// Returns:
//   - []Filter: A copy of the current filters in the chain
func (fc *FilterChain) GetFilters() []Filter {
	fc.mu.RLock()
	defer fc.mu.RUnlock()

	// Create a copy to prevent external modification
	filters := make([]Filter, len(fc.filters))
	copy(filters, fc.filters)
	
	return filters
}