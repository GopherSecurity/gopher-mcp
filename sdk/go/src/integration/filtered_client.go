// Package integration provides MCP SDK integration.
package integration

import (
	"github.com/modelcontextprotocol/go-sdk/pkg/client"
)

// FilteredMCPClient wraps MCP client with filtering.
type FilteredMCPClient struct {
	*client.MCPClient // Embedded MCP client
	requestChain      *FilterChain
	responseChain     *FilterChain
	reconnectStrategy ReconnectStrategy
}

// ReconnectStrategy defines reconnection behavior.
type ReconnectStrategy interface {
	ShouldReconnect(error) bool
	NextDelay() time.Duration
}

// NewFilteredMCPClient creates a filtered MCP client.
func NewFilteredMCPClient() *FilteredMCPClient {
	return &FilteredMCPClient{
		MCPClient:     client.NewMCPClient(),
		requestChain:  &FilterChain{},
		responseChain: &FilterChain{},
	}
}