// Package manager provides filter and chain management for the MCP Filter SDK.
package manager

import (
	"sync"
	"time"
)

// ManagerStatistics aggregates statistics from all filters and chains.
type ManagerStatistics struct {
	TotalFilters      int
	TotalChains       int
	ProcessedMessages int64
	TotalErrors       int64
	AverageLatency    time.Duration
	P95Latency        time.Duration
	P99Latency        time.Duration
	Throughput        float64
	LastUpdated       time.Time
	
	mu sync.RWMutex
}

// AggregateStatistics collects statistics from all filters and chains.
func (fm *FilterManager) AggregateStatistics() ManagerStatistics {
	stats := ManagerStatistics{
		TotalFilters: fm.registry.Count(),
		TotalChains:  len(fm.chains),
		LastUpdated:  time.Now(),
	}
	
	// Collect from all filters
	allFilters := fm.registry.GetAll()
	var totalLatency time.Duration
	var latencies []time.Duration
	
	for range allFilters {
		// Assuming filters have GetStats() method
		// filterStats := filter.GetStats()
		// stats.ProcessedMessages += filterStats.ProcessedCount
		// stats.TotalErrors += filterStats.ErrorCount
		// latencies = append(latencies, filterStats.Latencies...)
	}
	
	// Calculate percentiles
	if len(latencies) > 0 {
		stats.AverageLatency = totalLatency / time.Duration(len(latencies))
		stats.P95Latency = calculatePercentile(latencies, 95)
		stats.P99Latency = calculatePercentile(latencies, 99)
	}
	
	// Calculate throughput
	stats.Throughput = float64(stats.ProcessedMessages) / time.Since(fm.startTime).Seconds()
	
	fm.stats = stats
	return stats
}

// calculatePercentile calculates the percentile value from latencies.
func calculatePercentile(latencies []time.Duration, percentile int) time.Duration {
	if len(latencies) == 0 {
		return 0
	}
	
	// Simple percentile calculation
	index := len(latencies) * percentile / 100
	if index >= len(latencies) {
		index = len(latencies) - 1
	}
	
	return latencies[index]
}

// StartStatisticsCollection starts periodic statistics aggregation.
func (fm *FilterManager) StartStatisticsCollection() {
	if !fm.config.EnableMetrics {
		return
	}
	
	go func() {
		ticker := time.NewTicker(fm.config.MetricsInterval)
		defer ticker.Stop()
		
		for {
			select {
			case <-ticker.C:
				fm.AggregateStatistics()
			case <-fm.stopCh:
				return
			}
		}
	}()
}

// GetStatistics returns current statistics.
func (fm *FilterManager) GetStatistics() ManagerStatistics {
	fm.stats.mu.RLock()
	defer fm.stats.mu.RUnlock()
	return fm.stats
}