package filters_test

import (
	"bytes"
	"errors"
	"fmt"
	"strings"
	"sync"
	"testing"
	"time"

	"github.com/GopherSecurity/gopher-mcp/src/filters"
)

// Test 1: PrometheusExporter creation and format
func TestPrometheusExporter(t *testing.T) {
	labels := map[string]string{
		"service": "test",
		"env":     "test",
	}

	exporter := filters.NewPrometheusExporter("", labels)

	if exporter == nil {
		t.Fatal("NewPrometheusExporter returned nil")
	}

	if exporter.Format() != "prometheus" {
		t.Errorf("Format() = %s, want prometheus", exporter.Format())
	}

	// Test export without endpoint (should not error)
	metrics := map[string]interface{}{
		"test_counter": int64(10),
		"test_gauge":   float64(3.14),
	}

	err := exporter.Export(metrics)
	if err != nil {
		t.Errorf("Export failed: %v", err)
	}

	// Clean up
	exporter.Close()
}

// Test 2: JSONExporter with metadata
func TestJSONExporter(t *testing.T) {
	var buf bytes.Buffer
	metadata := map[string]interface{}{
		"version": "1.0",
		"service": "test",
	}

	exporter := filters.NewJSONExporter(&buf, metadata)

	if exporter.Format() != "json" {
		t.Errorf("Format() = %s, want json", exporter.Format())
	}

	// Export metrics
	metrics := map[string]interface{}{
		"requests": int64(100),
		"latency":  float64(25.5),
		"success":  true,
	}

	err := exporter.Export(metrics)
	if err != nil {
		t.Fatalf("Export failed: %v", err)
	}

	// Check output contains expected fields
	output := buf.String()
	if !strings.Contains(output, "timestamp") {
		t.Error("Output should contain timestamp")
	}
	if !strings.Contains(output, "metrics") {
		t.Error("Output should contain metrics")
	}
	if !strings.Contains(output, "version") {
		t.Error("Output should contain version metadata")
	}

	exporter.Close()
}

// Test 3: MetricsRegistry with multiple exporters
func TestMetricsRegistry(t *testing.T) {
	registry := filters.NewMetricsRegistry(100 * time.Millisecond)

	// Add exporters
	var buf1, buf2 bytes.Buffer
	jsonExporter := filters.NewJSONExporter(&buf1, nil)
	jsonExporter2 := filters.NewJSONExporter(&buf2, nil)

	registry.AddExporter(jsonExporter)
	registry.AddExporter(jsonExporter2)

	// Record metrics
	registry.RecordMetric("test.counter", int64(42), nil)
	registry.RecordMetric("test.gauge", float64(3.14), map[string]string{"tag": "value"})

	// Start export
	registry.Start()

	// Wait for export
	time.Sleep(150 * time.Millisecond)

	// Stop registry
	registry.Stop()

	// Both buffers should have data
	if buf1.Len() == 0 {
		t.Error("First exporter should have exported data")
	}
	if buf2.Len() == 0 {
		t.Error("Second exporter should have exported data")
	}
}

// Test 4: CustomMetrics with namespace and tags
func TestCustomMetrics(t *testing.T) {
	registry := filters.NewMetricsRegistry(1 * time.Second)
	cm := filters.NewCustomMetrics("myapp", registry)

	// Record different metric types
	cm.Counter("requests", 100)
	cm.Gauge("connections", 25.5)
	cm.Histogram("latency", 150.0)
	cm.Timer("duration", 500*time.Millisecond)

	// Test WithTags
	tagged := cm.WithTags(map[string]string{
		"endpoint": "/api",
		"method":   "GET",
	})

	tagged.Counter("tagged_requests", 50)

	// Verify metrics were recorded
	// (Would need access to registry internals to fully verify)

	registry.Stop()
}

// Test 5: Summary metrics with quantiles
func TestCustomMetrics_Summary(t *testing.T) {
	registry := filters.NewMetricsRegistry(1 * time.Second)
	cm := filters.NewCustomMetrics("test", registry)

	quantiles := map[float64]float64{
		0.5:  100.0,
		0.95: 200.0,
		0.99: 300.0,
	}

	cm.Summary("response_time", 150.0, quantiles)

	// Metrics should be recorded
	// (Would need access to registry internals to verify)

	registry.Stop()
}

// Test 6: MetricsContext with duration recording
func TestMetricsContext(t *testing.T) {
	registry := filters.NewMetricsRegistry(1 * time.Second)
	cm := filters.NewCustomMetrics("test", registry)
	mc := filters.NewMetricsContext(nil, cm)

	// Record successful operation
	err := mc.RecordDuration("operation", func() error {
		time.Sleep(10 * time.Millisecond)
		return nil
	})

	if err != nil {
		t.Errorf("RecordDuration returned error: %v", err)
	}

	// Record failed operation
	expectedErr := errors.New("test error")
	err = mc.RecordDuration("failed_operation", func() error {
		return expectedErr
	})

	if err != expectedErr {
		t.Errorf("RecordDuration should return the operation error")
	}

	registry.Stop()
}

