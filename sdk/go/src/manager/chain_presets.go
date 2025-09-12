// Package manager provides filter and chain management for the MCP Filter SDK.
package manager

import "time"

// DefaultChain creates a standard chain configuration.
func DefaultChain(name string) *ChainBuilder {
	return NewChainBuilder(name).
		WithMode(Sequential).
		WithTimeout(30 * time.Second)
}

// HighThroughputChain creates a high-throughput optimized chain.
func HighThroughputChain(name string) *ChainBuilder {
	return NewChainBuilder(name).
		WithMode(Parallel).
		WithTimeout(10 * time.Second)
		// Add more optimizations
}

// SecureChain creates a security-focused chain.
func SecureChain(name string) *ChainBuilder {
	return NewChainBuilder(name).
		WithMode(Sequential).
		WithTimeout(60 * time.Second)
		// Add security filters
}

// ResilientChain creates a resilient chain with retry and fallback.
func ResilientChain(name string) *ChainBuilder {
	return NewChainBuilder(name).
		WithMode(Sequential).
		WithTimeout(120 * time.Second)
		// Add resilience patterns
}