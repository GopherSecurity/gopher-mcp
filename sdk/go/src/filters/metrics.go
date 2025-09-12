// Package filters provides built-in filters for the MCP Filter SDK.
package filters

import (
	"bytes"
	"context"
	"encoding/json"
	"fmt"
	"io"
	"math"
	"net"
	"net/http"
	"strings"
	"sync"
	"sync/atomic"
	"time"

	"github.com/GopherSecurity/gopher-mcp/src/types"
)

// MetricsCollector defines the interface for metrics collection backends.
type MetricsCollector interface {
	// RecordLatency records a latency measurement
	RecordLatency(name string, duration time.Duration)
	
	// IncrementCounter increments a counter metric
	IncrementCounter(name string, delta int64)
	
	// SetGauge sets a gauge metric to a specific value
	SetGauge(name string, value float64)
	
	// RecordHistogram records a value in a histogram
	RecordHistogram(name string, value float64)
	
	// Flush forces export of buffered metrics
	Flush() error
	
	// Close shuts down the collector
	Close() error
}

// MetricsExporter defines the interface for exporting metrics to external systems.
type MetricsExporter interface {
	// Export sends metrics to the configured backend
	Export(metrics map[string]interface{}) error
	
	// Format returns the export format name
	Format() string
	
	// Close shuts down the exporter
	Close() error
}

// PrometheusExporter exports metrics in Prometheus format.
type PrometheusExporter struct {
	endpoint   string
	labels     map[string]string
	httpClient *http.Client
	mu         sync.RWMutex
}

// NewPrometheusExporter creates a new Prometheus exporter.
func NewPrometheusExporter(endpoint string, labels map[string]string) *PrometheusExporter {
	return &PrometheusExporter{
		endpoint: endpoint,
		labels:   labels,
		httpClient: &http.Client{
			Timeout: 10 * time.Second,
		},
	}
}

// Export sends metrics in Prometheus format.
func (pe *PrometheusExporter) Export(metrics map[string]interface{}) error {
	pe.mu.RLock()
	defer pe.mu.RUnlock()
	
	// Format metrics as Prometheus text format
	var buffer bytes.Buffer
	for name, value := range metrics {
		pe.writeMetric(&buffer, name, value)
	}
	
	// Push to Prometheus gateway if configured
	if pe.endpoint != "" {
		req, err := http.NewRequest("POST", pe.endpoint, &buffer)
		if err != nil {
			return fmt.Errorf("failed to create request: %w", err)
		}
		
		req.Header.Set("Content-Type", "text/plain; version=0.0.4")
		
		resp, err := pe.httpClient.Do(req)
		if err != nil {
			return fmt.Errorf("failed to push metrics: %w", err)
		}
		defer resp.Body.Close()
		
		if resp.StatusCode != http.StatusOK && resp.StatusCode != http.StatusAccepted {
			return fmt.Errorf("unexpected status code: %d", resp.StatusCode)
		}
	}
	
	return nil
}

// writeMetric writes a single metric in Prometheus format.
func (pe *PrometheusExporter) writeMetric(w io.Writer, name string, value interface{}) {
	// Sanitize metric name for Prometheus
	name = strings.ReplaceAll(name, ".", "_")
	name = strings.ReplaceAll(name, "-", "_")
	
	// Build labels string
	var labelPairs []string
	for k, v := range pe.labels {
		labelPairs = append(labelPairs, fmt.Sprintf(`%s="%s"`, k, v))
	}
	labelStr := ""
	if len(labelPairs) > 0 {
		labelStr = "{" + strings.Join(labelPairs, ",") + "}"
	}
	
	// Write metric based on type
	switch v := value.(type) {
	case int, int64, uint64:
		fmt.Fprintf(w, "%s%s %v\n", name, labelStr, v)
	case float64, float32:
		fmt.Fprintf(w, "%s%s %.6f\n", name, labelStr, v)
	case bool:
		val := 0
		if v {
			val = 1
		}
		fmt.Fprintf(w, "%s%s %d\n", name, labelStr, val)
	}
}

// Format returns the export format name.
func (pe *PrometheusExporter) Format() string {
	return "prometheus"
}

// Close shuts down the exporter.
func (pe *PrometheusExporter) Close() error {
	pe.httpClient.CloseIdleConnections()
	return nil
}

// StatsDExporter exports metrics using StatsD protocol.
type StatsDExporter struct {
	address string
	prefix  string
	tags    map[string]string
	conn    net.Conn
	mu      sync.Mutex
}

// NewStatsDExporter creates a new StatsD exporter.
func NewStatsDExporter(address, prefix string, tags map[string]string) (*StatsDExporter, error) {
	conn, err := net.Dial("udp", address)
	if err != nil {
		return nil, fmt.Errorf("failed to connect to StatsD: %w", err)
	}
	
	return &StatsDExporter{
		address: address,
		prefix:  prefix,
		tags:    tags,
		conn:    conn,
	}, nil
}

// Export sends metrics using StatsD protocol.
func (se *StatsDExporter) Export(metrics map[string]interface{}) error {
	se.mu.Lock()
	defer se.mu.Unlock()
	
	for name, value := range metrics {
		if err := se.sendMetric(name, value); err != nil {
			// Log error but continue with other metrics
			_ = err
		}
	}
	
	return nil
}

