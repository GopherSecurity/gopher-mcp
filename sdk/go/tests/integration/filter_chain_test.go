package integration_test

import (
	"errors"
	"sync"
	"sync/atomic"
	"testing"
	"time"

	"github.com/GopherSecurity/gopher-mcp/src/integration"
)

// Mock filter implementation for testing
type mockFilter struct {
	id          string
	name        string
	filterType  string
	version     string
	description string
	processFunc func([]byte) ([]byte, error)
	config      map[string]interface{}
	stateless   bool
}

func (m *mockFilter) GetID() string                                { return m.id }
func (m *mockFilter) GetName() string                              { return m.name }
func (m *mockFilter) GetType() string                              { return m.filterType }
func (m *mockFilter) GetVersion() string                           { return m.version }
func (m *mockFilter) GetDescription() string                       { return m.description }
func (m *mockFilter) ValidateConfig() error                        { return nil }
func (m *mockFilter) GetConfiguration() map[string]interface{}     { return m.config }
func (m *mockFilter) UpdateConfig(cfg map[string]interface{})      { m.config = cfg }
func (m *mockFilter) GetCapabilities() []string                    { return []string{"filter", "transform"} }
func (m *mockFilter) GetDependencies() []integration.FilterDependency { return nil }
func (m *mockFilter) GetResourceRequirements() integration.ResourceRequirements {
	return integration.ResourceRequirements{Memory: 1024, CPUCores: 1}
}
func (m *mockFilter) GetTypeInfo() integration.TypeInfo {
	return integration.TypeInfo{
		InputTypes:  []string{"bytes"},
		OutputTypes: []string{"bytes"},
	}
}
func (m *mockFilter) EstimateLatency() time.Duration       { return 10 * time.Millisecond }
func (m *mockFilter) HasBlockingOperations() bool          { return false }
func (m *mockFilter) UsesDeprecatedFeatures() bool         { return false }
func (m *mockFilter) HasKnownVulnerabilities() bool        { return false }
func (m *mockFilter) IsStateless() bool                    { return m.stateless }
func (m *mockFilter) SetID(id string)                      { m.id = id }
func (m *mockFilter) Clone() integration.Filter {
	return &mockFilter{
		id:          m.id + "_clone",
		name:        m.name,
		filterType:  m.filterType,
		version:     m.version,
		description: m.description,
		processFunc: m.processFunc,
		config:      m.config,
		stateless:   m.stateless,
	}
}

func (m *mockFilter) Process(data []byte) ([]byte, error) {
	if m.processFunc != nil {
		return m.processFunc(data)
	}
	return data, nil
}

// Test 1: Create new filter chain
func TestNewFilterChain(t *testing.T) {
	chain := integration.NewFilterChain()
	
	if chain == nil {
		t.Fatal("NewFilterChain returned nil")
	}
	
	if chain.GetID() == "" {
		t.Error("Chain should have an ID")
	}
	
	if chain.GetFilterCount() != 0 {
		t.Errorf("New chain should have 0 filters, got %d", chain.GetFilterCount())
	}
	
	if chain.GetMode() != integration.SequentialMode {
		t.Error("Default mode should be sequential")
	}
}

// Test 2: Add filters to chain
func TestFilterChain_Add(t *testing.T) {
	chain := integration.NewFilterChain()
	
	filter1 := &mockFilter{
		id:   "filter1",
		name: "test_filter_1",
	}
	
	filter2 := &mockFilter{
		id:   "filter2",
		name: "test_filter_2",
	}
	
	// Add filters
	err := chain.Add(filter1)
	if err != nil {
		t.Fatalf("Failed to add filter1: %v", err)
	}
	
	err = chain.Add(filter2)
	if err != nil {
		t.Fatalf("Failed to add filter2: %v", err)
	}
	
	if chain.GetFilterCount() != 2 {
		t.Errorf("Chain should have 2 filters, got %d", chain.GetFilterCount())
	}
}

