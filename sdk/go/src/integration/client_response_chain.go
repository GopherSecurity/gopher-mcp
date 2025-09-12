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