// sendMetric sends a single metric to StatsD.
func (se *StatsDExporter) sendMetric(name string, value interface{}) error {
	// Prefix metric name
	if se.prefix != "" {
		name = se.prefix + "." + name
	}
	
	// Format metric based on type
	var metricStr string
	switch v := value.(type) {
	case int, int64, uint64:
		metricStr = fmt.Sprintf("%s:%v|c", name, v) // Counter
	case float64, float32:
		metricStr = fmt.Sprintf("%s:%v|g", name, v) // Gauge
	case time.Duration:
		metricStr = fmt.Sprintf("%s:%d|ms", name, v.Milliseconds()) // Timer
	default:
		return nil // Skip unsupported types
	}
	
	// Add tags if supported (DogStatsD format)
	if len(se.tags) > 0 {
		var tagPairs []string
		for k, v := range se.tags {
			tagPairs = append(tagPairs, fmt.Sprintf("%s:%s", k, v))
		}
		metricStr += "|#" + strings.Join(tagPairs, ",")
	}
	
	// Send to StatsD
	_, err := se.conn.Write([]byte(metricStr + "\n"))
	return err
}

// Format returns the export format name.
func (se *StatsDExporter) Format() string {
	return "statsd"
}

// Close shuts down the exporter.
func (se *StatsDExporter) Close() error {
	if se.conn != nil {
		return se.conn.Close()
	}
	return nil
}

// JSONExporter exports metrics in JSON format.
type JSONExporter struct {
	output   io.Writer
	metadata map[string]interface{}
	mu       sync.Mutex
}

// NewJSONExporter creates a new JSON exporter.
func NewJSONExporter(output io.Writer, metadata map[string]interface{}) *JSONExporter {
	return &JSONExporter{
		output:   output,
		metadata: metadata,
	}
}

// Export sends metrics in JSON format.
func (je *JSONExporter) Export(metrics map[string]interface{}) error {
	je.mu.Lock()
	defer je.mu.Unlock()
	
	// Combine metrics with metadata
	exportData := map[string]interface{}{
		"timestamp": time.Now().Unix(),
		"metrics":   metrics,
	}
	
	// Add metadata
	for k, v := range je.metadata {
		exportData[k] = v
	}
	
	// Encode to JSON
	encoder := json.NewEncoder(je.output)
	encoder.SetIndent("", "  ")
	
	return encoder.Encode(exportData)
}

// Format returns the export format name.
func (je *JSONExporter) Format() string {
	return "json"
}

// Close shuts down the exporter.
func (je *JSONExporter) Close() error {
	// Nothing to close for basic writer
	return nil
}

// MetricsRegistry manages multiple exporters and collectors.
type MetricsRegistry struct {
	exporters []MetricsExporter
	interval  time.Duration
	metrics   map[string]interface{}
	mu        sync.RWMutex
	done      chan struct{}
}

// NewMetricsRegistry creates a new metrics registry.
func NewMetricsRegistry(interval time.Duration) *MetricsRegistry {
	return &MetricsRegistry{
		exporters: make([]MetricsExporter, 0),
		interval:  interval,
		metrics:   make(map[string]interface{}),
		done:      make(chan struct{}),
	}
}

// AddExporter adds a new exporter to the registry.
func (mr *MetricsRegistry) AddExporter(exporter MetricsExporter) {
	mr.mu.Lock()
	defer mr.mu.Unlock()
	mr.exporters = append(mr.exporters, exporter)
}

// RecordMetric records a metric value.
func (mr *MetricsRegistry) RecordMetric(name string, value interface{}, tags map[string]string) {
	mr.mu.Lock()
	defer mr.mu.Unlock()
	
	// Store metric with tags as part of the key
	key := name
	if len(tags) > 0 {
		var tagPairs []string
		for k, v := range tags {
			tagPairs = append(tagPairs, fmt.Sprintf("%s=%s", k, v))
		}
		key = fmt.Sprintf("%s{%s}", name, strings.Join(tagPairs, ","))
	}
	
	mr.metrics[key] = value
}

// Start begins periodic metric export.
func (mr *MetricsRegistry) Start() {
	go func() {
		ticker := time.NewTicker(mr.interval)
		defer ticker.Stop()
		
		for {
			select {
			case <-ticker.C:
				mr.export()
			case <-mr.done:
				return
			}
		}
	}()
}

// export sends metrics to all registered exporters.
func (mr *MetricsRegistry) export() {
	mr.mu.RLock()
	// Create snapshot of metrics
	snapshot := make(map[string]interface{})
	for k, v := range mr.metrics {
		snapshot[k] = v
	}
	exporters := mr.exporters
	mr.mu.RUnlock()
	
	// Export to all backends
	for _, exporter := range exporters {
		if err := exporter.Export(snapshot); err != nil {
			// Log error (would use actual logger)
			_ = err
		}
	}
}

// Stop stops the metrics registry.
func (mr *MetricsRegistry) Stop() {
	close(mr.done)
	
	// Close all exporters
	mr.mu.Lock()
	defer mr.mu.Unlock()
	
	for _, exporter := range mr.exporters {
		_ = exporter.Close()
	}
}

