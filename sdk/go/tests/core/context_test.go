package core_test

import (
	"context"
	"strings"
	"sync"
	"testing"
	"time"

	"github.com/GopherSecurity/gopher-mcp/src/core"
)

// Test 1: NewProcessingContext creation
func TestNewProcessingContext(t *testing.T) {
	parent := context.Background()
	ctx := core.NewProcessingContext(parent)

	if ctx == nil {
		t.Fatal("NewProcessingContext returned nil")
	}

	// Check context is properly embedded
	if ctx.Done() != parent.Done() {
		t.Error("Context not properly embedded")
	}

	// Check metrics collector is initialized
	metrics := ctx.GetMetrics()
	if metrics == nil {
		t.Error("Metrics not initialized")
	}
}

// Test 2: WithCorrelationID
func TestWithCorrelationID(t *testing.T) {
	parent := context.Background()
	correlationID := "test-correlation-123"

	ctx := core.WithCorrelationID(parent, correlationID)

	if ctx == nil {
		t.Fatal("WithCorrelationID returned nil")
	}

	if ctx.CorrelationID() != correlationID {
		t.Errorf("CorrelationID = %s, want %s", ctx.CorrelationID(), correlationID)
	}
}

// Test 3: SetProperty and GetProperty
func TestProcessingContext_Properties(t *testing.T) {
	ctx := core.NewProcessingContext(context.Background())

	// Set various types of properties
	ctx.SetProperty("string", "value")
	ctx.SetProperty("int", 42)
	ctx.SetProperty("bool", true)
	ctx.SetProperty("nil", nil)

	// Get properties
	tests := []struct {
		key      string
		expected interface{}
		exists   bool
	}{
		{"string", "value", true},
		{"int", 42, true},
		{"bool", true, true},
		{"nil", nil, true},
		{"missing", nil, false},
	}

	for _, tt := range tests {
		val, ok := ctx.GetProperty(tt.key)
		if ok != tt.exists {
			t.Errorf("GetProperty(%s) exists = %v, want %v", tt.key, ok, tt.exists)
		}
		if ok && val != tt.expected {
			t.Errorf("GetProperty(%s) = %v, want %v", tt.key, val, tt.expected)
		}
	}

	// Test empty key
	ctx.SetProperty("", "should not be stored")
	_, ok := ctx.GetProperty("")
	if ok {
		t.Error("Empty key should not be stored")
	}
}

// Test 4: Typed getters (GetString, GetInt, GetBool)
func TestProcessingContext_TypedGetters(t *testing.T) {
	ctx := core.NewProcessingContext(context.Background())

	ctx.SetProperty("string", "hello")
	ctx.SetProperty("int", 123)
	ctx.SetProperty("bool", true)
	ctx.SetProperty("wrong_type", 3.14)

	// Test GetString
	if str, ok := ctx.GetString("string"); !ok || str != "hello" {
		t.Errorf("GetString failed: got %s, %v", str, ok)
	}
	if _, ok := ctx.GetString("int"); ok {
		t.Error("GetString should fail for non-string")
	}
	if _, ok := ctx.GetString("missing"); ok {
		t.Error("GetString should fail for missing key")
	}

	// Test GetInt
	if val, ok := ctx.GetInt("int"); !ok || val != 123 {
		t.Errorf("GetInt failed: got %d, %v", val, ok)
	}
	if _, ok := ctx.GetInt("string"); ok {
		t.Error("GetInt should fail for non-int")
	}

	// Test GetBool
	if val, ok := ctx.GetBool("bool"); !ok || val != true {
		t.Errorf("GetBool failed: got %v, %v", val, ok)
	}
	if _, ok := ctx.GetBool("string"); ok {
		t.Error("GetBool should fail for non-bool")
	}
}

// Test 5: Value method (context.Context interface)
func TestProcessingContext_Value(t *testing.T) {
	// Create parent context with value
	type contextKey string
	parentKey := contextKey("parent")
	parent := context.WithValue(context.Background(), parentKey, "parent-value")

	ctx := core.NewProcessingContext(parent)
	ctx.SetProperty("prop", "prop-value")

	// Should find parent context value
	if val := ctx.Value(parentKey); val != "parent-value" {
		t.Errorf("Value from parent = %v, want parent-value", val)
	}

	// Should find property value
	if val := ctx.Value("prop"); val != "prop-value" {
		t.Errorf("Value from property = %v, want prop-value", val)
	}

	// Should return nil for missing
	if val := ctx.Value("missing"); val != nil {
		t.Errorf("Value for missing = %v, want nil", val)
	}
}

