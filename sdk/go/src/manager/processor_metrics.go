// Package manager provides filter and chain management for the MCP Filter SDK.
package manager

import (
	"sync/atomic"
	"time"
)

// ProcessorMetrics tracks processor statistics.
type ProcessorMetrics struct {
	messagesProcessed atomic.Int64
	routingDecisions  atomic.Int64
	aggregationOps    atomic.Int64
	errorRecoveries   atomic.Int64
	perRoute          map[string]*RouteMetrics
}

// RouteMetrics tracks per-route statistics.
type RouteMetrics struct {
	Requests    int64
	Successes   int64
	Failures    int64
	TotalTime   time.Duration
	AverageTime time.Duration
}

// RecordMessage records a processed message.
func (pm *ProcessorMetrics) RecordMessage(route string, duration time.Duration, success bool) {
	pm.messagesProcessed.Add(1)

	// Update per-route metrics
	// if metrics, exists := pm.perRoute[route]; exists {
	//     metrics.Requests++
	//     if success {
	//         metrics.Successes++
	//     } else {
	//         metrics.Failures++
	//     }
	//     metrics.TotalTime += duration
	// }
}

// RecordRouting records a routing decision.
func (pm *ProcessorMetrics) RecordRouting(from, to string) {
	pm.routingDecisions.Add(1)
}

// RecordAggregation records an aggregation operation.
func (pm *ProcessorMetrics) RecordAggregation(count int) {
	pm.aggregationOps.Add(1)
}

// RecordErrorRecovery records error recovery attempt.
func (pm *ProcessorMetrics) RecordErrorRecovery(success bool) {
	pm.errorRecoveries.Add(1)
}

// GetStatistics returns processor statistics.
func (pm *ProcessorMetrics) GetStatistics() map[string]interface{} {
	return map[string]interface{}{
		"messages_processed": pm.messagesProcessed.Load(),
		"routing_decisions":  pm.routingDecisions.Load(),
		"aggregation_ops":    pm.aggregationOps.Load(),
		"error_recoveries":   pm.errorRecoveries.Load(),
	}
}
