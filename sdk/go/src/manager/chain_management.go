// Package manager provides filter and chain management for the MCP Filter SDK.
package manager

import (
	"fmt"
	"time"
	
	"github.com/google/uuid"
)

// FilterChain represents a chain of filters.
type FilterChain struct {
	Name    string
	Filters []Filter
	Config  ChainConfig
}

// ChainConfig configures a filter chain.
type ChainConfig struct {
	Name            string
	ExecutionMode   ExecutionMode
	Timeout         time.Duration
	EnableMetrics   bool
	EnableTracing   bool
	MaxConcurrency  int
}

// ExecutionMode defines chain execution strategy.
type ExecutionMode int

const (
	Sequential ExecutionMode = iota
	Parallel
	Pipeline
)

// CreateChain creates a new filter chain.
func (fm *FilterManager) CreateChain(config ChainConfig) (*FilterChain, error) {
	fm.mu.Lock()
	defer fm.mu.Unlock()
	
	// Check if chain exists
	if _, exists := fm.chains[config.Name]; exists {
		return nil, fmt.Errorf("chain '%s' already exists", config.Name)
	}
	
	// Check capacity
	if len(fm.chains) >= fm.config.MaxChains {
		return nil, fmt.Errorf("maximum chain limit reached: %d", fm.config.MaxChains)
	}
	
	// Create chain
	chain := &FilterChain{
		Name:    config.Name,
		Filters: make([]Filter, 0),
		Config:  config,
	}
	
	// Add to chains map
	fm.chains[config.Name] = chain
	
	// Emit event
	if fm.events != nil {
		fm.events.Emit(ChainCreatedEvent{
			ChainName: config.Name,
		})
	}
	
	return chain, nil
}

// RemoveChain removes a filter chain.
func (fm *FilterManager) RemoveChain(name string) error {
	fm.mu.Lock()
	defer fm.mu.Unlock()
	
	chain, exists := fm.chains[name]
	if !exists {
		return fmt.Errorf("chain '%s' not found", name)
	}
	
	// Remove chain
	delete(fm.chains, name)
	
	// Emit event
	if fm.events != nil {
		fm.events.Emit(ChainRemovedEvent{
			ChainName: chain.Name,
		})
	}
	
	return nil
}

// GetChain retrieves a filter chain by name.
func (fm *FilterManager) GetChain(name string) (*FilterChain, bool) {
	fm.mu.RLock()
	defer fm.mu.RUnlock()
	
	chain, exists := fm.chains[name]
	return chain, exists
}

// RemoveFilter removes a filter from the chain.
func (fc *FilterChain) RemoveFilter(id uuid.UUID) {
	newFilters := make([]Filter, 0, len(fc.Filters))
	for _, f := range fc.Filters {
		if f.GetID() != id {
			newFilters = append(newFilters, f)
		}
	}
	fc.Filters = newFilters
}