// Package core provides the core interfaces and types for the MCP Filter SDK.
package core

import (
	"context"
	"time"

	"github.com/GopherSecurity/gopher-mcp/src/types"
)

// FilterFunc is a function type that implements the Filter interface.
// This allows regular functions to be used as filters without creating
// a full struct implementation.
//
// Example usage:
//
//	// Create a simple filter from a function
//	uppercaseFilter := core.FilterFunc(func(ctx context.Context, data []byte) (*types.FilterResult, error) {
//	    upperData := bytes.ToUpper(data)
//	    return types.ContinueWith(upperData), nil
//	})
//
//	// Use it in a filter chain
//	chain.Add(uppercaseFilter)
type FilterFunc func(ctx context.Context, data []byte) (*types.FilterResult, error)

// Process calls the function itself, implementing the Filter interface.
func (f FilterFunc) Process(ctx context.Context, data []byte) (*types.FilterResult, error) {
	return f(ctx, data)
}

// Initialize provides a no-op implementation for the Filter interface.
// FilterFunc instances don't store configuration.
func (f FilterFunc) Initialize(config types.FilterConfig) error {
	// FilterFunc doesn't need initialization
	return nil
}

// Close provides a no-op implementation for the Filter interface.
// FilterFunc instances don't hold resources.
func (f FilterFunc) Close() error {
	// FilterFunc doesn't need cleanup
	return nil
}

// Name returns a generic name for function-based filters.
// Override this by wrapping the function in a struct if you need a specific name.
func (f FilterFunc) Name() string {
	return "filter-func"
}

// Type returns a generic type for function-based filters.
// Override this by wrapping the function in a struct if you need a specific type.
func (f FilterFunc) Type() string {
	return "function"
}

// GetStats returns empty statistics for function-based filters.
// FilterFunc instances don't track statistics by default.
func (f FilterFunc) GetStats() types.FilterStatistics {
	return types.FilterStatistics{}
}

// WrapFilterFunc creates a named filter from a function.
// This provides a way to give function-based filters custom names and types.
//
// Example:
//
//	filter := core.WrapFilterFunc("uppercase", "transformation",
//	    func(ctx context.Context, data []byte) (*types.FilterResult, error) {
//	        return types.ContinueWith(bytes.ToUpper(data)), nil
//	    })
func WrapFilterFunc(name, filterType string, fn FilterFunc) Filter {
	return &wrappedFilterFunc{
		FilterBase: NewFilterBase(name, filterType),
		fn:         fn,
	}
}

// wrappedFilterFunc wraps a FilterFunc with a FilterBase for better metadata.
type wrappedFilterFunc struct {
	FilterBase
	fn FilterFunc
}

// Process delegates to the wrapped function and updates statistics.
func (w *wrappedFilterFunc) Process(ctx context.Context, data []byte) (*types.FilterResult, error) {
	// Check if disposed
	if err := w.checkDisposed(); err != nil {
		return nil, err
	}

	// Track start time for statistics
	startTime := time.Now()

	// Call the wrapped function
	result, err := w.fn(ctx, data)

	// Update statistics
	processingTime := uint64(time.Since(startTime).Microseconds())
	w.updateStats(uint64(len(data)), processingTime, err != nil)

	return result, err
}