// CustomMetrics provides typed methods for recording custom metrics.
type CustomMetrics struct {
	namespace string
	registry  *MetricsRegistry
	tags      map[string]string
	mu        sync.RWMutex
}

// NewCustomMetrics creates a new custom metrics recorder.
func NewCustomMetrics(namespace string, registry *MetricsRegistry) *CustomMetrics {
	return &CustomMetrics{
		namespace: namespace,
		registry:  registry,
		tags:      make(map[string]string),
	}
}

// WithTags returns a new CustomMetrics instance with additional tags.
func (cm *CustomMetrics) WithTags(tags map[string]string) *CustomMetrics {
	cm.mu.RLock()
	defer cm.mu.RUnlock()
	
	// Merge tags
	newTags := make(map[string]string)
	for k, v := range cm.tags {
		newTags[k] = v
	}
	for k, v := range tags {
		newTags[k] = v
	}
	
	return &CustomMetrics{
		namespace: cm.namespace,
		registry:  cm.registry,
		tags:      newTags,
	}
}

// Counter increments a counter metric.
func (cm *CustomMetrics) Counter(name string, value int64) {
	metricName := cm.buildMetricName(name)
	cm.registry.RecordMetric(metricName, value, cm.tags)
}

// Gauge sets a gauge metric to a specific value.
func (cm *CustomMetrics) Gauge(name string, value float64) {
	metricName := cm.buildMetricName(name)
	cm.registry.RecordMetric(metricName, value, cm.tags)
}

// Histogram records a value in a histogram.
func (cm *CustomMetrics) Histogram(name string, value float64) {
	metricName := cm.buildMetricName(name)
	cm.registry.RecordMetric(metricName+".histogram", value, cm.tags)
}

// Timer records a duration metric.
func (cm *CustomMetrics) Timer(name string, duration time.Duration) {
	metricName := cm.buildMetricName(name)
	cm.registry.RecordMetric(metricName+".timer", duration, cm.tags)
}

// Summary records a summary statistic.
func (cm *CustomMetrics) Summary(name string, value float64, quantiles map[float64]float64) {
	metricName := cm.buildMetricName(name)
	
	// Record the value
	cm.registry.RecordMetric(metricName, value, cm.tags)
	
	// Record quantiles
	for q, v := range quantiles {
		quantileTag := fmt.Sprintf("quantile=%.2f", q)
		tags := make(map[string]string)
		for k, v := range cm.tags {
			tags[k] = v
		}
		tags["quantile"] = quantileTag
		cm.registry.RecordMetric(metricName+".quantile", v, tags)
	}
}

// buildMetricName constructs the full metric name with namespace.
func (cm *CustomMetrics) buildMetricName(name string) string {
	if cm.namespace != "" {
		return cm.namespace + "." + name
	}
	return name
}

// MetricsContext provides context-based metric recording.
type MetricsContext struct {
	metrics *CustomMetrics
	ctx     context.Context
}

// NewMetricsContext creates a new metrics context.
func NewMetricsContext(ctx context.Context, metrics *CustomMetrics) *MetricsContext {
	return &MetricsContext{
		metrics: metrics,
		ctx:     ctx,
	}
}

// RecordDuration records the duration of an operation.
func (mc *MetricsContext) RecordDuration(name string, fn func() error) error {
	start := time.Now()
	err := fn()
	duration := time.Since(start)
	
	mc.metrics.Timer(name, duration)
	
	if err != nil {
		mc.metrics.Counter(name+".errors", 1)
	} else {
		mc.metrics.Counter(name+".success", 1)
	}
	
	return err
}

// RecordValue records a value with automatic type detection.
func (mc *MetricsContext) RecordValue(name string, value interface{}) {
	switch v := value.(type) {
	case int, int64, uint64:
		mc.metrics.Counter(name, v.(int64))
	case float64, float32:
		mc.metrics.Gauge(name, v.(float64))
	case time.Duration:
		mc.metrics.Timer(name, v)
	case bool:
		val := int64(0)
		if v {
			val = 1
		}
		mc.metrics.Counter(name, val)
	}
}

// contextKey is the type for context keys.
type contextKey string

const (
	// MetricsContextKey is the context key for custom metrics.
	MetricsContextKey contextKey = "custom_metrics"
)

// WithMetrics adds custom metrics to a context.
func WithMetrics(ctx context.Context, metrics *CustomMetrics) context.Context {
	return context.WithValue(ctx, MetricsContextKey, metrics)
}

// MetricsFromContext retrieves custom metrics from context.
func MetricsFromContext(ctx context.Context) (*CustomMetrics, bool) {
	metrics, ok := ctx.Value(MetricsContextKey).(*CustomMetrics)
	return metrics, ok
}

// FilterMetricsRecorder allows filters to record custom metrics.
type FilterMetricsRecorder struct {
	filter    string
	namespace string
	registry  *MetricsRegistry
	mu        sync.RWMutex
}

// NewFilterMetricsRecorder creates a new filter metrics recorder.
func NewFilterMetricsRecorder(filterName string, registry *MetricsRegistry) *FilterMetricsRecorder {
	return &FilterMetricsRecorder{
		filter:    filterName,
		namespace: "filter." + filterName,
		registry:  registry,
	}
}

