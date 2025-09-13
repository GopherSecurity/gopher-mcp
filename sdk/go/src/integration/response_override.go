// Package integration provides MCP SDK integration.
package integration

import (
	"encoding/json"
)

// SendResponse overrides response sending.
func (fs *FilteredMCPServer) SendResponse(response interface{}) error {
	// Intercept response
	data, err := extractResponseData(response)
	if err != nil {
		return err
	}

	// Pass through response chain
	if fs.responseChain != nil {
		filtered, err := fs.ProcessResponse(data, "")
		if err != nil {
			// Handle filter error
			return err
		}
		data = filtered
	}

	// Send filtered response
	// return fs.MCPServer.SendResponse(response)
	return nil
}

func extractResponseData(response interface{}) ([]byte, error) {
	// Extract data from response by marshaling it to JSON
	if response == nil {
		return []byte{}, nil
	}
	return json.Marshal(response)
}
