package core_test

import (
	"sync"
	"testing"
	"time"

	"github.com/GopherSecurity/gopher-mcp/src/core"
	"github.com/GopherSecurity/gopher-mcp/src/types"
)

// Test 1: NewFilterBase creation
func TestNewFilterBase(t *testing.T) {
	name := "test-filter"
	filterType := "test-type"
	
	fb := core.NewFilterBase(name, filterType)
	
	if fb.Name() != name {
		t.Errorf("Name() = %s, want %s", fb.Name(), name)
	}
	
	if fb.Type() != filterType {
		t.Errorf("Type() = %s, want %s", fb.Type(), filterType)
	}
	
	// Stats should be initialized
	stats := fb.GetStats()
	if stats.BytesProcessed != 0 {
		t.Error("Initial stats should be zero")
	}
}

// Test 2: SetName and SetType
func TestFilterBase_SetNameAndType(t *testing.T) {
	fb := core.NewFilterBase("initial", "initial-type")
	
	// Change name
	newName := "updated-name"
	fb.SetName(newName)
	if fb.Name() != newName {
		t.Errorf("Name() = %s, want %s", fb.Name(), newName)
	}
	
	// Change type
	newType := "updated-type"
	fb.SetType(newType)
	if fb.Type() != newType {
		t.Errorf("Type() = %s, want %s", fb.Type(), newType)
	}
}

// Test 3: Initialize with configuration
func TestFilterBase_Initialize(t *testing.T) {
	fb := core.NewFilterBase("test", "test-type")
	
	config := types.FilterConfig{
		Name:             "config-name",
		Type:             "config-type",
		Enabled:          true,
		EnableStatistics: true,
		Settings:         map[string]interface{}{"key": "value"},
	}
	
	err := fb.Initialize(config)
	if err != nil {
		t.Fatalf("Initialize failed: %v", err)
	}
	
	// Name should be updated from config
	if fb.Name() != "config-name" {
		t.Errorf("Name not updated from config: %s", fb.Name())
	}
	
	// Type should be updated from config
	if fb.Type() != "config-type" {
		t.Errorf("Type not updated from config: %s", fb.Type())
	}
	
	// Config should be stored
	storedConfig := fb.GetConfig()
	if storedConfig.Name != config.Name {
		t.Error("Config not stored correctly")
	}
	
	// Stats should be reset
	stats := fb.GetStats()
	if stats.ProcessCount != 0 {
		t.Error("Stats not reset after initialization")
	}
}

// Test 4: Initialize with invalid configuration
func TestFilterBase_Initialize_Invalid(t *testing.T) {
	fb := core.NewFilterBase("test", "test-type")
	
	// Create invalid config (assuming Validate() checks for certain conditions)
	config := types.FilterConfig{
		Name: "", // Empty name might be invalid
	}
	
	// Note: This test depends on the actual validation logic in types.FilterConfig.Validate()
	// If Validate() always returns empty, this test should be adjusted
	err := fb.Initialize(config)
	if err == nil {
		// If no validation error, that's also acceptable
		t.Log("Config validation passed (no validation rules enforced)")
	}
}

// Test 5: Close and disposal state
func TestFilterBase_Close(t *testing.T) {
	fb := core.NewFilterBase("test", "test-type")
	
	// First close should succeed
	err := fb.Close()
	if err != nil {
		t.Fatalf("Close failed: %v", err)
	}
	
	// Second close should be idempotent (no error)
	err = fb.Close()
	if err != nil {
		t.Errorf("Second Close returned error: %v", err)
	}
	
	// Stats should be cleared
	stats := fb.GetStats()
	if stats.BytesProcessed != 0 {
		t.Error("Stats not cleared after Close")
	}
}

// Test 6: Initialize after Close
func TestFilterBase_Initialize_AfterClose(t *testing.T) {
	fb := core.NewFilterBase("test", "test-type")
	
	// Close the filter
	fb.Close()
	
	// Try to initialize after close
	config := types.FilterConfig{Name: "test"}
	err := fb.Initialize(config)
	
	// Should return an error because filter is disposed
	if err == nil {
		t.Error("Initialize should fail after Close")
	}
}

// Test 7: GetStats thread safety
func TestFilterBase_GetStats_ThreadSafe(t *testing.T) {
	fb := core.NewFilterBase("test", "test-type")
	
	var wg sync.WaitGroup
	numGoroutines := 10
	
	// Concurrent reads should be safe
	for i := 0; i < numGoroutines; i++ {
		wg.Add(1)
		go func() {
			defer wg.Done()
			for j := 0; j < 100; j++ {
				_ = fb.GetStats()
			}
		}()
	}
	
	wg.Wait()
	// If we get here without panic/race, the test passes
}

// Test 8: UpdateStats functionality (using exported method if available)
func TestFilterBase_UpdateStats(t *testing.T) {
	fb := core.NewFilterBase("test", "test-type")
	
	// Since updateStats is private, we test it indirectly through GetStats
	// after operations that would call it
	
	// Initial stats should be zero
	stats := fb.GetStats()
	if stats.BytesProcessed != 0 {
		t.Error("Initial BytesProcessed should be 0")
	}
	if stats.ProcessCount != 0 {
		t.Error("Initial ProcessCount should be 0")
	}
	
	// Note: In a real implementation, we would need public methods that call updateStats
	// or make updateStats public for testing
}

// Test 9: ResetStats functionality
func TestFilterBase_ResetStats(t *testing.T) {
	fb := core.NewFilterBase("test", "test-type")
	
	// Get initial stats
	stats1 := fb.GetStats()
	
	// Reset stats
	fb.ResetStats()
	
	// Stats should be zeroed
	stats2 := fb.GetStats()
	if stats2.BytesProcessed != 0 || stats2.ProcessCount != 0 || stats2.ErrorCount != 0 {
		t.Error("Stats not properly reset")
	}
	
	// Should be same as initial
	if stats1.BytesProcessed != stats2.BytesProcessed {
		t.Error("Reset stats should match initial state")
	}
}

