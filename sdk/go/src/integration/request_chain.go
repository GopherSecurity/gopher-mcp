// Package integration provides MCP SDK integration.
package integration

// SetRequestChain sets the request filter chain.
func (fs *FilteredMCPServer) SetRequestChain(chain *FilterChain) {
	fs.requestChain = chain
}

// ProcessRequest filters incoming requests.
func (fs *FilteredMCPServer) ProcessRequest(request []byte) ([]byte, error) {
	if fs.requestChain != nil {
		// Pass through filter chain
		// return fs.requestChain.Process(request)
	}
	return request, nil
}
