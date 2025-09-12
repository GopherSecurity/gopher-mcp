// Package manager provides filter and chain management for the MCP Filter SDK.
package manager

import (
	"fmt"
	"time"
)

// WithTimeout sets the chain timeout.
func (cb *ChainBuilder) WithTimeout(timeout time.Duration) *ChainBuilder {
	// Validate timeout is positive
	if timeout <= 0 {
		cb.errors = append(cb.errors, fmt.Errorf("timeout must be positive: %v", timeout))
		return cb
	}
	
	cb.config.Timeout = timeout
	return cb
}