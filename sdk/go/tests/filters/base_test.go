package filters_test

import (
	"fmt"
	"sync"
	"testing"
	"time"

	"github.com/GopherSecurity/gopher-mcp/src/filters"
	"github.com/GopherSecurity/gopher-mcp/src/types"
)

// Test 1: NewFilterBase creation
func TestNewFilterBase(t *testing.T) {
	name := "test-filter"
	filterType := "test-type"
	
	fb := filters.NewFilterBase(name, filterType)
	
	if fb == nil {
		t.Fatal("NewFilterBase returned nil")
	}
	
	if fb.Name() != name {
		t.Errorf("Name() = %s, want %s", fb.Name(), name)
	}
	
	if fb.Type() != filterType {
		t.Errorf("Type() = %s, want %s", fb.Type(), filterType)
	}
	
	if fb.IsDisposed() {
		t.Error("New filter should not be disposed")
	}
}

// Test 2: Initialize with valid config
func TestFilterBase_Initialize(t *testing.T) {
	fb := filters.NewFilterBase("test", "type")
	
	config := types.FilterConfig{
		Name:             "configured-name",
		Type:             "configured-type",
		Enabled:          true,
		EnableStatistics: true,
		Settings:         map[string]interface{}{"key": "value"},
	}
	
	err := fb.Initialize(config)
	if err != nil {
		t.Fatalf("Initialize failed: %v", err)
	}
	
	// Name and type should be updated
	if fb.Name() != "configured-name" {
		t.Errorf("Name not updated: %s", fb.Name())
	}
	
	if fb.Type() != "configured-type" {
		t.Errorf("Type not updated: %s", fb.Type())
	}
}

// Test 3: Initialize twice should fail
func TestFilterBase_Initialize_Twice(t *testing.T) {
	fb := filters.NewFilterBase("test", "type")
	
	config := types.FilterConfig{
		Name: "test",
		Type: "type",
	}
	
	// First initialization
	err := fb.Initialize(config)
	if err != nil {
		t.Fatalf("First Initialize failed: %v", err)
	}
	
	// Second initialization should fail
	err = fb.Initialize(config)
	if err == nil {
		t.Error("Second Initialize should fail")
	}
}

// Test 4: Close and disposal
func TestFilterBase_Close(t *testing.T) {
	fb := filters.NewFilterBase("test", "type")
	
	// Close should succeed
	err := fb.Close()
	if err != nil {
		t.Fatalf("Close failed: %v", err)
	}
	
	if !fb.IsDisposed() {
		t.Error("Filter should be disposed after Close")
	}
	
	// Second close should be idempotent
	err = fb.Close()
	if err != nil {
		t.Error("Second Close should not return error")
	}
}

// Test 5: Operations after disposal
func TestFilterBase_DisposedOperations(t *testing.T) {
	fb := filters.NewFilterBase("test", "type")
	fb.Close()
	
	// Name should return empty string when disposed
	if fb.Name() != "" {
		t.Error("Name() should return empty string when disposed")
	}
	
	// Type should return empty string when disposed
	if fb.Type() != "" {
		t.Error("Type() should return empty string when disposed")
	}
	
	// GetStats should return empty stats when disposed
	stats := fb.GetStats()
	if stats.BytesProcessed != 0 {
		t.Error("GetStats() should return empty stats when disposed")
	}
	
	// Initialize should fail when disposed
	config := types.FilterConfig{Name: "test", Type: "type"}
	err := fb.Initialize(config)
	if err != filters.ErrFilterDisposed {
		t.Errorf("Initialize should return ErrFilterDisposed, got %v", err)
	}
}

// Test 6: ThrowIfDisposed
func TestFilterBase_ThrowIfDisposed(t *testing.T) {
	fb := filters.NewFilterBase("test", "type")
	
	// Should not throw when not disposed
	err := fb.ThrowIfDisposed()
	if err != nil {
		t.Errorf("ThrowIfDisposed returned error when not disposed: %v", err)
	}
	
	// Close the filter
	fb.Close()
	
	// Should throw when disposed
	err = fb.ThrowIfDisposed()
	if err != filters.ErrFilterDisposed {
		t.Errorf("ThrowIfDisposed should return ErrFilterDisposed, got %v", err)
	}
}

