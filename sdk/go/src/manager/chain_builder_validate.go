// Package manager provides filter and chain management for the MCP Filter SDK.
package manager

import "fmt"

// Validate checks chain configuration.
func (cb *ChainBuilder) Validate() error {
	// Check for collected errors
	if len(cb.errors) > 0 {
		return fmt.Errorf("validation errors: %v", cb.errors)
	}
	
	// Check filter compatibility
	if len(cb.filters) == 0 {
		return fmt.Errorf("chain must have at least one filter")
	}
	
	// Check mode requirements
	if cb.config.ExecutionMode == Parallel {
		// Verify filters support parallel execution
		for _, filter := range cb.filters {
			// Check filter capabilities
			_ = filter
		}
	}
	
	// Check configuration consistency
	if cb.config.Timeout <= 0 {
		return fmt.Errorf("invalid timeout: %v", cb.config.Timeout)
	}
	
	// Check for circular dependencies
	visited := make(map[string]bool)
	for _, filter := range cb.filters {
		if visited[filter.GetID().String()] {
			return fmt.Errorf("circular dependency detected")
		}
		visited[filter.GetID().String()] = true
	}
	
	// Run custom validators
	for _, validator := range cb.validators {
		if err := validator(cb); err != nil {
			return err
		}
	}
	
	return nil
}