// Record records a custom metric for the filter.
func (fmr *FilterMetricsRecorder) Record(metric string, value interface{}, tags map[string]string) {
	fmr.mu.RLock()
	defer fmr.mu.RUnlock()
	
	// Add filter tag
	if tags == nil {
		tags = make(map[string]string)
	}
	tags["filter"] = fmr.filter
	
	// Build full metric name
	metricName := fmr.namespace + "." + metric
	
	// Record to registry
	fmr.registry.RecordMetric(metricName, value, tags)
}

// StartTimer starts a timer for measuring operation duration.
func (fmr *FilterMetricsRecorder) StartTimer(operation string) func() {
	start := time.Now()
	return func() {
		duration := time.Since(start)
		fmr.Record(operation+".duration", duration, nil)
	}
}

// IncrementCounter increments a counter metric.
func (fmr *FilterMetricsRecorder) IncrementCounter(name string, delta int64, tags map[string]string) {
	fmr.Record(name, delta, tags)
}

// SetGauge sets a gauge metric.
func (fmr *FilterMetricsRecorder) SetGauge(name string, value float64, tags map[string]string) {
	fmr.Record(name, value, tags)
}

// RecordHistogram records a histogram value.
func (fmr *FilterMetricsRecorder) RecordHistogram(name string, value float64, tags map[string]string) {
	fmr.Record(name+".histogram", value, tags)
}

// MetricsAggregator aggregates metrics across multiple filters.
type MetricsAggregator struct {
	filters   map[string]*FilterMetrics
	chainName string
	mu        sync.RWMutex
}

// NewMetricsAggregator creates a new metrics aggregator.
func NewMetricsAggregator(chainName string) *MetricsAggregator {
	return &MetricsAggregator{
		filters:   make(map[string]*FilterMetrics),
		chainName: chainName,
	}
}

// FilterMetrics holds metrics for a single filter.
type FilterMetrics struct {
	Name           string
	ProcessedCount int64
	ErrorCount     int64
	TotalLatency   time.Duration
	MinLatency     time.Duration
	MaxLatency     time.Duration
	AvgLatency     time.Duration
	LastUpdated    time.Time
	CustomMetrics  map[string]interface{}
}

// AddFilter registers a filter for aggregation.
func (ma *MetricsAggregator) AddFilter(name string) {
	ma.mu.Lock()
	defer ma.mu.Unlock()
	
	if _, exists := ma.filters[name]; !exists {
		ma.filters[name] = &FilterMetrics{
			Name:          name,
			MinLatency:    time.Duration(1<<63 - 1), // Max duration
			CustomMetrics: make(map[string]interface{}),
			LastUpdated:   time.Now(),
		}
	}
}

// UpdateFilterMetrics updates metrics for a specific filter.
func (ma *MetricsAggregator) UpdateFilterMetrics(name string, latency time.Duration, error bool) {
	ma.mu.Lock()
	defer ma.mu.Unlock()
	
	filter, exists := ma.filters[name]
	if !exists {
		filter = &FilterMetrics{
			Name:          name,
			MinLatency:    time.Duration(1<<63 - 1),
			CustomMetrics: make(map[string]interface{}),
		}
		ma.filters[name] = filter
	}
	
	// Update counts
	filter.ProcessedCount++
	if error {
		filter.ErrorCount++
	}
	
	// Update latencies
	filter.TotalLatency += latency
	if latency < filter.MinLatency {
		filter.MinLatency = latency
	}
	if latency > filter.MaxLatency {
		filter.MaxLatency = latency
	}
	filter.AvgLatency = filter.TotalLatency / time.Duration(filter.ProcessedCount)
	filter.LastUpdated = time.Now()
}

// AggregatedMetrics represents chain-wide aggregated metrics.
type AggregatedMetrics struct {
	ChainName       string
	TotalProcessed  int64
	TotalErrors     int64
	ErrorRate       float64
	TotalLatency    time.Duration
	AverageLatency  time.Duration
	MinLatency      time.Duration
	MaxLatency      time.Duration
	FilterCount     int
	HealthScore     float64
	LastAggregation time.Time
	FilterMetrics   map[string]*FilterMetrics
}

// GetAggregatedMetrics calculates chain-wide statistics.
func (ma *MetricsAggregator) GetAggregatedMetrics() *AggregatedMetrics {
	ma.mu.RLock()
	defer ma.mu.RUnlock()
	
	agg := &AggregatedMetrics{
		ChainName:       ma.chainName,
		MinLatency:      time.Duration(1<<63 - 1),
		FilterCount:     len(ma.filters),
		LastAggregation: time.Now(),
		FilterMetrics:   make(map[string]*FilterMetrics),
	}
	
	// Aggregate across all filters
	for name, filter := range ma.filters {
		agg.TotalProcessed += filter.ProcessedCount
		agg.TotalErrors += filter.ErrorCount
		agg.TotalLatency += filter.TotalLatency
		
		if filter.MinLatency < agg.MinLatency {
			agg.MinLatency = filter.MinLatency
		}
		if filter.MaxLatency > agg.MaxLatency {
			agg.MaxLatency = filter.MaxLatency
		}
		
		// Copy filter metrics
		filterCopy := *filter
		agg.FilterMetrics[name] = &filterCopy
	}
	
	// Calculate derived metrics
	if agg.TotalProcessed > 0 {
		agg.ErrorRate = float64(agg.TotalErrors) / float64(agg.TotalProcessed)
		agg.AverageLatency = agg.TotalLatency / time.Duration(agg.TotalProcessed)
		
		// Calculate health score (0-100)
		// Based on error rate and latency
		errorScore := math.Max(0, 100*(1-agg.ErrorRate))
		
		// Latency score (assuming 1s is bad, 10ms is good)
		latencyMs := float64(agg.AverageLatency.Milliseconds())
		latencyScore := math.Max(0, 100*(1-latencyMs/1000))
		
		agg.HealthScore = (errorScore + latencyScore) / 2
	} else {
		agg.HealthScore = 100 // No data means healthy
	}
	
	return agg
}

