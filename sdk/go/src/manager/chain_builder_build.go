// Package manager provides filter and chain management for the MCP Filter SDK.
package manager

import "fmt"

// Build creates the filter chain.
func (cb *ChainBuilder) Build() (*FilterChain, error) {
	// Validate configuration
	if err := cb.Validate(); err != nil {
		return nil, fmt.Errorf("validation failed: %w", err)
	}
	
	// Create chain
	chain := &FilterChain{
		Name:    cb.config.Name,
		Filters: make([]Filter, len(cb.filters)),
		Config:  cb.config,
	}
	
	// Initialize filters in order
	for i, filter := range cb.filters {
		// Initialize filter if needed
		// if initializer, ok := filter.(Initializable); ok {
		//     if err := initializer.Initialize(); err != nil {
		//         return nil, fmt.Errorf("failed to initialize filter %d: %w", i, err)
		//     }
		// }
		chain.Filters[i] = filter
	}
	
	// Set up metrics if enabled
	if cb.config.EnableMetrics {
		// Setup metrics collection
		// chain.setupMetrics()
	}
	
	// Return ready-to-use chain
	return chain, nil
}