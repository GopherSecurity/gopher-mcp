// Package manager provides filter and chain management for the MCP Filter SDK.
package manager

import (
	"fmt"
	
	"github.com/google/uuid"
)

// UnregisterFilter removes a filter from the registry.
func (fm *FilterManager) UnregisterFilter(id uuid.UUID) error {
	// Find and remove filter
	filter, exists := fm.registry.Remove(id)
	if !exists {
		return fmt.Errorf("filter not found: %s", id)
	}
	
	// Remove from any chains
	fm.mu.Lock()
	for _, chain := range fm.chains {
		if chain != nil {
			chain.RemoveFilter(id)
		}
	}
	fm.mu.Unlock()
	
	// Close filter
	if err := filter.Close(); err != nil {
		// Log error but continue
	}
	
	// Emit event
	if fm.events != nil {
		fm.events.Emit(FilterUnregisteredEvent{
			FilterID:   id,
			FilterName: filter.GetName(),
		})
	}
	
	return nil
}