// Test 3: Remove filter from chain
func TestFilterChain_Remove(t *testing.T) {
	chain := integration.NewFilterChain()
	
	filter := &mockFilter{
		id:   "filter1",
		name: "test_filter",
	}
	
	chain.Add(filter)
	
	// Remove filter
	err := chain.Remove("filter1")
	if err != nil {
		t.Fatalf("Failed to remove filter: %v", err)
	}
	
	if chain.GetFilterCount() != 0 {
		t.Error("Chain should be empty after removal")
	}
	
	// Try to remove non-existent filter
	err = chain.Remove("non_existent")
	if err == nil {
		t.Error("Removing non-existent filter should return error")
	}
}

// Test 4: Process data through chain (sequential)
func TestFilterChain_ProcessSequential(t *testing.T) {
	chain := integration.NewFilterChain()
	chain.SetMode(integration.SequentialMode)
	
	// Add filters that append to data
	filter1 := &mockFilter{
		id:   "filter1",
		name: "append_A",
		processFunc: func(data []byte) ([]byte, error) {
			return append(data, 'A'), nil
		},
	}
	
	filter2 := &mockFilter{
		id:   "filter2",
		name: "append_B",
		processFunc: func(data []byte) ([]byte, error) {
			return append(data, 'B'), nil
		},
	}
	
	chain.Add(filter1)
	chain.Add(filter2)
	
	// Process data
	input := []byte("test")
	output, err := chain.Process(input)
	if err != nil {
		t.Fatalf("Process failed: %v", err)
	}
	
	expected := "testAB"
	if string(output) != expected {
		t.Errorf("Output = %s, want %s", string(output), expected)
	}
}

// Test 5: Process with filter error
func TestFilterChain_ProcessWithError(t *testing.T) {
	chain := integration.NewFilterChain()
	
	// Add filter that returns error
	errorFilter := &mockFilter{
		id:   "error_filter",
		name: "error",
		processFunc: func(data []byte) ([]byte, error) {
			return nil, errors.New("filter error")
		},
	}
	
	chain.Add(errorFilter)
	
	// Process should fail
	_, err := chain.Process([]byte("test"))
	if err == nil {
		t.Error("Process should return error from filter")
	}
}

// Test 6: Chain configuration
func TestFilterChain_Configuration(t *testing.T) {
	chain := integration.NewFilterChain()
	
	// Set various configurations
	chain.SetName("test_chain")
	chain.SetDescription("Test filter chain")
	chain.SetTimeout(5 * time.Second)
	chain.SetMaxFilters(10)
	chain.SetCacheEnabled(true)
	chain.SetCacheTTL(1 * time.Minute)
	
	// Verify configurations
	if chain.GetName() != "test_chain" {
		t.Errorf("Name = %s, want test_chain", chain.GetName())
	}
	
	if chain.GetDescription() != "Test filter chain" {
		t.Error("Description not set correctly")
	}
	
	if chain.GetTimeout() != 5*time.Second {
		t.Error("Timeout not set correctly")
	}
	
	if chain.GetMaxFilters() != 10 {
		t.Error("MaxFilters not set correctly")
	}
	
	if !chain.IsCacheEnabled() {
		t.Error("Cache should be enabled")
	}
}

// Test 7: Chain tags
func TestFilterChain_Tags(t *testing.T) {
	chain := integration.NewFilterChain()
	
	// Add tags
	chain.AddTag("env", "test")
	chain.AddTag("version", "1.0")
	
	// Get tags
	tags := chain.GetTags()
	if tags["env"] != "test" {
		t.Error("env tag not set correctly")
	}
	if tags["version"] != "1.0" {
		t.Error("version tag not set correctly")
	}
	
	// Remove tag
	chain.RemoveTag("env")
	tags = chain.GetTags()
	if _, exists := tags["env"]; exists {
		t.Error("env tag should be removed")
	}
}