// Test 6: CorrelationID generation
func TestProcessingContext_CorrelationID_Generation(t *testing.T) {
	ctx := core.NewProcessingContext(context.Background())

	// First call should generate ID
	id1 := ctx.CorrelationID()
	if id1 == "" {
		t.Error("CorrelationID should generate non-empty ID")
	}

	// Should be hex string (UUID-like)
	if len(id1) != 32 {
		t.Errorf("CorrelationID length = %d, want 32", len(id1))
	}

	// Second call should return same ID
	id2 := ctx.CorrelationID()
	if id1 != id2 {
		t.Error("CorrelationID should be stable")
	}

	// SetCorrelationID should update
	newID := "custom-id-456"
	ctx.SetCorrelationID(newID)
	if ctx.CorrelationID() != newID {
		t.Errorf("CorrelationID = %s, want %s", ctx.CorrelationID(), newID)
	}
}

// Test 7: Metrics recording
func TestProcessingContext_Metrics(t *testing.T) {
	ctx := core.NewProcessingContext(context.Background())

	// Record metrics
	ctx.RecordMetric("latency", 100.5)
	ctx.RecordMetric("throughput", 1000)
	ctx.RecordMetric("errors", 2)

	// Get metrics
	metrics := ctx.GetMetrics()
	if metrics == nil {
		t.Fatal("GetMetrics returned nil")
	}

	// Check values
	if metrics["latency"] != 100.5 {
		t.Errorf("latency = %f, want 100.5", metrics["latency"])
	}
	if metrics["throughput"] != 1000 {
		t.Errorf("throughput = %f, want 1000", metrics["throughput"])
	}
	if metrics["errors"] != 2 {
		t.Errorf("errors = %f, want 2", metrics["errors"])
	}

	// Update metric
	ctx.RecordMetric("errors", 3)
	metrics = ctx.GetMetrics()
	if metrics["errors"] != 3 {
		t.Errorf("Updated errors = %f, want 3", metrics["errors"])
	}
}

// Test 8: Clone context
func TestProcessingContext_Clone(t *testing.T) {
	parent := context.Background()
	ctx := core.WithCorrelationID(parent, "original-id")

	// Set properties and metrics
	ctx.SetProperty("key1", "value1")
	ctx.SetProperty("key2", 42)
	ctx.RecordMetric("metric1", 100)

	// Clone
	cloned := ctx.Clone()

	// Check correlation ID is copied
	if cloned.CorrelationID() != ctx.CorrelationID() {
		t.Error("Correlation ID not copied")
	}

	// Check properties are copied
	val1, _ := cloned.GetProperty("key1")
	if val1 != "value1" {
		t.Error("Properties not copied correctly")
	}

	// Metrics should be fresh (empty)
	metrics := cloned.GetMetrics()
	if len(metrics) != 0 {
		t.Error("Clone should have fresh metrics")
	}

	// Modifications to clone should not affect original
	cloned.SetProperty("key3", "value3")
	if _, ok := ctx.GetProperty("key3"); ok {
		t.Error("Clone modifications affected original")
	}
}

// Test 9: WithTimeout and WithDeadline
func TestProcessingContext_TimeoutDeadline(t *testing.T) {
	ctx := core.NewProcessingContext(context.Background())
	ctx.SetProperty("original", true)

	// Test WithTimeout
	timeout := 100 * time.Millisecond
	timeoutCtx := ctx.WithTimeout(timeout)

	// Properties should be copied
	if val, _ := timeoutCtx.GetProperty("original"); val != true {
		t.Error("Properties not copied in WithTimeout")
	}

	// Context should have deadline
	_, ok := timeoutCtx.Deadline()
	if !ok {
		t.Error("WithTimeout should set deadline")
	}

	// Test WithDeadline
	futureTime := time.Now().Add(200 * time.Millisecond)
	deadlineCtx := ctx.WithDeadline(futureTime)

	// Properties should be copied
	if val, _ := deadlineCtx.GetProperty("original"); val != true {
		t.Error("Properties not copied in WithDeadline")
	}

	// Check deadline is set
	dl, ok := deadlineCtx.Deadline()
	if !ok || !dl.Equal(futureTime) {
		t.Error("WithDeadline not set correctly")
	}
}

