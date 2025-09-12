// Package manager provides filter and chain management for the MCP Filter SDK.
package manager

import "fmt"

// Add appends a filter to the chain.
func (cb *ChainBuilder) Add(filter Filter) *ChainBuilder {
	// Validate filter not nil
	if filter == nil {
		cb.errors = append(cb.errors, fmt.Errorf("cannot add nil filter"))
		return cb
	}
	
	// Check for duplicate
	for _, f := range cb.filters {
		if f.GetID() == filter.GetID() {
			cb.errors = append(cb.errors, fmt.Errorf("duplicate filter: %s", filter.GetID()))
			return cb
		}
	}
	
	// Append filter
	cb.filters = append(cb.filters, filter)
	return cb
}