// Test 8: Chain hooks
func TestFilterChain_Hooks(t *testing.T) {
	chain := integration.NewFilterChain()
	
	hookCalled := false
	
	// Add hook
	chain.AddHook(func(data []byte, stage string) {
		hookCalled = true
		// We can track data and stage if needed
		_ = data
		_ = stage
	})
	
	// Add a simple filter
	filter := &mockFilter{
		id:   "filter1",
		name: "test",
		processFunc: func(data []byte) ([]byte, error) {
			return data, nil
		},
	}
	chain.Add(filter)
	
	// Process data
	input := []byte("test")
	chain.Process(input)
	
	// Verify hook was called
	if !hookCalled {
		t.Error("Hook should be called during processing")
	}
}

// Test 9: Clone filter chain
func TestFilterChain_Clone(t *testing.T) {
	chain := integration.NewFilterChain()
	chain.SetName("original")
	chain.AddTag("test", "true")
	
	// Add filters
	filter := &mockFilter{
		id:   "filter1",
		name: "test_filter",
	}
	chain.Add(filter)
	
	// Clone chain
	cloned := chain.Clone()
	
	if cloned.GetID() == chain.GetID() {
		t.Error("Cloned chain should have different ID")
	}
	
	if cloned.GetName() != chain.GetName() {
		t.Error("Cloned chain should have same name")
	}
	
	if cloned.GetFilterCount() != chain.GetFilterCount() {
		t.Error("Cloned chain should have same number of filters")
	}
}

// Test 10: Validate filter chain
func TestFilterChain_Validate(t *testing.T) {
	chain := integration.NewFilterChain()
	
	// Empty chain should be valid
	err := chain.Validate()
	if err != nil {
		t.Errorf("Empty chain validation failed: %v", err)
	}
	
	// Add valid filter
	filter := &mockFilter{
		id:   "filter1",
		name: "valid_filter",
	}
	chain.Add(filter)
	
	// Should still be valid
	err = chain.Validate()
	if err != nil {
		t.Errorf("Valid chain validation failed: %v", err)
	}
}

// Test 11: Chain execution modes
func TestFilterChain_ExecutionModes(t *testing.T) {
	tests := []struct {
		name string
		mode integration.ExecutionMode
	}{
		{"Sequential", integration.SequentialMode},
		{"Parallel", integration.ParallelMode},
		{"Pipeline", integration.PipelineMode},
	}
	
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			chain := integration.NewFilterChain()
			chain.SetMode(tt.mode)
			
			if chain.GetMode() != tt.mode {
				t.Errorf("Mode = %v, want %v", chain.GetMode(), tt.mode)
			}
		})
	}
}

// Test 12: Max filters limit
func TestFilterChain_MaxFiltersLimit(t *testing.T) {
	chain := integration.NewFilterChain()
	chain.SetMaxFilters(2)
	
	// Add filters up to limit
	filter1 := &mockFilter{id: "1", name: "filter1"}
	filter2 := &mockFilter{id: "2", name: "filter2"}
	filter3 := &mockFilter{id: "3", name: "filter3"}
	
	err := chain.Add(filter1)
	if err != nil {
		t.Error("Should add first filter")
	}
	
	err = chain.Add(filter2)
	if err != nil {
		t.Error("Should add second filter")
	}
	
	err = chain.Add(filter3)
	if err == nil {
		t.Error("Should not add filter beyond limit")
	}
}

