// Package integration provides MCP SDK integration.
package integration

// SetClientRequestChain sets request filter chain.
func (fc *FilteredMCPClient) SetClientRequestChain(chain *FilterChain) {
	fc.requestChain = chain
}

// FilterOutgoingRequest filters outgoing requests.
func (fc *FilteredMCPClient) FilterOutgoingRequest(request []byte) ([]byte, error) {
	if fc.requestChain != nil {
		return fc.requestChain.Process(request)
	}
	return request, nil
}
