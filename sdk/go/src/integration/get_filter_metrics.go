// Package integration provides MCP SDK integration.
package integration

import (
	"sync"
	"time"
)

// FilterMetrics contains metrics for filter performance.
type FilterMetrics struct {
	FilterID          string
	FilterName        string
	ProcessedCount    int64
	SuccessCount      int64
	ErrorCount        int64
	TotalDuration     time.Duration
	AverageDuration   time.Duration
	MinDuration       time.Duration
	MaxDuration       time.Duration
	LastProcessedTime time.Time
	ErrorRate         float64
	Throughput        float64
}

// ChainMetrics contains metrics for filter chain.
type ChainMetrics struct {
	ChainID         string
	FilterCount     int
	TotalProcessed  int64
	TotalDuration   time.Duration
	AverageDuration time.Duration
	Filters         []*FilterMetrics
}

// SystemMetrics contains overall system metrics.
type SystemMetrics struct {
	TotalRequests       int64
	TotalResponses      int64
	TotalNotifications  int64
	ActiveChains        int
	ActiveFilters       int
	SystemUptime        time.Duration
	StartTime           time.Time
	RequestMetrics      *ChainMetrics
	ResponseMetrics     *ChainMetrics
	NotificationMetrics *ChainMetrics
}

// MetricsCollector collects filter metrics.
type MetricsCollector struct {
	filterMetrics map[string]*FilterMetrics
	chainMetrics  map[string]*ChainMetrics
	systemMetrics *SystemMetrics
	mu            sync.RWMutex
}

// GetFilterMetrics retrieves metrics for all filters.
func (fc *FilteredMCPClient) GetFilterMetrics() *SystemMetrics {
	// Get system metrics snapshot - only hold lock briefly
	fc.metricsCollector.mu.RLock()
	systemMetrics := fc.metricsCollector.systemMetrics
	chainMetricsCount := len(fc.metricsCollector.chainMetrics)
	filterMetricsCount := len(fc.metricsCollector.filterMetrics)
	fc.metricsCollector.mu.RUnlock()

	// Create system metrics snapshot
	metrics := &SystemMetrics{
		TotalRequests:      systemMetrics.TotalRequests,
		TotalResponses:     systemMetrics.TotalResponses,
		TotalNotifications: systemMetrics.TotalNotifications,
		ActiveChains:       chainMetricsCount,
		ActiveFilters:      filterMetricsCount,
		SystemUptime:       time.Since(systemMetrics.StartTime),
		StartTime:          systemMetrics.StartTime,
	}

	// Get request chain metrics
	if fc.requestChain != nil {
		metrics.RequestMetrics = fc.getChainMetrics(fc.requestChain)
	}

	// Get response chain metrics
	if fc.responseChain != nil {
		metrics.ResponseMetrics = fc.getChainMetrics(fc.responseChain)
	}

	// Get notification chain metrics
	if fc.notificationChain != nil {
		metrics.NotificationMetrics = fc.getChainMetrics(fc.notificationChain)
	}

	return metrics
}

// getChainMetrics retrieves metrics for a filter chain.
func (fc *FilteredMCPClient) getChainMetrics(chain *FilterChain) *ChainMetrics {
	chainID := chain.GetID()

	fc.metricsCollector.mu.RLock()
	existing, exists := fc.metricsCollector.chainMetrics[chainID]
	fc.metricsCollector.mu.RUnlock()

	if exists {
		return existing
	}

	// Create new chain metrics
	metrics := &ChainMetrics{
		ChainID:     chainID,
		FilterCount: len(chain.filters),
		Filters:     make([]*FilterMetrics, 0, len(chain.filters)),
	}

	// Collect metrics for each filter - no lock held here
	for _, filter := range chain.filters {
		filterMetrics := fc.getFilterMetricsUnlocked(filter)
		metrics.Filters = append(metrics.Filters, filterMetrics)
		metrics.TotalProcessed += filterMetrics.ProcessedCount
		metrics.TotalDuration += filterMetrics.TotalDuration
	}

	// Calculate average duration
	if metrics.TotalProcessed > 0 {
		metrics.AverageDuration = time.Duration(
			int64(metrics.TotalDuration) / metrics.TotalProcessed,
		)
	}

	// Store metrics - check again to avoid race
	fc.metricsCollector.mu.Lock()
	// Double-check in case another goroutine created it
	if existing, exists := fc.metricsCollector.chainMetrics[chainID]; exists {
		fc.metricsCollector.mu.Unlock()
		return existing
	}
	fc.metricsCollector.chainMetrics[chainID] = metrics
	fc.metricsCollector.mu.Unlock()

	return metrics
}

