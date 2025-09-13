// Package integration provides MCP SDK integration.
package integration

import (
	"sync/atomic"
	"time"
)

// ServerMetrics tracks MCP server metrics.
type ServerMetrics struct {
	toolInvocations  map[string]*atomic.Int64
	promptExecutions map[string]*atomic.Int64
	resourceAccesses map[string]*ResourceMetrics
	protocolErrors   atomic.Int64
}

// ResourceMetrics tracks resource access metrics.
type ResourceMetrics struct {
	Reads  atomic.Int64
	Writes atomic.Int64
}

// RecordToolInvocation records tool invocation.
func (sm *ServerMetrics) RecordToolInvocation(tool string, duration time.Duration) {
	if counter, exists := sm.toolInvocations[tool]; exists {
		counter.Add(1)
	}
}

// RecordPromptExecution records prompt execution.
func (sm *ServerMetrics) RecordPromptExecution(prompt string, duration time.Duration) {
	if counter, exists := sm.promptExecutions[prompt]; exists {
		counter.Add(1)
	}
}

// RecordResourceAccess records resource access.
func (sm *ServerMetrics) RecordResourceAccess(resource string, isWrite bool) {
	if metrics, exists := sm.resourceAccesses[resource]; exists {
		if isWrite {
			metrics.Writes.Add(1)
		} else {
			metrics.Reads.Add(1)
		}
	}
}

// RecordProtocolError records protocol error.
func (sm *ServerMetrics) RecordProtocolError() {
	sm.protocolErrors.Add(1)
}

// GetStatistics returns server statistics.
func (sm *ServerMetrics) GetStatistics() map[string]interface{} {
	stats := make(map[string]interface{})

	// Aggregate tool invocations
	toolStats := make(map[string]int64)
	for tool, counter := range sm.toolInvocations {
		toolStats[tool] = counter.Load()
	}
	stats["tools"] = toolStats

	// Aggregate prompt executions
	promptStats := make(map[string]int64)
	for prompt, counter := range sm.promptExecutions {
		promptStats[prompt] = counter.Load()
	}
	stats["prompts"] = promptStats

	stats["protocol_errors"] = sm.protocolErrors.Load()

	return stats
}