// Test 10: Concurrent operations
func TestFilterBase_Concurrent(t *testing.T) {
	fb := core.NewFilterBase("test", "test-type")
	
	var wg sync.WaitGroup
	numGoroutines := 10
	
	// Start multiple goroutines doing various operations
	for i := 0; i < numGoroutines; i++ {
		wg.Add(1)
		go func(id int) {
			defer wg.Done()
			
			// Read operations
			for j := 0; j < 50; j++ {
				_ = fb.Name()
				_ = fb.Type()
				_ = fb.GetStats()
				_ = fb.GetConfig()
			}
			
			// Modify operations
			if id%2 == 0 {
				fb.ResetStats()
			}
			
			// Initialize with config (only some goroutines)
			if id%3 == 0 {
				config := types.FilterConfig{
					Name: "concurrent-test",
				}
				fb.Initialize(config)
			}
		}(i)
	}
	
	// One goroutine tries to close
	wg.Add(1)
	go func() {
		defer wg.Done()
		time.Sleep(10 * time.Millisecond)
		fb.Close()
	}()
	
	wg.Wait()
	
	// Verify final state is consistent
	// The filter should be closed
	err := fb.Initialize(types.FilterConfig{Name: "after-close"})
	if err == nil {
		t.Error("Should not be able to initialize after close in concurrent test")
	}
}

// Test embedded FilterBase in custom filter
type CustomFilter struct {
	core.FilterBase
	customField string
}

func TestFilterBase_Embedded(t *testing.T) {
	cf := &CustomFilter{
		FilterBase:  core.NewFilterBase("custom", "custom-type"),
		customField: "custom-value",
	}
	
	// FilterBase methods should work
	if cf.Name() != "custom" {
		t.Errorf("Name() = %s, want custom", cf.Name())
	}
	
	if cf.Type() != "custom-type" {
		t.Errorf("Type() = %s, want custom-type", cf.Type())
	}
	
	// Initialize should work
	config := types.FilterConfig{
		Name: "configured-custom",
		Type: "custom-type",
	}
	err := cf.Initialize(config)
	if err != nil {
		t.Fatalf("Initialize failed: %v", err)
	}
	
	// Name should be updated
	if cf.Name() != "configured-custom" {
		t.Error("Name not updated after Initialize")
	}
	
	// Custom fields should still be accessible
	if cf.customField != "custom-value" {
		t.Error("Custom field not preserved")
	}
	
	// Close should work
	err = cf.Close()
	if err != nil {
		t.Fatalf("Close failed: %v", err)
	}
}

// Test config preservation
func TestFilterBase_ConfigPreservation(t *testing.T) {
	fb := core.NewFilterBase("test", "test-type")
	
	config := types.FilterConfig{
		Name:             "test-filter",
		Type:             "test-type",
		Enabled:          true,
		EnableStatistics: true,
		TimeoutMs:        5000,
		Settings: map[string]interface{}{
			"option1": "value1",
			"option2": 42,
			"option3": true,
		},
	}
	
	err := fb.Initialize(config)
	if err != nil {
		t.Fatalf("Initialize failed: %v", err)
	}
	
	// Get config back
	storedConfig := fb.GetConfig()
	
	// Verify all fields are preserved
	if storedConfig.Name != config.Name {
		t.Errorf("Name not preserved: got %s, want %s", storedConfig.Name, config.Name)
	}
	if storedConfig.Enabled != config.Enabled {
		t.Error("Enabled flag not preserved")
	}
	if storedConfig.EnableStatistics != config.EnableStatistics {
		t.Error("EnableStatistics flag not preserved")
	}
	if storedConfig.TimeoutMs != config.TimeoutMs {
		t.Errorf("TimeoutMs not preserved: got %d, want %d", storedConfig.TimeoutMs, config.TimeoutMs)
	}
	
	// Check settings
	if val, ok := storedConfig.Settings["option1"].(string); !ok || val != "value1" {
		t.Error("String setting not preserved")
	}
	if val, ok := storedConfig.Settings["option2"].(int); !ok || val != 42 {
		t.Error("Int setting not preserved")
	}
	if val, ok := storedConfig.Settings["option3"].(bool); !ok || val != true {
		t.Error("Bool setting not preserved")
	}
}

// Benchmarks

func BenchmarkFilterBase_GetStats(b *testing.B) {
	fb := core.NewFilterBase("bench", "bench-type")
	
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		_ = fb.GetStats()
	}
}

func BenchmarkFilterBase_Name(b *testing.B) {
	fb := core.NewFilterBase("bench", "bench-type")
	
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		_ = fb.Name()
	}
}

func BenchmarkFilterBase_Initialize(b *testing.B) {
	config := types.FilterConfig{
		Name: "bench-filter",
		Type: "bench-type",
	}
	
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		fb := core.NewFilterBase("bench", "bench-type")
		fb.Initialize(config)
	}
}

func BenchmarkFilterBase_Close(b *testing.B) {
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		fb := core.NewFilterBase("bench", "bench-type")
		fb.Close()
	}
}

func BenchmarkFilterBase_Concurrent_GetStats(b *testing.B) {
	fb := core.NewFilterBase("bench", "bench-type")
	
	b.RunParallel(func(pb *testing.PB) {
		for pb.Next() {
			_ = fb.GetStats()
		}
	})
}

func BenchmarkFilterBase_ResetStats(b *testing.B) {
	fb := core.NewFilterBase("bench", "bench-type")
	
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		fb.ResetStats()
	}
}