// getFilterMetrics retrieves metrics for a single filter.
func (fc *FilteredMCPClient) getFilterMetrics(filter Filter) *FilterMetrics {
	filterID := filter.GetID()

	fc.metricsCollector.mu.RLock()
	existing, exists := fc.metricsCollector.filterMetrics[filterID]
	fc.metricsCollector.mu.RUnlock()

	if exists {
		return existing
	}

	// Create new filter metrics
	metrics := &FilterMetrics{
		FilterID:   filterID,
		FilterName: filter.GetName(),
	}

	// Store metrics
	fc.metricsCollector.mu.Lock()
	fc.metricsCollector.filterMetrics[filterID] = metrics
	fc.metricsCollector.mu.Unlock()

	return metrics
}

// getFilterMetricsUnlocked retrieves metrics for a single filter without holding the lock.
// This is used internally when we're already in a metrics collection context.
func (fc *FilteredMCPClient) getFilterMetricsUnlocked(filter Filter) *FilterMetrics {
	filterID := filter.GetID()

	// Try to get existing metrics with minimal locking
	fc.metricsCollector.mu.RLock()
	existing, exists := fc.metricsCollector.filterMetrics[filterID]
	fc.metricsCollector.mu.RUnlock()

	if exists {
		return existing
	}

	// Create new filter metrics
	metrics := &FilterMetrics{
		FilterID:   filterID,
		FilterName: filter.GetName(),
	}

	// Store metrics with double-check pattern
	fc.metricsCollector.mu.Lock()
	// Check again in case another goroutine created it
	if existing, exists := fc.metricsCollector.filterMetrics[filterID]; exists {
		fc.metricsCollector.mu.Unlock()
		return existing
	}
	fc.metricsCollector.filterMetrics[filterID] = metrics
	fc.metricsCollector.mu.Unlock()

	return metrics
}

// RecordFilterExecution records filter execution metrics.
func (fc *FilteredMCPClient) RecordFilterExecution(
	filterID string,
	duration time.Duration,
	success bool,
) {
	fc.metricsCollector.mu.Lock()
	defer fc.metricsCollector.mu.Unlock()

	metrics, exists := fc.metricsCollector.filterMetrics[filterID]
	if !exists {
		metrics = &FilterMetrics{
			FilterID:    filterID,
			MinDuration: duration,
			MaxDuration: duration,
		}
		fc.metricsCollector.filterMetrics[filterID] = metrics
	}

	// Update metrics
	metrics.ProcessedCount++
	metrics.TotalDuration += duration
	metrics.LastProcessedTime = time.Now()

	if success {
		metrics.SuccessCount++
	} else {
		metrics.ErrorCount++
	}

	// Update min/max duration
	if duration < metrics.MinDuration || metrics.MinDuration == 0 {
		metrics.MinDuration = duration
	}
	if duration > metrics.MaxDuration {
		metrics.MaxDuration = duration
	}

	// Calculate averages and rates
	if metrics.ProcessedCount > 0 {
		metrics.AverageDuration = time.Duration(
			int64(metrics.TotalDuration) / metrics.ProcessedCount,
		)
		metrics.ErrorRate = float64(metrics.ErrorCount) / float64(metrics.ProcessedCount)

		// Calculate throughput (requests per second)
		elapsed := time.Since(fc.metricsCollector.systemMetrics.StartTime).Seconds()
		if elapsed > 0 {
			metrics.Throughput = float64(metrics.ProcessedCount) / elapsed
		}
	}
}

// ResetMetrics resets all metrics.
func (fc *FilteredMCPClient) ResetMetrics() {
	fc.metricsCollector.mu.Lock()
	defer fc.metricsCollector.mu.Unlock()

	fc.metricsCollector.filterMetrics = make(map[string]*FilterMetrics)
	fc.metricsCollector.chainMetrics = make(map[string]*ChainMetrics)
	fc.metricsCollector.systemMetrics = &SystemMetrics{
		StartTime: time.Now(),
	}
}

// ExportMetrics exports metrics in specified format.
func (fc *FilteredMCPClient) ExportMetrics(format string) ([]byte, error) {
	metrics := fc.GetFilterMetrics()

	switch format {
	case "json":
		// Export as JSON
		return exportMetricsJSON(metrics)
	case "prometheus":
		// Export in Prometheus format
		return exportMetricsPrometheus(metrics)
	default:
		// Export as text
		return exportMetricsText(metrics)
	}
}

// Helper functions for export
func exportMetricsJSON(metrics *SystemMetrics) ([]byte, error) {
	// Implementation would use json.Marshal
	return []byte("{}"), nil
}

func exportMetricsPrometheus(metrics *SystemMetrics) ([]byte, error) {
	// Implementation would format for Prometheus
	return []byte("# HELP filter_requests_total Total requests processed\n"), nil
}

func exportMetricsText(metrics *SystemMetrics) ([]byte, error) {
	// Implementation would format as readable text
	return []byte("System Metrics Report\n"), nil
}
