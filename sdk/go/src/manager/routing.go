// Package manager provides filter and chain management for the MCP Filter SDK.
package manager

import (
	"fmt"
	"regexp"
)

// DefaultRouter implements request routing.
type DefaultRouter struct {
	routes    []Route
	fallback  string
}

// Route defines a routing rule.
type Route struct {
	Pattern  *regexp.Regexp
	Chain    string
	Priority int
	Headers  map[string]string
}

// Route routes message to appropriate chain.
func (r *DefaultRouter) Route(message []byte) (string, error) {
	// Check routes by priority
	for _, route := range r.routes {
		if route.Pattern.Match(message) {
			return route.Chain, nil
		}
	}
	
	// Use fallback
	if r.fallback != "" {
		return r.fallback, nil
	}
	
	return "", fmt.Errorf("no matching route")
}