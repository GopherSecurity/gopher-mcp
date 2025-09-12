// Package manager provides filter and chain management for the MCP Filter SDK.
package manager

import "github.com/google/uuid"

// GetFilter retrieves a filter by ID.
func (fm *FilterManager) GetFilter(id uuid.UUID) (Filter, bool) {
	// Use read lock for thread safety
	return fm.registry.Get(id)
}

// GetFilterByName retrieves a filter by name.
func (fm *FilterManager) GetFilterByName(name string) (Filter, bool) {
	// Use read lock for thread safety
	return fm.registry.GetByName(name)
}

// GetAllFilters returns copies of all registered filters.
func (fm *FilterManager) GetAllFilters() map[uuid.UUID]Filter {
	// Return copy to prevent modification
	return fm.registry.GetAll()
}

// GetFilterCount returns the number of registered filters.
func (fm *FilterManager) GetFilterCount() int {
	return fm.registry.Count()
}