// Test 7: GetStats with calculations
func TestFilterBase_GetStats(t *testing.T) {
	fb := filters.NewFilterBase("test", "type")
	
	// Initial stats should be zero
	stats := fb.GetStats()
	if stats.BytesProcessed != 0 || stats.ProcessCount != 0 {
		t.Error("Initial stats should be zero")
	}
	
	// Note: updateStats is private, so we can't test it directly
	// In a real scenario, this would be tested through the filter implementations
}

// Test 8: Concurrent Name and Type access
func TestFilterBase_ConcurrentAccess(t *testing.T) {
	fb := filters.NewFilterBase("test", "type")
	
	var wg sync.WaitGroup
	numGoroutines := 100
	
	// Concurrent reads
	for i := 0; i < numGoroutines; i++ {
		wg.Add(1)
		go func() {
			defer wg.Done()
			for j := 0; j < 100; j++ {
				_ = fb.Name()
				_ = fb.Type()
				_ = fb.GetStats()
				_ = fb.IsDisposed()
			}
		}()
	}
	
	// One goroutine does initialization
	wg.Add(1)
	go func() {
		defer wg.Done()
		config := types.FilterConfig{
			Name: "concurrent-test",
			Type: "concurrent-type",
		}
		fb.Initialize(config)
	}()
	
	wg.Wait()
	
	// Verify filter is still in valid state
	if fb.IsDisposed() {
		t.Error("Filter should not be disposed")
	}
}

// Test 9: Initialize with empty config
func TestFilterBase_Initialize_EmptyConfig(t *testing.T) {
	fb := filters.NewFilterBase("original", "original-type")
	
	config := types.FilterConfig{}
	
	err := fb.Initialize(config)
	// Depending on validation, this might succeed or fail
	// The test ensures it doesn't panic
	if err == nil {
		// If it succeeded, original values should be preserved
		if fb.Name() != "original" && fb.Name() != "" {
			t.Error("Name should be preserved or empty")
		}
	}
}

// Test 10: Concurrent Close
func TestFilterBase_ConcurrentClose(t *testing.T) {
	fb := filters.NewFilterBase("test", "type")
	
	var wg sync.WaitGroup
	numGoroutines := 10
	
	// Multiple goroutines try to close
	for i := 0; i < numGoroutines; i++ {
		wg.Add(1)
		go func() {
			defer wg.Done()
			fb.Close()
		}()
	}
	
	wg.Wait()
	
	// Filter should be disposed
	if !fb.IsDisposed() {
		t.Error("Filter should be disposed after concurrent closes")
	}
}

// Custom filter implementation for testing
type TestFilter struct {
	*filters.FilterBase
	processCount int
	mu           sync.Mutex
}

func NewTestFilter(name string) *TestFilter {
	return &TestFilter{
		FilterBase: filters.NewFilterBase(name, "test"),
	}
}

func (tf *TestFilter) Process(data []byte) error {
	if err := tf.ThrowIfDisposed(); err != nil {
		return err
	}
	
	tf.mu.Lock()
	tf.processCount++
	tf.mu.Unlock()
	
	return nil
}

// Test 11: Embedded FilterBase
func TestFilterBase_Embedded(t *testing.T) {
	tf := NewTestFilter("embedded-test")
	
	// FilterBase methods should work
	if tf.Name() != "embedded-test" {
		t.Errorf("Name() = %s, want embedded-test", tf.Name())
	}
	
	if tf.Type() != "test" {
		t.Errorf("Type() = %s, want test", tf.Type())
	}
	
	// Process some data
	err := tf.Process([]byte("test data"))
	if err != nil {
		t.Fatalf("Process failed: %v", err)
	}
	
	// Close the filter
	tf.Close()
	
	// Process should fail after close
	err = tf.Process([]byte("more data"))
	if err != filters.ErrFilterDisposed {
		t.Errorf("Process should return ErrFilterDisposed after close, got %v", err)
	}
}