// Test 10: Concurrent property access
func TestProcessingContext_Concurrent(t *testing.T) {
	ctx := core.NewProcessingContext(context.Background())

	var wg sync.WaitGroup
	numGoroutines := 10
	opsPerGoroutine := 100

	// Concurrent writes
	for i := 0; i < numGoroutines; i++ {
		wg.Add(1)
		go func(id int) {
			defer wg.Done()
			for j := 0; j < opsPerGoroutine; j++ {
				key := strings.Repeat("k", id+1) // Different key per goroutine
				ctx.SetProperty(key, id*1000+j)
				ctx.RecordMetric(key, float64(j))
			}
		}(i)
	}

	// Concurrent reads
	for i := 0; i < numGoroutines; i++ {
		wg.Add(1)
		go func(id int) {
			defer wg.Done()
			for j := 0; j < opsPerGoroutine; j++ {
				key := strings.Repeat("k", id+1)
				ctx.GetProperty(key)
				ctx.GetMetrics()
				ctx.CorrelationID()
			}
		}(i)
	}

	wg.Wait()

	// Verify some values exist
	for i := 0; i < numGoroutines; i++ {
		key := strings.Repeat("k", i+1)
		if _, ok := ctx.GetProperty(key); !ok {
			t.Errorf("Property %s not found after concurrent access", key)
		}
	}
}

// Test MetricsCollector separately

func TestNewMetricsCollector(t *testing.T) {
	mc := core.NewMetricsCollector()
	if mc == nil {
		t.Fatal("NewMetricsCollector returned nil")
	}

	// Should start empty
	all := mc.All()
	if len(all) != 0 {
		t.Error("New collector should be empty")
	}
}

func TestMetricsCollector_RecordAndGet(t *testing.T) {
	mc := core.NewMetricsCollector()

	// Record metrics
	mc.Record("cpu", 75.5)
	mc.Record("memory", 1024)

	// Get existing metric
	val, ok := mc.Get("cpu")
	if !ok || val != 75.5 {
		t.Errorf("Get(cpu) = %f, %v, want 75.5, true", val, ok)
	}

	// Get non-existing metric
	val, ok = mc.Get("missing")
	if ok || val != 0 {
		t.Errorf("Get(missing) = %f, %v, want 0, false", val, ok)
	}

	// Update existing metric
	mc.Record("cpu", 80.0)
	val, _ = mc.Get("cpu")
	if val != 80.0 {
		t.Errorf("Updated cpu = %f, want 80.0", val)
	}
}

func TestMetricsCollector_All(t *testing.T) {
	mc := core.NewMetricsCollector()

	// Record multiple metrics
	mc.Record("metric1", 1.0)
	mc.Record("metric2", 2.0)
	mc.Record("metric3", 3.0)

	// Get all metrics
	all := mc.All()
	if len(all) != 3 {
		t.Errorf("All() returned %d metrics, want 3", len(all))
	}

	// Verify values
	if all["metric1"] != 1.0 {
		t.Errorf("metric1 = %f, want 1.0", all["metric1"])
	}
	if all["metric2"] != 2.0 {
		t.Errorf("metric2 = %f, want 2.0", all["metric2"])
	}

	// Modifying returned map should not affect internal state
	all["metric1"] = 999
	val, _ := mc.Get("metric1")
	if val != 1.0 {
		t.Error("All() should return a copy")
	}
}

func TestMetricsCollector_Concurrent(t *testing.T) {
	mc := core.NewMetricsCollector()

	var wg sync.WaitGroup
	numGoroutines := 10

	for i := 0; i < numGoroutines; i++ {
		wg.Add(1)
		go func(id int) {
			defer wg.Done()
			for j := 0; j < 100; j++ {
				mc.Record("shared", float64(id*100+j))
				mc.Get("shared")
				mc.All()
			}
		}(i)
	}

	wg.Wait()

	// Should have the metric
	if _, ok := mc.Get("shared"); !ok {
		t.Error("Metric not found after concurrent access")
	}
}

// Benchmarks

func BenchmarkProcessingContext_SetProperty(b *testing.B) {
	ctx := core.NewProcessingContext(context.Background())

	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		ctx.SetProperty("key", i)
	}
}

func BenchmarkProcessingContext_GetProperty(b *testing.B) {
	ctx := core.NewProcessingContext(context.Background())
	ctx.SetProperty("key", "value")

	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		ctx.GetProperty("key")
	}
}

func BenchmarkProcessingContext_RecordMetric(b *testing.B) {
	ctx := core.NewProcessingContext(context.Background())

	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		ctx.RecordMetric("metric", float64(i))
	}
}

func BenchmarkProcessingContext_Clone(b *testing.B) {
	ctx := core.NewProcessingContext(context.Background())
	for i := 0; i < 10; i++ {
		ctx.SetProperty("key"+string(rune('0'+i)), i)
	}

	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		_ = ctx.Clone()
	}
}

func BenchmarkProcessingContext_Concurrent(b *testing.B) {
	ctx := core.NewProcessingContext(context.Background())

	b.RunParallel(func(pb *testing.PB) {
		i := 0
		for pb.Next() {
			if i%2 == 0 {
				ctx.SetProperty("key", i)
			} else {
				ctx.GetProperty("key")
			}
			i++
		}
	})
}