// HierarchicalAggregator supports hierarchical metric aggregation.
type HierarchicalAggregator struct {
	root     *MetricsNode
	registry *MetricsRegistry
	mu       sync.RWMutex
}

// MetricsNode represents a node in the metrics hierarchy.
type MetricsNode struct {
	Name     string
	Level    int
	Metrics  map[string]interface{}
	Children []*MetricsNode
	Parent   *MetricsNode
}

// NewHierarchicalAggregator creates a new hierarchical aggregator.
func NewHierarchicalAggregator(rootName string, registry *MetricsRegistry) *HierarchicalAggregator {
	return &HierarchicalAggregator{
		root: &MetricsNode{
			Name:     rootName,
			Level:    0,
			Metrics:  make(map[string]interface{}),
			Children: make([]*MetricsNode, 0),
		},
		registry: registry,
	}
}

// AddNode adds a node to the hierarchy.
func (ha *HierarchicalAggregator) AddNode(path []string, metrics map[string]interface{}) {
	ha.mu.Lock()
	defer ha.mu.Unlock()
	
	current := ha.root
	for i, name := range path {
		found := false
		for _, child := range current.Children {
			if child.Name == name {
				current = child
				found = true
				break
			}
		}
		
		if !found {
			newNode := &MetricsNode{
				Name:     name,
				Level:    i + 1,
				Metrics:  make(map[string]interface{}),
				Children: make([]*MetricsNode, 0),
				Parent:   current,
			}
			current.Children = append(current.Children, newNode)
			current = newNode
		}
	}
	
	// Update metrics at the leaf node
	for k, v := range metrics {
		current.Metrics[k] = v
	}
}

// AggregateUp aggregates metrics from children to parents.
func (ha *HierarchicalAggregator) AggregateUp() {
	ha.mu.Lock()
	defer ha.mu.Unlock()
	
	ha.aggregateNode(ha.root)
}

// aggregateNode recursively aggregates metrics for a node.
func (ha *HierarchicalAggregator) aggregateNode(node *MetricsNode) map[string]interface{} {
	aggregated := make(map[string]interface{})
	
	// Start with node's own metrics
	for k, v := range node.Metrics {
		aggregated[k] = v
	}
	
	// Aggregate children's metrics
	for _, child := range node.Children {
		childMetrics := ha.aggregateNode(child)
		for k, v := range childMetrics {
			if existing, exists := aggregated[k]; exists {
				// Sum numeric values
				aggregated[k] = ha.sumValues(existing, v)
			} else {
				aggregated[k] = v
			}
		}
	}
	
	// Update node's aggregated metrics
	node.Metrics = aggregated
	
	return aggregated
}

// sumValues sums two metric values.
func (ha *HierarchicalAggregator) sumValues(a, b interface{}) interface{} {
	switch va := a.(type) {
	case int64:
		if vb, ok := b.(int64); ok {
			return va + vb
		}
	case float64:
		if vb, ok := b.(float64); ok {
			return va + vb
		}
	case time.Duration:
		if vb, ok := b.(time.Duration); ok {
			return va + vb
		}
	}
	return a // Return first value if types don't match
}

// GetHierarchicalMetrics returns the complete metrics hierarchy.
func (ha *HierarchicalAggregator) GetHierarchicalMetrics() *MetricsNode {
	ha.mu.RLock()
	defer ha.mu.RUnlock()
	
	return ha.copyNode(ha.root)
}

// copyNode creates a deep copy of a metrics node.
func (ha *HierarchicalAggregator) copyNode(node *MetricsNode) *MetricsNode {
	if node == nil {
		return nil
	}
	
	copy := &MetricsNode{
		Name:     node.Name,
		Level:    node.Level,
		Metrics:  make(map[string]interface{}),
		Children: make([]*MetricsNode, 0, len(node.Children)),
	}
	
	// Copy metrics
	for k, v := range node.Metrics {
		copy.Metrics[k] = v
	}
	
	// Copy children
	for _, child := range node.Children {
		copy.Children = append(copy.Children, ha.copyNode(child))
	}
	
	return copy
}

// RollingAggregator maintains rolling window aggregations.
type RollingAggregator struct {
	windowSize time.Duration
	buckets    []MetricBucket
	current    int
	mu         sync.RWMutex
}

// MetricBucket represents a time bucket for metrics.
type MetricBucket struct {
	Timestamp time.Time
	Metrics   map[string]interface{}
}

