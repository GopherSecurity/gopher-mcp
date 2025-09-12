// Package filters provides built-in filters for the MCP Filter SDK.
package filters

import (
	"sync"
	"sync/atomic"

	"github.com/GopherSecurity/gopher-mcp/src/types"
)

// FilterBase provides a base implementation for filters.
// It's designed to be embedded in concrete filter implementations.
type FilterBase struct {
	// name is the unique identifier for this filter
	name string

	// filterType categorizes the filter (e.g., "security", "transform")
	filterType string

	// stats tracks filter performance metrics
	stats types.FilterStatistics

	// disposed indicates if the filter has been closed (0=active, 1=disposed)
	disposed int32

	// mu protects concurrent access to filter state
	mu sync.RWMutex

	// config stores the filter configuration
	config types.FilterConfig
}

// NewFilterBase creates a new FilterBase instance.
func NewFilterBase(name, filterType string) *FilterBase {
	return &FilterBase{
		name:       name,
		filterType: filterType,
		stats:      types.FilterStatistics{},
		disposed:   0,
	}
}