// Test 13: Chain retry policy
func TestFilterChain_RetryPolicy(t *testing.T) {
	chain := integration.NewFilterChain()
	
	policy := integration.RetryPolicy{
		MaxRetries:     3,
		InitialBackoff: 100 * time.Millisecond,
		BackoffFactor:  2.0,
	}
	
	chain.SetRetryPolicy(policy)
	
	// Test that retry policy is set (actual retry logic would be implemented in Process)
	// For now, just test that the filter fails as expected
	filter := &mockFilter{
		id:   "retry_filter",
		name: "retry",
		processFunc: func(data []byte) ([]byte, error) {
			return nil, errors.New("temporary error")
		},
	}
	
	chain.Add(filter)
	
	// Process should fail (retry not implemented yet)
	_, err := chain.Process([]byte("test"))
	if err == nil {
		t.Error("Expected error from failing filter")
	}
}

// Test 14: Chain timeout
func TestFilterChain_Timeout(t *testing.T) {
	chain := integration.NewFilterChain()
	chain.SetTimeout(50 * time.Millisecond)
	
	// Test that timeout is set correctly
	if chain.GetTimeout() != 50*time.Millisecond {
		t.Error("Timeout not set correctly")
	}
	
	// Add normal filter (timeout logic would be implemented in Process)
	filter := &mockFilter{
		id:   "normal_filter",
		name: "normal",
		processFunc: func(data []byte) ([]byte, error) {
			return data, nil
		},
	}
	
	chain.Add(filter)
	
	// Process should work (timeout not implemented yet)
	output, err := chain.Process([]byte("test"))
	if err != nil {
		t.Errorf("Process failed: %v", err)
	}
	
	if string(output) != "test" {
		t.Error("Output data incorrect")
	}
}

// Test 15: Concurrent chain operations
func TestFilterChain_Concurrent(t *testing.T) {
	chain := integration.NewFilterChain()
	
	// Add filter with counter
	var counter atomic.Int32
	filter := &mockFilter{
		id:   "concurrent_filter",
		name: "concurrent",
		processFunc: func(data []byte) ([]byte, error) {
			counter.Add(1)
			return data, nil
		},
	}
	
	chain.Add(filter)
	
	// Run concurrent processing
	var wg sync.WaitGroup
	numGoroutines := 100
	
	for i := 0; i < numGoroutines; i++ {
		wg.Add(1)
		go func() {
			defer wg.Done()
			chain.Process([]byte("test"))
		}()
	}
	
	wg.Wait()
	
	// Verify all processed
	if counter.Load() != int32(numGoroutines) {
		t.Errorf("Expected %d processes, got %d", numGoroutines, counter.Load())
	}
}

// Test 16: Filter order preservation
func TestFilterChain_OrderPreservation(t *testing.T) {
	chain := integration.NewFilterChain()
	
	// Add filters that append their ID
	for i := 0; i < 5; i++ {
		id := string(rune('A' + i))
		filter := &mockFilter{
			id:   id,
			name: "filter_" + id,
			processFunc: func(id string) func([]byte) ([]byte, error) {
				return func(data []byte) ([]byte, error) {
					return append(data, id...), nil
				}
			}(id),
		}
		chain.Add(filter)
	}
	
	// Process and verify order
	output, err := chain.Process([]byte(""))
	if err != nil {
		t.Fatalf("Process failed: %v", err)
	}
	
	expected := "ABCDE"
	if string(output) != expected {
		t.Errorf("Output = %s, want %s", string(output), expected)
	}
}

// Test 17: Chain clear operation
func TestFilterChain_Clear(t *testing.T) {
	chain := integration.NewFilterChain()
	
	// Add filters
	for i := 0; i < 3; i++ {
		filter := &mockFilter{
			id:   string(rune('0' + i)),
			name: "filter",
		}
		chain.Add(filter)
	}
	
	// Clear chain
	chain.Clear()
	
	if chain.GetFilterCount() != 0 {
		t.Error("Chain should be empty after clear")
	}
}

