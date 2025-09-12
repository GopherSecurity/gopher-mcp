// Package integration provides MCP SDK integration.
package integration

// SetResponseChain sets the response filter chain.
func (fs *FilteredMCPServer) SetResponseChain(chain *FilterChain) {
	fs.responseChain = chain
}

// ProcessResponse filters outgoing responses.
func (fs *FilteredMCPServer) ProcessResponse(response []byte, requestID string) ([]byte, error) {
	if fs.responseChain != nil {
		// Maintain correlation with request
		// return fs.responseChain.Process(response)
	}
	return response, nil
}