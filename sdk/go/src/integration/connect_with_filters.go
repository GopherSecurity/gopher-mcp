// Package integration provides MCP SDK integration.
package integration

import "context"

// Transport interface for connection.
type Transport interface {
	Connect(ctx context.Context) error
	Send(data []byte) error
	Receive() ([]byte, error)
	Disconnect() error
}

// ConnectWithFilters establishes connection with filters.
func (fc *FilteredMCPClient) ConnectWithFilters(ctx context.Context, transport Transport, filters ...Filter) error {
	// Create connection-level filter chain
	chain := NewFilterChain()
	for _, filter := range filters {
		chain.Add(filter)
	}

	// Apply to all traffic
	fc.SetClientRequestChain(chain)
	fc.SetClientResponseChain(chain)

	// Establish connection
	if err := transport.Connect(ctx); err != nil {
		return err
	}

	// Connect MCP client
	// return fc.MCPClient.Connect(transport)
	return nil
}
