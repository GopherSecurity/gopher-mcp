// Package manager provides filter and chain management for the MCP Filter SDK.
package manager

import "fmt"

// WithMode sets the execution mode.
func (cb *ChainBuilder) WithMode(mode ExecutionMode) *ChainBuilder {
	// Validate mode
	if mode < Sequential || mode > Pipeline {
		cb.errors = append(cb.errors, fmt.Errorf("invalid execution mode: %d", mode))
		return cb
	}
	
	// Validate mode for current filter set
	if mode == Parallel && len(cb.filters) > 0 {
		// Check if filters support parallel execution
		// This would require checking filter capabilities
	}
	
	cb.config.ExecutionMode = mode
	return cb
}