// Test 7: Concurrent metric recording
func TestMetricsRegistry_Concurrent(t *testing.T) {
	registry := filters.NewMetricsRegistry(100 * time.Millisecond)

	var wg sync.WaitGroup

	// Multiple goroutines recording metrics
	for i := 0; i < 10; i++ {
		wg.Add(1)
		go func(id int) {
			defer wg.Done()
			for j := 0; j < 100; j++ {
				registry.RecordMetric(
					fmt.Sprintf("metric_%d", id),
					int64(j),
					map[string]string{"goroutine": fmt.Sprintf("%d", id)},
				)
			}
		}(i)
	}

	wg.Wait()

	// No panic should occur
	registry.Stop()
}

// Test 8: Metric name sanitization for Prometheus
func TestPrometheusExporter_MetricSanitization(t *testing.T) {
	exporter := filters.NewPrometheusExporter("", nil)

	// This would require access to writeMetric method
	// which is private, so we test indirectly
	metrics := map[string]interface{}{
		"test.metric-name": int64(10),
		"another-metric":   float64(20.5),
	}

	// Export should sanitize names
	err := exporter.Export(metrics)
	if err != nil {
		t.Errorf("Export failed: %v", err)
	}

	exporter.Close()
}

// Test 9: MetricsRegistry export interval
func TestMetricsRegistry_ExportInterval(t *testing.T) {
	var exportCount int
	var mu sync.Mutex

	// Create a custom exporter that counts exports
	countExporter := &countingExporter{
		count: &exportCount,
		mu:    &mu,
	}

	registry := filters.NewMetricsRegistry(50 * time.Millisecond)
	registry.AddExporter(countExporter)

	registry.RecordMetric("test", int64(1), nil)
	registry.Start()

	// Wait for multiple export intervals
	time.Sleep(220 * time.Millisecond)

	registry.Stop()

	mu.Lock()
	count := exportCount
	mu.Unlock()

	// Should have exported at least 3 times (200ms / 50ms)
	if count < 3 {
		t.Errorf("Export count = %d, want at least 3", count)
	}
}

// Test 10: Multiple tag handling
func TestCustomMetrics_MultipleTags(t *testing.T) {
	registry := filters.NewMetricsRegistry(1 * time.Second)
	cm := filters.NewCustomMetrics("app", registry)

	// Create metrics with different tag combinations
	tags1 := map[string]string{"env": "prod", "region": "us-east"}
	tags2 := map[string]string{"env": "prod", "region": "us-west"}
	tags3 := map[string]string{"env": "dev", "region": "us-east"}

	cm1 := cm.WithTags(tags1)
	cm2 := cm.WithTags(tags2)
	cm3 := cm.WithTags(tags3)

	// Record same metric with different tags
	cm1.Counter("requests", 100)
	cm2.Counter("requests", 200)
	cm3.Counter("requests", 50)

	// Each should be recorded separately
	// (Would need registry internals to verify)

	registry.Stop()
}

// Helper types for testing

type countingExporter struct {
	count *int
	mu    *sync.Mutex
}

func (ce *countingExporter) Export(metrics map[string]interface{}) error {
	ce.mu.Lock()
	defer ce.mu.Unlock()
	*ce.count++
	return nil
}

func (ce *countingExporter) Format() string {
	return "counting"
}

func (ce *countingExporter) Close() error {
	return nil
}

// Benchmarks

func BenchmarkMetricsRegistry_RecordMetric(b *testing.B) {
	registry := filters.NewMetricsRegistry(1 * time.Second)

	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		registry.RecordMetric("bench_metric", int64(i), nil)
	}

	registry.Stop()
}

func BenchmarkCustomMetrics_Counter(b *testing.B) {
	registry := filters.NewMetricsRegistry(1 * time.Second)
	cm := filters.NewCustomMetrics("bench", registry)

	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		cm.Counter("counter", int64(i))
	}

	registry.Stop()
}

func BenchmarkJSONExporter_Export(b *testing.B) {
	var buf bytes.Buffer
	exporter := filters.NewJSONExporter(&buf, nil)

	metrics := map[string]interface{}{
		"metric1": int64(100),
		"metric2": float64(3.14),
		"metric3": true,
	}

	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		buf.Reset()
		exporter.Export(metrics)
	}

	exporter.Close()
}

func BenchmarkPrometheusExporter_Export(b *testing.B) {
	exporter := filters.NewPrometheusExporter("", nil)

	metrics := map[string]interface{}{
		"metric1": int64(100),
		"metric2": float64(3.14),
		"metric3": int64(42),
	}

	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		exporter.Export(metrics)
	}

	exporter.Close()
}
