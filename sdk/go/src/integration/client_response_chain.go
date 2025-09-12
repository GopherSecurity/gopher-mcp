// Package integration provides MCP SDK integration.
package integration

// SetClientResponseChain sets response filter chain.
func (fc *FilteredMCPClient) SetClientResponseChain(chain *FilterChain) {
	fc.responseChain = chain
}

// FilterIncomingResponse filters incoming responses.
func (fc *FilteredMCPClient) FilterIncomingResponse(response []byte) ([]byte, error) {
	if fc.responseChain != nil {
		return fc.responseChain.Process(response)
	}
	return response, nil
}

// Process processes data through chain.
func (fc *FilterChain) Process(data []byte) ([]byte, error) {
	// Process through filters
	return data, nil
}