// Test 12: Stats calculation accuracy
func TestFilterBase_StatsCalculation(t *testing.T) {
	// This test validates the stats calculation logic
	// Since updateStats is private, we test the calculation logic
	// through GetStats return values
	
	fb := filters.NewFilterBase("stats-test", "type")
	
	// Get initial stats
	stats := fb.GetStats()
	
	// Verify derived metrics are calculated correctly
	if stats.ProcessCount == 0 && stats.AverageProcessingTimeUs != 0 {
		t.Error("AverageProcessingTimeUs should be 0 when ProcessCount is 0")
	}
	
	if stats.ProcessCount == 0 && stats.ErrorRate != 0 {
		t.Error("ErrorRate should be 0 when ProcessCount is 0")
	}
	
	if stats.ProcessingTimeUs == 0 && stats.ThroughputBps != 0 {
		t.Error("ThroughputBps should be 0 when ProcessingTimeUs is 0")
	}
}

// Benchmarks

func BenchmarkFilterBase_Name(b *testing.B) {
	fb := filters.NewFilterBase("bench", "type")
	
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		_ = fb.Name()
	}
}

func BenchmarkFilterBase_GetStats(b *testing.B) {
	fb := filters.NewFilterBase("bench", "type")
	
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		_ = fb.GetStats()
	}
}

func BenchmarkFilterBase_IsDisposed(b *testing.B) {
	fb := filters.NewFilterBase("bench", "type")
	
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		_ = fb.IsDisposed()
	}
}

func BenchmarkFilterBase_Concurrent(b *testing.B) {
	fb := filters.NewFilterBase("bench", "type")
	
	b.RunParallel(func(pb *testing.PB) {
		for pb.Next() {
			_ = fb.Name()
			_ = fb.Type()
			_ = fb.GetStats()
		}
	})
}

// Test 13: Initialize with nil configuration
func TestFilterBase_Initialize_NilConfig(t *testing.T) {
	fb := filters.NewFilterBase("test", "type")
	
	// Initialize with mostly nil/empty values
	config := types.FilterConfig{
		Settings: nil,
	}
	
	err := fb.Initialize(config)
	// Should handle nil settings gracefully
	if err != nil {
		// Check if error is expected
		if fb.Name() == "" {
			// Name might be cleared on error
			t.Log("Initialize with nil config resulted in error:", err)
		}
	}
}

// Test 14: Filter type validation
func TestFilterBase_TypeValidation(t *testing.T) {
	validTypes := []string{
		"authentication",
		"authorization",
		"validation",
		"transformation",
		"encryption",
		"logging",
		"monitoring",
		"custom",
	}
	
	for _, filterType := range validTypes {
		fb := filters.NewFilterBase("test", filterType)
		if fb.Type() != filterType {
			t.Errorf("Type not set correctly for %s", filterType)
		}
	}
}

// Test 15: Stats with high volume
func TestFilterBase_HighVolumeStats(t *testing.T) {
	fb := filters.NewFilterBase("volume-test", "type")
	
	// Simulate high volume processing
	var wg sync.WaitGroup
	numGoroutines := 10
	iterationsPerGoroutine := 100
	
	for i := 0; i < numGoroutines; i++ {
		wg.Add(1)
		go func() {
			defer wg.Done()
			for j := 0; j < iterationsPerGoroutine; j++ {
				// Simulate getting stats under load
				_ = fb.GetStats()
			}
		}()
	}
	
	wg.Wait()
	
	// Verify filter is still operational
	if fb.IsDisposed() {
		t.Error("Filter should not be disposed after high volume operations")
	}
}

