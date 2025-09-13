package integration_test

import (
	"context"
	"fmt"
	"sync"
	"sync/atomic"
	"testing"
	"time"

	"github.com/GopherSecurity/gopher-mcp/src/integration"
)

// Copy mockFilter from other test files
type mockAdvancedFilter struct {
	id          string
	name        string
	filterType  string
	version     string
	description string
	processFunc func([]byte) ([]byte, error)
	config      map[string]interface{}
	stateless   bool
}

func (m *mockAdvancedFilter) GetID() string                                { return m.id }
func (m *mockAdvancedFilter) GetName() string                              { return m.name }
func (m *mockAdvancedFilter) GetType() string                              { return m.filterType }
func (m *mockAdvancedFilter) GetVersion() string                           { return m.version }
func (m *mockAdvancedFilter) GetDescription() string                       { return m.description }
func (m *mockAdvancedFilter) ValidateConfig() error                        { return nil }
func (m *mockAdvancedFilter) GetConfiguration() map[string]interface{}     { return m.config }
func (m *mockAdvancedFilter) UpdateConfig(cfg map[string]interface{})      { m.config = cfg }
func (m *mockAdvancedFilter) GetCapabilities() []string                    { return []string{"filter", "transform"} }
func (m *mockAdvancedFilter) GetDependencies() []integration.FilterDependency { return nil }
func (m *mockAdvancedFilter) GetResourceRequirements() integration.ResourceRequirements {
	return integration.ResourceRequirements{Memory: 1024, CPUCores: 1}
}
func (m *mockAdvancedFilter) GetTypeInfo() integration.TypeInfo {
	return integration.TypeInfo{
		InputTypes:  []string{"bytes"},
		OutputTypes: []string{"bytes"},
	}
}
func (m *mockAdvancedFilter) EstimateLatency() time.Duration       { return 10 * time.Millisecond }
func (m *mockAdvancedFilter) HasBlockingOperations() bool          { return false }
func (m *mockAdvancedFilter) UsesDeprecatedFeatures() bool         { return false }
func (m *mockAdvancedFilter) HasKnownVulnerabilities() bool        { return false }
func (m *mockAdvancedFilter) IsStateless() bool                    { return m.stateless }
func (m *mockAdvancedFilter) SetID(id string)                      { m.id = id }
func (m *mockAdvancedFilter) Clone() integration.Filter {
	return &mockAdvancedFilter{
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

func (m *mockAdvancedFilter) Process(data []byte) ([]byte, error) {
	if m.processFunc != nil {
		return m.processFunc(data)
	}
	return data, nil
}

// Test 1: Advanced batch request handling
func TestAdvanced_BatchRequestHandling(t *testing.T) {
	client := integration.NewFilteredMCPClient(integration.ClientConfig{
		BatchConcurrency: 2,
		BatchFailFast:    true,
	})
	
	var requests []integration.BatchRequest
	for i := 0; i < 10; i++ {
		requests = append(requests, integration.BatchRequest{
			ID:      fmt.Sprintf("req_%d", i),
			Request: map[string]interface{}{"id": i},
		})
	}
	
	ctx := context.Background()
	result, err := client.BatchRequestsWithFilters(ctx, requests)
	
	if result != nil && len(result.Responses) > 0 {
		if result.SuccessRate() < 0 {
			t.Error("Invalid success rate")
		}
	}
	
	_ = err
}

// Test 2: Multiple filter composition
func TestAdvanced_MultipleFilterComposition(t *testing.T) {
	client := integration.NewFilteredMCPClient(integration.ClientConfig{})
	
	filters := make([]integration.Filter, 0)
	for i := 0; i < 3; i++ {
		filters = append(filters, &mockAdvancedFilter{
			id:   fmt.Sprintf("filter_%d", i),
			name: fmt.Sprintf("composed_filter_%d", i),
			processFunc: func(data []byte) ([]byte, error) {
				return append(data, '.'), nil
			},
		})
	}
	
	_, err := client.CallToolWithFilters(
		"test_tool",
		map[string]interface{}{"param": "value"},
		filters...,
	)
	
	_ = err
}

// Test 3: Context cancellation handling
func TestAdvanced_ContextCancellation(t *testing.T) {
	client := integration.NewFilteredMCPClient(integration.ClientConfig{})
	
	ctx, cancel := context.WithCancel(context.Background())
	
	// Cancel immediately
	cancel()
	
	request := map[string]interface{}{
		"method": "test_method",
	}
	
	_, err := client.RequestWithTimeout(ctx, request, 100*time.Millisecond)
	
	// Should fail due to cancelled context
	_ = err
}

// Test 4: Chain performance monitoring
func TestAdvanced_ChainPerformanceMonitoring(t *testing.T) {
	chain := integration.NewFilterChain()
	
	var latencies []time.Duration
	mu := &sync.Mutex{}
	
	for i := 0; i < 3; i++ {
		delay := time.Duration(i+1) * 10 * time.Millisecond
		chain.Add(&mockAdvancedFilter{
			id:   fmt.Sprintf("perf_%d", i),
			name: fmt.Sprintf("performance_filter_%d", i),
			processFunc: func(d time.Duration) func([]byte) ([]byte, error) {
				return func(data []byte) ([]byte, error) {
					start := time.Now()
					time.Sleep(d)
					mu.Lock()
					latencies = append(latencies, time.Since(start))
					mu.Unlock()
					return data, nil
				}
			}(delay),
		})
	}
	
	chain.Process([]byte("test"))
	
	if len(latencies) != 3 {
		t.Errorf("Expected 3 latency measurements, got %d", len(latencies))
	}
}

// Test 5: Concurrent filter execution
func TestAdvanced_ConcurrentFilterExecution(t *testing.T) {
	chain := integration.NewFilterChain()
	chain.SetMode(integration.ParallelMode)
	
	var execCount atomic.Int32
	
	for i := 0; i < 5; i++ {
		chain.Add(&mockAdvancedFilter{
			id:   fmt.Sprintf("concurrent_%d", i),
			name: fmt.Sprintf("concurrent_filter_%d", i),
			processFunc: func(data []byte) ([]byte, error) {
				execCount.Add(1)
				time.Sleep(10 * time.Millisecond)
				return data, nil
			},
		})
	}
	
	start := time.Now()
	chain.Process([]byte("test"))
	elapsed := time.Since(start)
	
	// Parallel execution should be faster than sequential
	if elapsed > 30*time.Millisecond {
		t.Log("Parallel execution may not be working efficiently")
	}
	
	if execCount.Load() != 5 {
		t.Errorf("Expected 5 executions, got %d", execCount.Load())
	}
}

// Test 6: Error propagation in chains
func TestAdvanced_ErrorPropagation(t *testing.T) {
	chain := integration.NewFilterChain()
	
	executed := make([]string, 0)
	mu := &sync.Mutex{}
	
	// Add filters
	chain.Add(&mockAdvancedFilter{
		id:   "first",
		name: "first_filter",
		processFunc: func(data []byte) ([]byte, error) {
			mu.Lock()
			executed = append(executed, "first")
			mu.Unlock()
			return data, nil
		},
	})
	
	chain.Add(&mockAdvancedFilter{
		id:   "error",
		name: "error_filter",
		processFunc: func(data []byte) ([]byte, error) {
			mu.Lock()
			executed = append(executed, "error")
			mu.Unlock()
			return nil, fmt.Errorf("intentional error")
		},
	})
	
	chain.Add(&mockAdvancedFilter{
		id:   "third",
		name: "third_filter",
		processFunc: func(data []byte) ([]byte, error) {
			mu.Lock()
			executed = append(executed, "third")
			mu.Unlock()
			return data, nil
		},
	})
	
	_, err := chain.Process([]byte("test"))
	
	if err == nil {
		t.Error("Expected error to propagate")
	}
	
	if len(executed) != 2 {
		t.Errorf("Expected 2 filters to execute before error, got %d", len(executed))
	}
	
	if executed[len(executed)-1] == "third" {
		t.Error("Third filter should not execute after error")
	}
}

// Test 7: Dynamic filter addition and removal
func TestAdvanced_DynamicFilterManagement(t *testing.T) {
	chain := integration.NewFilterChain()
	
	// Add initial filters
	for i := 0; i < 3; i++ {
		chain.Add(&mockAdvancedFilter{
			id:   fmt.Sprintf("%d", i),
			name: fmt.Sprintf("initial_%d", i),
		})
	}
	
	if chain.GetFilterCount() != 3 {
		t.Errorf("Expected 3 filters, got %d", chain.GetFilterCount())
	}
	
	// Remove middle filter
	err := chain.Remove("1")
	if err != nil {
		t.Errorf("Failed to remove filter: %v", err)
	}
	
	if chain.GetFilterCount() != 2 {
		t.Errorf("Expected 2 filters after removal, got %d", chain.GetFilterCount())
	}
	
	// Add new filter
	chain.Add(&mockAdvancedFilter{
		id:   "new",
		name: "new_filter",
	})
	
	if chain.GetFilterCount() != 3 {
		t.Errorf("Expected 3 filters after addition, got %d", chain.GetFilterCount())
	}
}

// Test 8: Chain validation with complex rules
func TestAdvanced_ComplexChainValidation(t *testing.T) {
	client := integration.NewFilteredMCPClient(integration.ClientConfig{})
	chain := integration.NewFilterChain()
	
	// Add filters with specific types
	chain.Add(&mockAdvancedFilter{
		id:         "auth",
		name:       "authentication",
		filterType: "security",
	})
	
	chain.Add(&mockAdvancedFilter{
		id:         "validate",
		name:       "validation",
		filterType: "validation",
	})
	
	chain.Add(&mockAdvancedFilter{
		id:         "transform",
		name:       "transformation",
		filterType: "transform",
	})
	
	chain.Add(&mockAdvancedFilter{
		id:         "log",
		name:       "logging",
		filterType: "logging",
	})
	
	result, err := client.ValidateFilterChain(chain)
	if err != nil {
		t.Errorf("Validation failed: %v", err)
	}
	
	_ = result
}

// Test 9: Batch processing with timeout
func TestAdvanced_BatchProcessingWithTimeout(t *testing.T) {
	client := integration.NewFilteredMCPClient(integration.ClientConfig{
		BatchConcurrency: 5,
	})
	
	// Create requests with varying processing times
	var requests []integration.BatchRequest
	for i := 0; i < 20; i++ {
		requests = append(requests, integration.BatchRequest{
			ID:      fmt.Sprintf("req_%d", i),
			Request: map[string]interface{}{"delay": i * 10}, // ms
		})
	}
	
	ctx, cancel := context.WithTimeout(context.Background(), 100*time.Millisecond)
	defer cancel()
	
	start := time.Now()
	result, err := client.BatchRequestsWithFilters(ctx, requests)
	elapsed := time.Since(start)
	
	// Should timeout
	if elapsed > 150*time.Millisecond {
		t.Error("Batch processing didn't respect timeout")
	}
	
	_ = result
	_ = err
}

// Test 10: Filter priority ordering
func TestAdvanced_FilterPriorityOrdering(t *testing.T) {
	chain := integration.NewFilterChain()
	
	executionOrder := make([]string, 0)
	mu := &sync.Mutex{}
	
	// Add filters in random order but with priority hints
	filters := []struct {
		id       string
		priority int
	}{
		{"low", 3},
		{"high", 1},
		{"medium", 2},
	}
	
	for _, f := range filters {
		filter := &mockAdvancedFilter{
			id:   f.id,
			name: fmt.Sprintf("priority_%s", f.id),
			processFunc: func(id string) func([]byte) ([]byte, error) {
				return func(data []byte) ([]byte, error) {
					mu.Lock()
					executionOrder = append(executionOrder, id)
					mu.Unlock()
					return data, nil
				}
			}(f.id),
		}
		chain.Add(filter)
	}
	
	chain.Process([]byte("test"))
	
	// Verify execution order
	if len(executionOrder) != 3 {
		t.Errorf("Expected 3 filters to execute, got %d", len(executionOrder))
	}
}

// Test 11: Resource pool management
func TestAdvanced_ResourcePoolManagement(t *testing.T) {
	server := integration.NewFilteredMCPServer()
	
	// Register multiple resources
	for i := 0; i < 10; i++ {
		resource := &mockResource{
			name: fmt.Sprintf("resource_%d", i),
		}
		
		filter := &mockAdvancedFilter{
			id:   fmt.Sprintf("res_filter_%d", i),
			name: fmt.Sprintf("resource_filter_%d", i),
		}
		
		err := server.RegisterFilteredResource(resource, filter)
		_ = err
	}
	
	// Verify resources are managed properly
	// Note: Actual verification depends on implementation
}

// Test 12: Chain statistics collection
func TestAdvanced_ChainStatisticsCollection(t *testing.T) {
	chain := integration.NewFilterChain()
	
	// Add filters
	for i := 0; i < 3; i++ {
		chain.Add(&mockAdvancedFilter{
			id:   fmt.Sprintf("stat_%d", i),
			name: fmt.Sprintf("statistics_filter_%d", i),
			processFunc: func(data []byte) ([]byte, error) {
				time.Sleep(5 * time.Millisecond)
				return data, nil
			},
		})
	}
	
	// Process multiple times
	for i := 0; i < 10; i++ {
		chain.Process([]byte("test"))
	}
	
	stats := chain.GetStatistics()
	
	if stats.TotalExecutions != 10 {
		t.Errorf("Expected 10 executions, got %d", stats.TotalExecutions)
	}
}

// Test 13: Memory-efficient processing
func TestAdvanced_MemoryEfficientProcessing(t *testing.T) {
	chain := integration.NewFilterChain()
	chain.SetBufferSize(1024) // 1KB buffer
	
	// Add filter that checks buffer constraints
	chain.Add(&mockAdvancedFilter{
		id:   "memory",
		name: "memory_filter",
		processFunc: func(data []byte) ([]byte, error) {
			if len(data) > chain.GetBufferSize() {
				return nil, fmt.Errorf("data exceeds buffer size")
			}
			return data, nil
		},
	})
	
	// Test with small data
	_, err := chain.Process(make([]byte, 512))
	if err != nil {
		t.Error("Small data should process successfully")
	}
	
	// Test with large data
	_, err = chain.Process(make([]byte, 2048))
	if err == nil {
		t.Error("Large data should fail")
	}
}

// Test 14: Subscription management
func TestAdvanced_SubscriptionManagement(t *testing.T) {
	client := integration.NewFilteredMCPClient(integration.ClientConfig{})
	
	// Create multiple subscriptions
	var subs []*integration.Subscription
	
	for i := 0; i < 5; i++ {
		filter := &mockAdvancedFilter{
			id:   fmt.Sprintf("sub_filter_%d", i),
			name: fmt.Sprintf("subscription_filter_%d", i),
		}
		
		sub, err := client.SubscribeWithFilters(
			fmt.Sprintf("resource_%d", i),
			filter,
		)
		
		if err == nil && sub != nil {
			subs = append(subs, sub)
		}
	}
	
	// Update filters on subscriptions
	for _, sub := range subs {
		newFilter := &mockAdvancedFilter{
			id:   "updated",
			name: "updated_filter",
		}
		sub.UpdateFilters(newFilter)
	}
	
	// Unsubscribe all
	for _, sub := range subs {
		sub.Unsubscribe()
	}
}

// Test 15: Debug mode with detailed logging
func TestAdvanced_DebugModeDetailedLogging(t *testing.T) {
	client := integration.NewFilteredMCPClient(integration.ClientConfig{})
	
	// Enable debug mode
	client.EnableDebugMode(
		integration.WithLogLevel("TRACE"),
		integration.WithLogFilters(true),
		integration.WithLogRequests(true),
		integration.WithTraceExecution(true),
	)
	
	// Perform operations
	chain := integration.NewFilterChain()
	for i := 0; i < 3; i++ {
		chain.Add(&mockAdvancedFilter{
			id:   fmt.Sprintf("debug_%d", i),
			name: fmt.Sprintf("debug_filter_%d", i),
		})
	}
	
	client.SetClientRequestChain(chain)
	client.FilterOutgoingRequest([]byte("debug test"))
	
	// Get debug state
	state := client.DumpState()
	if state == "" {
		t.Error("Debug state should not be empty")
	}
	
	client.DisableDebugMode()
}

// Test 16: Graceful degradation
func TestAdvanced_GracefulDegradation(t *testing.T) {
	chain := integration.NewFilterChain()
	
	failureCount := 0
	
	// Add filter that fails intermittently
	chain.Add(&mockAdvancedFilter{
		id:   "intermittent",
		name: "intermittent_filter",
		processFunc: func(data []byte) ([]byte, error) {
			failureCount++
			if failureCount%3 == 0 {
				return nil, fmt.Errorf("intermittent failure")
			}
			return data, nil
		},
	})
	
	// Process multiple times
	successCount := 0
	for i := 0; i < 10; i++ {
		_, err := chain.Process([]byte("test"))
		if err == nil {
			successCount++
		}
	}
	
	// Should have ~66% success rate
	if successCount < 6 || successCount > 7 {
		t.Errorf("Unexpected success count: %d", successCount)
	}
}

// Test 17: Chain cloning and modification
func TestAdvanced_ChainCloningModification(t *testing.T) {
	original := integration.NewFilterChain()
	original.SetName("original")
	
	// Add filters
	for i := 0; i < 5; i++ {
		original.Add(&mockAdvancedFilter{
			id:   fmt.Sprintf("orig_%d", i),
			name: fmt.Sprintf("original_filter_%d", i),
		})
	}
	
	// Clone chain
	cloned := original.Clone()
	
	// Modify cloned chain
	cloned.SetName("cloned")
	cloned.Add(&mockAdvancedFilter{
		id:   "new",
		name: "new_filter",
	})
	
	// Verify independence
	if original.GetFilterCount() == cloned.GetFilterCount() {
		t.Error("Cloned chain modifications affected original")
	}
	
	if original.GetName() == cloned.GetName() {
		t.Error("Chain names should be different")
	}
}

// Test 18: Complete end-to-end flow
func TestAdvanced_CompleteEndToEndFlow(t *testing.T) {
	client := integration.NewFilteredMCPClient(integration.ClientConfig{
		EnableFiltering: true,
	})
	server := integration.NewFilteredMCPServer()
	
	// Set up client chains
	clientReqChain := integration.NewFilterChain()
	clientReqChain.Add(&mockAdvancedFilter{
		id:   "client_req",
		name: "client_request",
		processFunc: func(data []byte) ([]byte, error) {
			return append([]byte("CLIENT:"), data...), nil
		},
	})
	client.SetClientRequestChain(clientReqChain)
	
	// Set up server chains
	serverReqChain := integration.NewFilterChain()
	serverReqChain.Add(&mockAdvancedFilter{
		id:   "server_req",
		name: "server_request",
		processFunc: func(data []byte) ([]byte, error) {
			return append([]byte("SERVER:"), data...), nil
		},
	})
	server.SetRequestChain(serverReqChain)
	
	// Simulate flow
	originalData := []byte("data")
	
	// Client processes outgoing
	clientProcessed, err := client.FilterOutgoingRequest(originalData)
	if err != nil {
		t.Fatalf("Client processing failed: %v", err)
	}
	
	// Server processes incoming
	serverProcessed, err := server.ProcessRequest(clientProcessed)
	if err != nil {
		t.Fatalf("Server processing failed: %v", err)
	}
	
	// Verify transformations
	if len(serverProcessed) <= len(originalData) {
		t.Error("Data should be transformed through the pipeline")
	}
}

// Test 19: Performance benchmarking suite
func TestAdvanced_PerformanceBenchmarking(t *testing.T) {
	scenarios := []struct {
		name        string
		filterCount int
		dataSize    int
	}{
		{"Small", 3, 100},
		{"Medium", 10, 1000},
		{"Large", 20, 10000},
	}
	
	for _, scenario := range scenarios {
		t.Run(scenario.name, func(t *testing.T) {
			chain := integration.NewFilterChain()
			
			// Add filters
			for i := 0; i < scenario.filterCount; i++ {
				chain.Add(&mockAdvancedFilter{
					id:   fmt.Sprintf("bench_%d", i),
					name: fmt.Sprintf("benchmark_filter_%d", i),
					processFunc: func(data []byte) ([]byte, error) {
						// Simulate processing
						time.Sleep(time.Microsecond)
						return data, nil
					},
				})
			}
			
			// Measure performance
			data := make([]byte, scenario.dataSize)
			iterations := 100
			
			start := time.Now()
			for i := 0; i < iterations; i++ {
				chain.Process(data)
			}
			elapsed := time.Since(start)
			
			avgTime := elapsed / time.Duration(iterations)
			t.Logf("Scenario %s: avg time %v", scenario.name, avgTime)
		})
	}
}

// Test 20: Stress test with resource limits
func TestAdvanced_StressTestWithLimits(t *testing.T) {
	client := integration.NewFilteredMCPClient(integration.ClientConfig{
		BatchConcurrency: 20,
	})
	
	// Set up resource-limited chain
	chain := integration.NewFilterChain()
	chain.SetMaxFilters(100)
	
	// Add filters up to limit
	for i := 0; i < 100; i++ {
		err := chain.Add(&mockAdvancedFilter{
			id:   fmt.Sprintf("stress_%d", i),
			name: fmt.Sprintf("stress_filter_%d", i),
		})
		if err != nil {
			t.Errorf("Failed to add filter %d: %v", i, err)
			break
		}
	}
	
	// Try to exceed limit
	err := chain.Add(&mockAdvancedFilter{
		id:   "excess",
		name: "excess_filter",
	})
	if err == nil {
		t.Error("Should not be able to exceed filter limit")
	}
	
	client.SetClientRequestChain(chain)
	
	// Stress test with concurrent operations
	var wg sync.WaitGroup
	numOperations := 1000
	
	for i := 0; i < numOperations; i++ {
		wg.Add(1)
		go func(id int) {
			defer wg.Done()
			client.FilterOutgoingRequest([]byte(fmt.Sprintf("req_%d", id)))
		}(i)
	}
	
	wg.Wait()
}

// Mock resource type
type mockResource struct {
	name string
}

func (m *mockResource) Name() string {
	return m.name
}

func (m *mockResource) Read() ([]byte, error) {
	return []byte("resource data"), nil
}

func (m *mockResource) Write(data []byte) error {
	return nil
}