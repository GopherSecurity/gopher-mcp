// Package manager provides filter and chain management for the MCP Filter SDK.
package manager

import (
	"fmt"
	"time"
)

// ChainBuilder provides fluent interface for chain construction.
type ChainBuilder struct {
	filters    []Filter
	config     ChainConfig
	validators []Validator
	errors     []error
}

// Validator validates chain configuration.
type Validator func(*ChainBuilder) error

// NewChainBuilder creates a new chain builder.
func NewChainBuilder(name string) *ChainBuilder {
	return &ChainBuilder{
		filters: make([]Filter, 0),
		config: ChainConfig{
			Name:          name,
			ExecutionMode: Sequential,
			Timeout:       30 * time.Second,
		},
		validators: make([]Validator, 0),
		errors:     make([]error, 0),
	}
}