// NewRollingAggregator creates a new rolling window aggregator.
func NewRollingAggregator(windowSize time.Duration, bucketCount int) *RollingAggregator {
	buckets := make([]MetricBucket, bucketCount)
	for i := range buckets {
		buckets[i] = MetricBucket{
			Metrics: make(map[string]interface{}),
		}
	}
	
	return &RollingAggregator{
		windowSize: windowSize,
		buckets:    buckets,
		current:    0,
	}
}

// Record adds metrics to the current bucket.
func (ra *RollingAggregator) Record(metrics map[string]interface{}) {
	ra.mu.Lock()
	defer ra.mu.Unlock()
	
	now := time.Now()
	bucketDuration := ra.windowSize / time.Duration(len(ra.buckets))
	
	// Check if we need to advance to next bucket
	if now.Sub(ra.buckets[ra.current].Timestamp) > bucketDuration {
		ra.current = (ra.current + 1) % len(ra.buckets)
		ra.buckets[ra.current] = MetricBucket{
			Timestamp: now,
			Metrics:   make(map[string]interface{}),
		}
	}
	
	// Add metrics to current bucket
	for k, v := range metrics {
		if existing, exists := ra.buckets[ra.current].Metrics[k]; exists {
			ra.buckets[ra.current].Metrics[k] = ra.combineValues(existing, v)
		} else {
			ra.buckets[ra.current].Metrics[k] = v
		}
	}
}

// combineValues combines two metric values.
func (ra *RollingAggregator) combineValues(a, b interface{}) interface{} {
	switch va := a.(type) {
	case int64:
		if vb, ok := b.(int64); ok {
			return va + vb
		}
	case float64:
		if vb, ok := b.(float64); ok {
			return va + vb
		}
	case []float64:
		if vb, ok := b.(float64); ok {
			return append(va, vb)
		}
	}
	return b // Replace with new value if types don't match
}

// GetAggregated returns aggregated metrics for the rolling window.
func (ra *RollingAggregator) GetAggregated() map[string]interface{} {
	ra.mu.RLock()
	defer ra.mu.RUnlock()
	
	aggregated := make(map[string]interface{})
	cutoff := time.Now().Add(-ra.windowSize)
	
	for _, bucket := range ra.buckets {
		if bucket.Timestamp.After(cutoff) {
			for k, v := range bucket.Metrics {
				if existing, exists := aggregated[k]; exists {
					aggregated[k] = ra.combineValues(existing, v)
				} else {
					aggregated[k] = v
				}
			}
		}
	}
	
	return aggregated
}

// MetricsConfig configures metrics collection behavior.
type MetricsConfig struct {
	// Enabled determines if metrics collection is active
	Enabled bool
	
	// ExportInterval defines how often metrics are exported
	ExportInterval time.Duration
	
	// IncludeHistograms enables histogram metrics (more memory)
	IncludeHistograms bool
	
	// IncludePercentiles enables percentile calculations (P50, P90, P95, P99)
	IncludePercentiles bool
	
	// MetricPrefix is prepended to all metric names
	MetricPrefix string
	
	// Tags are added to all metrics for grouping/filtering
	Tags map[string]string
	
	// BufferSize for metric events (0 = unbuffered)
	BufferSize int
	
	// FlushOnClose ensures all metrics are exported on shutdown
	FlushOnClose bool
	
	// ErrorThreshold for alerting (percentage)
	ErrorThreshold float64
}

// DefaultMetricsConfig returns a sensible default configuration.
func DefaultMetricsConfig() MetricsConfig {
	return MetricsConfig{
		Enabled:            true,
		ExportInterval:     10 * time.Second,
		IncludeHistograms:  true,
		IncludePercentiles: true,
		MetricPrefix:       "filter",
		Tags:               make(map[string]string),
		BufferSize:         1000,
		FlushOnClose:       true,
	}
}

// MetricsFilter collects metrics for filter processing.
type MetricsFilter struct {
	*FilterBase
	
	// Metrics collector implementation
	collector MetricsCollector
	
	// Configuration
	config MetricsConfig
	
	// Statistics storage
	stats map[string]atomic.Value
	
	// Mutex for map access
	mu sync.RWMutex
}

// NewMetricsFilter creates a new metrics collection filter.
func NewMetricsFilter(config MetricsConfig, collector MetricsCollector) *MetricsFilter {
	f := &MetricsFilter{
		FilterBase: NewFilterBase("metrics", "monitoring"),
		collector:  collector,
		config:     config,
		stats:      make(map[string]atomic.Value),
	}
	
	// Start export timer if configured
	if config.Enabled && config.ExportInterval > 0 {
		go f.exportLoop()
	}
	
	return f
}