// Test 18: Get filter by ID
func TestFilterChain_GetFilterByID(t *testing.T) {
	chain := integration.NewFilterChain()
	
	filter := &mockFilter{
		id:   "target_filter",
		name: "target",
	}
	
	chain.Add(filter)
	
	// Get filter by ID
	retrieved := chain.GetFilterByID("target_filter")
	if retrieved == nil {
		t.Error("Should retrieve filter by ID")
	}
	
	if retrieved.GetID() != "target_filter" {
		t.Error("Retrieved wrong filter")
	}
	
	// Try non-existent ID
	notFound := chain.GetFilterByID("non_existent")
	if notFound != nil {
		t.Error("Should return nil for non-existent ID")
	}
}

// Test 19: Chain statistics
func TestFilterChain_Statistics(t *testing.T) {
	chain := integration.NewFilterChain()
	
	// Add filter
	filter := &mockFilter{
		id:   "stats_filter",
		name: "stats",
		processFunc: func(data []byte) ([]byte, error) {
			return data, nil
		},
	}
	
	chain.Add(filter)
	
	// Process multiple times
	for i := 0; i < 10; i++ {
		chain.Process([]byte("test"))
	}
	
	// Get statistics
	stats := chain.GetStatistics()
	if stats.TotalExecutions != 10 {
		t.Errorf("TotalExecutions = %d, want 10", stats.TotalExecutions)
	}
	
	if stats.SuccessCount != 10 {
		t.Errorf("SuccessCount = %d, want 10", stats.SuccessCount)
	}
}

// Test 20: Chain buffer size
func TestFilterChain_BufferSize(t *testing.T) {
	chain := integration.NewFilterChain()
	
	// Set buffer size
	chain.SetBufferSize(1024)
	
	if chain.GetBufferSize() != 1024 {
		t.Errorf("BufferSize = %d, want 1024", chain.GetBufferSize())
	}
	
	// Add filter that checks buffer
	filter := &mockFilter{
		id:   "buffer_filter",
		name: "buffer",
		processFunc: func(data []byte) ([]byte, error) {
			// Simulate processing with buffer
			if len(data) > chain.GetBufferSize() {
				return nil, errors.New("data exceeds buffer size")
			}
			return data, nil
		},
	}
	
	chain.Add(filter)
	
	// Small data should work
	_, err := chain.Process(make([]byte, 512))
	if err != nil {
		t.Error("Small data should process successfully")
	}
	
	// Large data should fail
	_, err = chain.Process(make([]byte, 2048))
	if err == nil {
		t.Error("Large data should fail")
	}
}

// Benchmarks

func BenchmarkFilterChain_Add(b *testing.B) {
	chain := integration.NewFilterChain()
	
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		filter := &mockFilter{
			id:   string(rune(i % 256)),
			name: "bench_filter",
		}
		chain.Add(filter)
	}
}

func BenchmarkFilterChain_Process(b *testing.B) {
	chain := integration.NewFilterChain()
	
	// Add simple filter
	filter := &mockFilter{
		id:   "bench",
		name: "bench_filter",
		processFunc: func(data []byte) ([]byte, error) {
			return data, nil
		},
	}
	chain.Add(filter)
	
	data := []byte("benchmark data")
	
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		chain.Process(data)
	}
}

func BenchmarkFilterChain_ConcurrentProcess(b *testing.B) {
	chain := integration.NewFilterChain()
	
	filter := &mockFilter{
		id:   "concurrent",
		name: "concurrent_filter",
		processFunc: func(data []byte) ([]byte, error) {
			return data, nil
		},
	}
	chain.Add(filter)
	
	data := []byte("benchmark data")
	
	b.RunParallel(func(pb *testing.PB) {
		for pb.Next() {
			chain.Process(data)
		}
	})
}

func BenchmarkFilterChain_Clone(b *testing.B) {
	chain := integration.NewFilterChain()
	
	// Add multiple filters
	for i := 0; i < 10; i++ {
		filter := &mockFilter{
			id:   string(rune('A' + i)),
			name: "filter",
		}
		chain.Add(filter)
	}
	
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		_ = chain.Clone()
	}
}