// Test 16: Multiple Close calls
func TestFilterBase_MultipleClose(t *testing.T) {
	fb := filters.NewFilterBase("multi-close", "type")
	
	// Close multiple times
	for i := 0; i < 5; i++ {
		err := fb.Close()
		if i == 0 && err != nil {
			t.Errorf("First close failed: %v", err)
		}
		// Subsequent closes should be idempotent
	}
	
	if !fb.IsDisposed() {
		t.Error("Filter should be disposed")
	}
}

// Test 17: Name length limits
func TestFilterBase_NameLengthLimits(t *testing.T) {
	tests := []struct {
		name string
		desc string
	}{
		{"", "empty name"},
		{"a", "single char"},
		{string(make([]byte, 255)), "max typical length"},
		{string(make([]byte, 1000)), "very long name"},
	}
	
	for _, test := range tests {
		fb := filters.NewFilterBase(test.name, "type")
		if fb.Name() != test.name {
			t.Errorf("Name not preserved for %s", test.desc)
		}
		fb.Close()
	}
}

// Test 18: Concurrent initialization and disposal
func TestFilterBase_ConcurrentInitDispose(t *testing.T) {
	fb := filters.NewFilterBase("concurrent", "type")
	
	var wg sync.WaitGroup
	wg.Add(2)
	
	// One goroutine tries to initialize
	go func() {
		defer wg.Done()
		config := types.FilterConfig{
			Name: "configured",
			Type: "configured-type",
		}
		fb.Initialize(config)
	}()
	
	// Another tries to close
	go func() {
		defer wg.Done()
		// Small delay to create race condition
		time.Sleep(time.Microsecond)
		fb.Close()
	}()
	
	wg.Wait()
	
	// Filter should be in one of the valid states
	if !fb.IsDisposed() {
		// If not disposed, name should be set
		if fb.Name() == "" {
			t.Error("Filter in invalid state")
		}
	}
}

// Test 19: Configuration with special characters
func TestFilterBase_SpecialCharConfig(t *testing.T) {
	fb := filters.NewFilterBase("test", "type")
	
	config := types.FilterConfig{
		Name:     "filter-with-special-chars!@#$%^&*()",
		Type:     "type/with/slashes",
		Settings: map[string]interface{}{
			"key with spaces": "value",
			"unicode-key-♠♣♥♦": "unicode-value-αβγδ",
		},
	}
	
	err := fb.Initialize(config)
	if err != nil {
		t.Fatalf("Initialize failed: %v", err)
	}
	
	// Verify special characters are preserved
	if fb.Name() != config.Name {
		t.Error("Special characters in name not preserved")
	}
	
	if fb.Type() != config.Type {
		t.Error("Special characters in type not preserved")
	}
}

// Test 20: Memory stress test
func TestFilterBase_MemoryStress(t *testing.T) {
	// Create and dispose many filters
	var filterList []*filters.FilterBase
	
	// Create filters
	for i := 0; i < 100; i++ {
		fb := filters.NewFilterBase(
			fmt.Sprintf("stress_%d", i),
			fmt.Sprintf("type_%d", i),
		)
		filterList = append(filterList, fb)
	}
	
	// Initialize them all
	for i, fb := range filterList {
		config := types.FilterConfig{
			Name:             fmt.Sprintf("configured_%d", i),
			Type:             fmt.Sprintf("configured_type_%d", i),
			Enabled:          i%2 == 0,
			EnableStatistics: i%3 == 0,
		}
		fb.Initialize(config)
	}
	
	// Access them concurrently
	var wg sync.WaitGroup
	for _, fb := range filterList {
		wg.Add(1)
		go func(f *filters.FilterBase) {
			defer wg.Done()
			for j := 0; j < 10; j++ {
				_ = f.Name()
				_ = f.Type()
				_ = f.GetStats()
			}
		}(fb)
	}
	wg.Wait()
	
	// Dispose them all
	for _, fb := range filterList {
		fb.Close()
	}
	
	// Verify all are disposed
	for i, fb := range filterList {
		if !fb.IsDisposed() {
			t.Errorf("Filter %d not disposed", i)
		}
	}
}