// Process implements the Filter interface with metrics collection.
func (f *MetricsFilter) Process(ctx context.Context, data []byte) (*types.FilterResult, error) {
	if !f.config.Enabled {
		// Pass through without metrics if disabled
		return types.ContinueWith(data), nil
	}
	
	// Record start time
	startTime := time.Now()
	
	// Get metric name from context or use default
	metricName := f.getMetricName(ctx)
	
	// Increment request counter
	f.collector.IncrementCounter(metricName+".requests", 1)
	
	// Process the actual data (would call next filter in real implementation)
	result, err := f.processNext(ctx, data)
	
	// Calculate duration
	duration := time.Since(startTime)
	
	// Record latency
	f.collector.RecordLatency(metricName+".latency", duration)
	
	// Track percentiles
	f.trackLatencyPercentiles(metricName, duration)
	
	// Record in histogram if enabled
	if f.config.IncludeHistograms {
		f.collector.RecordHistogram(metricName+".duration_ms", float64(duration.Milliseconds()))
	}
	
	// Track success/error rates
	if err != nil || (result != nil && result.Status == types.Error) {
		f.collector.IncrementCounter(metricName+".errors", 1)
		f.recordErrorRate(metricName, true)
	} else {
		f.collector.IncrementCounter(metricName+".success", 1)
		f.recordErrorRate(metricName, false)
	}
	
	// Track data size
	f.collector.RecordHistogram(metricName+".request_size", float64(len(data)))
	if result != nil && result.Data != nil {
		f.collector.RecordHistogram(metricName+".response_size", float64(len(result.Data)))
	}
	
	// Update throughput metrics
	f.updateThroughput(metricName, len(data))
	
	return result, err
}

// processNext simulates calling the next filter in the chain.
func (f *MetricsFilter) processNext(ctx context.Context, data []byte) (*types.FilterResult, error) {
	// In real implementation, this would delegate to the next filter
	return types.ContinueWith(data), nil
}

// getMetricName extracts metric name from context or returns default.
func (f *MetricsFilter) getMetricName(ctx context.Context) string {
	if name, ok := ctx.Value("metric_name").(string); ok {
		return f.config.MetricPrefix + "." + name
	}
	return f.config.MetricPrefix + ".default"
}

// recordErrorRate tracks error rate over time with categorization.
func (f *MetricsFilter) recordErrorRate(name string, isError bool) {
	key := name + ".error_rate"
	
	// Get or create error rate tracker
	var tracker *ErrorRateTracker
	if v, ok := f.stats[key]; ok {
		tracker = v.Load().(*ErrorRateTracker)
	} else {
		tracker = NewErrorRateTracker(f.config.ErrorThreshold)
		var v atomic.Value
		v.Store(tracker)
		f.mu.Lock()
		f.stats[key] = v
		f.mu.Unlock()
	}
	
	// Update tracker
	tracker.Record(isError)
	
	// Record as gauge
	f.collector.SetGauge(key, tracker.GetRate())
	
	// Check threshold breach
	if tracker.IsThresholdBreached() {
		f.collector.IncrementCounter(name+".error_threshold_breaches", 1)
		// Would trigger alert here
	}
}

// ErrorRateTracker tracks error rate with categorization.
type ErrorRateTracker struct {
	total           uint64
	errors          uint64
	errorsByType    map[string]uint64
	threshold       float64
	breachCount     uint64
	lastBreachTime  time.Time
	mu              sync.RWMutex
}

// NewErrorRateTracker creates a new error rate tracker.
func NewErrorRateTracker(threshold float64) *ErrorRateTracker {
	return &ErrorRateTracker{
		errorsByType: make(map[string]uint64),
		threshold:    threshold,
	}
}

// Record records a request outcome.
func (ert *ErrorRateTracker) Record(isError bool) {
	ert.mu.Lock()
	defer ert.mu.Unlock()
	
	ert.total++
	if isError {
		ert.errors++
	}
}

// RecordError records an error with type categorization.
func (ert *ErrorRateTracker) RecordError(errorType string) {
	ert.mu.Lock()
	defer ert.mu.Unlock()
	
	ert.total++
	ert.errors++
	ert.errorsByType[errorType]++
	
	// Check threshold
	if ert.GetRate() > ert.threshold {
		ert.breachCount++
		ert.lastBreachTime = time.Now()
	}
}

// GetRate returns the current error rate percentage.
func (ert *ErrorRateTracker) GetRate() float64 {
	if ert.total == 0 {
		return 0
	}
	return float64(ert.errors) / float64(ert.total) * 100.0
}

// IsThresholdBreached checks if error rate exceeds threshold.
func (ert *ErrorRateTracker) IsThresholdBreached() bool {
	return ert.GetRate() > ert.threshold
}

// GetErrorsByType returns error count by type.
func (ert *ErrorRateTracker) GetErrorsByType() map[string]uint64 {
	ert.mu.RLock()
	defer ert.mu.RUnlock()
	
	result := make(map[string]uint64)
	for k, v := range ert.errorsByType {
		result[k] = v
	}
	return result
}

// ThroughputTracker tracks throughput using sliding window.
type ThroughputTracker struct {
	requestsPerSec float64
	bytesPerSec    float64
	peakRPS        float64
	peakBPS        float64
	
	window      []throughputSample
	windowSize  time.Duration
	lastUpdate  time.Time
	mu          sync.RWMutex
}

type throughputSample struct {
	timestamp time.Time
	requests  int64
	bytes     int64
}

// NewThroughputTracker creates a new throughput tracker.
func NewThroughputTracker(windowSize time.Duration) *ThroughputTracker {
	return &ThroughputTracker{
		window:     make([]throughputSample, 0, 100),
		windowSize: windowSize,
		lastUpdate: time.Now(),
	}
}

