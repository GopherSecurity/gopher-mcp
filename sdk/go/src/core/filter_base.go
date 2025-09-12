// Package core provides the core interfaces and types for the MCP Filter SDK.
package core

import (
	"sync"
	"sync/atomic"

	"github.com/GopherSecurity/gopher-mcp/src/types"
)

// FilterBase provides a base implementation of the Filter interface.
// It can be embedded in concrete filter implementations to provide
// common functionality and reduce boilerplate code.
//
// FilterBase handles:
//   - Name and type management
//   - Configuration storage
//   - Statistics collection with thread-safety
//   - Disposal state tracking
//
// Example usage:
//
//	type MyFilter struct {
//	    core.FilterBase
//	    // Additional fields specific to this filter
//	}
//
//	func NewMyFilter(name string) *MyFilter {
//	    f := &MyFilter{}
//	    f.name = name
//	    f.filterType = "custom"
//	    return f
//	}
type FilterBase struct {
	// name is the unique identifier for this filter instance.
	name string

	// filterType is the category of this filter.
	filterType string

	// config stores the filter's configuration.
	config types.FilterConfig

	// stats tracks performance metrics for this filter.
	// Protected by statsLock for thread-safe access.
	stats types.FilterStatistics

	// statsLock protects concurrent access to stats.
	statsLock sync.RWMutex

	// disposed indicates if this filter has been closed.
	// Use atomic operations for thread-safe access.
	// 0 = active, 1 = disposed
	disposed int32
}

// NewFilterBase creates a new FilterBase with the given name and type.
// This is a convenience constructor for embedded use.
func NewFilterBase(name, filterType string) FilterBase {
	return FilterBase{
		name:       name,
		filterType: filterType,
		stats:      types.FilterStatistics{},
		disposed:   0,
	}
}

// SetName sets the filter's name.
// This should only be called during initialization.
func (fb *FilterBase) SetName(name string) {
	fb.name = name
}

// SetType sets the filter's type category.
// This should only be called during initialization.
func (fb *FilterBase) SetType(filterType string) {
	fb.filterType = filterType
}

// GetConfig returns a copy of the filter's configuration.
// This is safe to call concurrently.
func (fb *FilterBase) GetConfig() types.FilterConfig {
	return fb.config
}