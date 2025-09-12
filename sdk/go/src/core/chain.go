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
	// Use RLock for read operations, Lock for modifications.
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