// Add adds a sample to the tracker.
func (tt *ThroughputTracker) Add(requests, bytes int64) {
	tt.mu.Lock()
	defer tt.mu.Unlock()
	
	now := time.Now()
	tt.window = append(tt.window, throughputSample{
		timestamp: now,
		requests:  requests,
		bytes:     bytes,
	})
	
	// Clean old samples
	cutoff := now.Add(-tt.windowSize)
	newWindow := make([]throughputSample, 0, len(tt.window))
	for _, s := range tt.window {
		if s.timestamp.After(cutoff) {
			newWindow = append(newWindow, s)
		}
	}
	tt.window = newWindow
	
	// Calculate rates
	if len(tt.window) > 1 {
		duration := tt.window[len(tt.window)-1].timestamp.Sub(tt.window[0].timestamp).Seconds()
		if duration > 0 {
			var totalRequests, totalBytes int64
			for _, s := range tt.window {
				totalRequests += s.requests
				totalBytes += s.bytes
			}
			
			tt.requestsPerSec = float64(totalRequests) / duration
			tt.bytesPerSec = float64(totalBytes) / duration
			
			// Update peaks
			if tt.requestsPerSec > tt.peakRPS {
				tt.peakRPS = tt.requestsPerSec
			}
			if tt.bytesPerSec > tt.peakBPS {
				tt.peakBPS = tt.bytesPerSec
			}
		}
	}
}

// updateThroughput updates throughput metrics with sliding window.
func (f *MetricsFilter) updateThroughput(name string, bytes int) {
	key := name + ".throughput"
	
	// Get or create throughput tracker
	var tracker *ThroughputTracker
	if v, ok := f.stats[key]; ok {
		tracker = v.Load().(*ThroughputTracker)
	} else {
		tracker = NewThroughputTracker(10 * time.Second) // 10 second window
		var v atomic.Value
		v.Store(tracker)
		f.mu.Lock()
		f.stats[key] = v
		f.mu.Unlock()
	}
	
	// Add sample
	tracker.Add(1, int64(bytes))
	
	// Export metrics
	f.collector.SetGauge(name+".rps", tracker.requestsPerSec)
	f.collector.SetGauge(name+".bps", tracker.bytesPerSec)
	f.collector.SetGauge(name+".peak_rps", tracker.peakRPS)
	f.collector.SetGauge(name+".peak_bps", tracker.peakBPS)
}

// exportLoop periodically exports metrics.
func (f *MetricsFilter) exportLoop() {
	ticker := time.NewTicker(f.config.ExportInterval)
	defer ticker.Stop()
	
	for range ticker.C {
		if err := f.collector.Flush(); err != nil {
			// Log error (would use actual logger)
			_ = err
		}
	}
}

// errorRateTracker tracks error rate.
type errorRateTracker struct {
	total  uint64
	errors uint64
}

// PercentileTracker tracks latency percentiles.
type PercentileTracker struct {
	values []float64
	mu     sync.RWMutex
	sorted bool
}

// NewPercentileTracker creates a new percentile tracker.
func NewPercentileTracker() *PercentileTracker {
	return &PercentileTracker{
		values: make([]float64, 0, 1000),
	}
}

// Add adds a value to the tracker.
func (pt *PercentileTracker) Add(value float64) {
	pt.mu.Lock()
	defer pt.mu.Unlock()
	pt.values = append(pt.values, value)
	pt.sorted = false
}

// GetPercentile calculates the given percentile (0-100).
func (pt *PercentileTracker) GetPercentile(p float64) float64 {
	pt.mu.Lock()
	defer pt.mu.Unlock()
	
	if len(pt.values) == 0 {
		return 0
	}
	
	if !pt.sorted {
		// Sort values for percentile calculation
		for i := 0; i < len(pt.values); i++ {
			for j := i + 1; j < len(pt.values); j++ {
				if pt.values[i] > pt.values[j] {
					pt.values[i], pt.values[j] = pt.values[j], pt.values[i]
				}
			}
		}
		pt.sorted = true
	}
	
	index := int(float64(len(pt.values)-1) * p / 100.0)
	return pt.values[index]
}

// trackLatencyPercentiles tracks P50, P90, P95, P99.
func (f *MetricsFilter) trackLatencyPercentiles(name string, duration time.Duration) {
	if !f.config.IncludePercentiles {
		return
	}
	
	key := name + ".percentiles"
	
	// Get or create percentile tracker
	var tracker *PercentileTracker
	if v, ok := f.stats[key]; ok {
		tracker = v.Load().(*PercentileTracker)
	} else {
		tracker = NewPercentileTracker()
		var v atomic.Value
		v.Store(tracker)
		f.mu.Lock()
		f.stats[key] = v
		f.mu.Unlock()
	}
	
	// Add value
	tracker.Add(float64(duration.Microseconds()))
	
	// Export percentiles
	f.collector.SetGauge(name+".p50", tracker.GetPercentile(50))
	f.collector.SetGauge(name+".p90", tracker.GetPercentile(90))
	f.collector.SetGauge(name+".p95", tracker.GetPercentile(95))
	f.collector.SetGauge(name+".p99", tracker.GetPercentile(99))
}