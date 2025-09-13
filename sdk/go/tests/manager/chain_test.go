package manager_test

import (
	"testing"
	"time"

	"github.com/GopherSecurity/gopher-mcp/src/manager"
	"github.com/google/uuid"
)

// Test 1: Create filter chain
func TestFilterManager_CreateChain(t *testing.T) {
	config := manager.DefaultFilterManagerConfig()
	fm := manager.NewFilterManager(config)
	
	chainConfig := manager.ChainConfig{
		Name:           "test-chain",
		ExecutionMode:  manager.Sequential,
		Timeout:        time.Second,
		EnableMetrics:  true,
		EnableTracing:  false,
		MaxConcurrency: 1,
	}
	
	chain, err := fm.CreateChain(chainConfig)
	if err != nil {
		t.Fatalf("CreateChain failed: %v", err)
	}
	
	if chain == nil {
		t.Fatal("CreateChain returned nil chain")
	}
	
	if chain.Name != "test-chain" {
		t.Errorf("Chain name = %s, want test-chain", chain.Name)
	}
	
	if chain.Config.ExecutionMode != manager.Sequential {
		t.Error("Chain execution mode not set correctly")
	}
}

// Test 2: Create duplicate chain
func TestFilterManager_CreateDuplicateChain(t *testing.T) {
	config := manager.DefaultFilterManagerConfig()
	fm := manager.NewFilterManager(config)
	
	chainConfig := manager.ChainConfig{
		Name: "duplicate-chain",
	}
	
	// First creation should succeed
	_, err := fm.CreateChain(chainConfig)
	if err != nil {
		t.Fatalf("First CreateChain failed: %v", err)
	}
	
	// Second creation should fail
	_, err = fm.CreateChain(chainConfig)
	if err == nil {
		t.Error("Creating duplicate chain should fail")
	}
}

// Test 3: Get chain by name
func TestFilterManager_GetChain(t *testing.T) {
	config := manager.DefaultFilterManagerConfig()
	fm := manager.NewFilterManager(config)
	
	chainConfig := manager.ChainConfig{
		Name: "retrievable-chain",
	}
	
	created, err := fm.CreateChain(chainConfig)
	if err != nil {
		t.Fatalf("CreateChain failed: %v", err)
	}
	
	// Get chain
	retrieved, exists := fm.GetChain("retrievable-chain")
	if !exists {
		t.Error("Chain should exist")
	}
	
	if retrieved.Name != created.Name {
		t.Error("Retrieved chain doesn't match created chain")
	}
	
	// Try to get non-existent chain
	_, exists = fm.GetChain("non-existent")
	if exists {
		t.Error("Non-existent chain should not be found")
	}
}

// Test 4: Remove chain
func TestFilterManager_RemoveChain(t *testing.T) {
	config := manager.DefaultFilterManagerConfig()
	fm := manager.NewFilterManager(config)
	
	chainConfig := manager.ChainConfig{
		Name: "removable-chain",
	}
	
	_, err := fm.CreateChain(chainConfig)
	if err != nil {
		t.Fatalf("CreateChain failed: %v", err)
	}
	
	// Remove chain
	err = fm.RemoveChain("removable-chain")
	if err != nil {
		t.Fatalf("RemoveChain failed: %v", err)
	}
	
	// Verify it's gone
	_, exists := fm.GetChain("removable-chain")
	if exists {
		t.Error("Chain should not exist after removal")
	}
	
	// Removing non-existent chain should fail
	err = fm.RemoveChain("non-existent")
	if err == nil {
		t.Error("Removing non-existent chain should fail")
	}
}

// Test 5: Chain capacity limit
func TestFilterManager_ChainCapacityLimit(t *testing.T) {
	config := manager.DefaultFilterManagerConfig()
	config.MaxChains = 2
	fm := manager.NewFilterManager(config)
	
	// Create chains up to limit
	for i := 0; i < 2; i++ {
		chainConfig := manager.ChainConfig{
			Name: string(rune('a' + i)),
		}
		_, err := fm.CreateChain(chainConfig)
		if err != nil {
			t.Fatalf("CreateChain %d failed: %v", i, err)
		}
	}
	
	// Next creation should fail
	chainConfig := manager.ChainConfig{
		Name: "overflow",
	}
	_, err := fm.CreateChain(chainConfig)
	if err == nil {
		t.Error("Creating chain beyond capacity should fail")
	}
}

// Test 6: Chain execution modes
func TestChainExecutionModes(t *testing.T) {
	tests := []struct {
		name string
		mode manager.ExecutionMode
	}{
		{"sequential", manager.Sequential},
		{"parallel", manager.Parallel},
		{"pipeline", manager.Pipeline},
	}
	
	config := manager.DefaultFilterManagerConfig()
	fm := manager.NewFilterManager(config)
	
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			chainConfig := manager.ChainConfig{
				Name:          tt.name,
				ExecutionMode: tt.mode,
			}
			
			chain, err := fm.CreateChain(chainConfig)
			if err != nil {
				t.Fatalf("CreateChain failed: %v", err)
			}
			
			if chain.Config.ExecutionMode != tt.mode {
				t.Errorf("ExecutionMode = %v, want %v", 
					chain.Config.ExecutionMode, tt.mode)
			}
		})
	}
}

// Test 7: Remove filter from chain
func TestFilterChain_RemoveFilter(t *testing.T) {
	config := manager.DefaultFilterManagerConfig()
	fm := manager.NewFilterManager(config)
	
	chainConfig := manager.ChainConfig{
		Name: "filter-removal-chain",
	}
	
	chain, err := fm.CreateChain(chainConfig)
	if err != nil {
		t.Fatalf("CreateChain failed: %v", err)
	}
	
	// Add mock filters to chain
	id1 := uuid.New()
	id2 := uuid.New()
	filter1 := &mockFilter{id: id1, name: "filter1"}
	filter2 := &mockFilter{id: id2, name: "filter2"}
	
	chain.Filters = append(chain.Filters, filter1, filter2)
	
	// Remove first filter
	chain.RemoveFilter(id1)
	
	// Verify filter is removed
	if len(chain.Filters) != 1 {
		t.Errorf("Chain should have 1 filter, has %d", len(chain.Filters))
	}
	
	if chain.Filters[0].GetID() != id2 {
		t.Error("Wrong filter was removed")
	}
	
	// Remove non-existent filter (should be no-op)
	chain.RemoveFilter(uuid.New())
	if len(chain.Filters) != 1 {
		t.Error("Removing non-existent filter should not affect chain")
	}
}

// Test 8: Chain with different configurations
func TestChainConfigurations(t *testing.T) {
	config := manager.DefaultFilterManagerConfig()
	fm := manager.NewFilterManager(config)
	
	tests := []struct {
		name   string
		config manager.ChainConfig
	}{
		{
			name: "metrics-enabled",
			config: manager.ChainConfig{
				Name:          "metrics-chain",
				EnableMetrics: true,
			},
		},
		{
			name: "tracing-enabled",
			config: manager.ChainConfig{
				Name:          "tracing-chain",
				EnableTracing: true,
			},
		},
		{
			name: "high-concurrency",
			config: manager.ChainConfig{
				Name:           "concurrent-chain",
				MaxConcurrency: 100,
				ExecutionMode:  manager.Parallel,
			},
		},
		{
			name: "with-timeout",
			config: manager.ChainConfig{
				Name:    "timeout-chain",
				Timeout: 5 * time.Second,
			},
		},
	}
	
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			chain, err := fm.CreateChain(tt.config)
			if err != nil {
				t.Fatalf("CreateChain failed: %v", err)
			}
			
			// Verify config is stored correctly
			if chain.Config.Name != tt.config.Name {
				t.Error("Chain config name mismatch")
			}
			
			if chain.Config.EnableMetrics != tt.config.EnableMetrics {
				t.Error("EnableMetrics not set correctly")
			}
			
			if chain.Config.EnableTracing != tt.config.EnableTracing {
				t.Error("EnableTracing not set correctly")
			}
		})
	}
}

// Test 9: Chain management with running manager
func TestChainManagement_WithRunningManager(t *testing.T) {
	config := manager.DefaultFilterManagerConfig()
	fm := manager.NewFilterManager(config)
	
	// Start manager
	err := fm.Start()
	if err != nil {
		t.Fatalf("Start failed: %v", err)
	}
	defer fm.Stop()
	
	// Should be able to create chains while running
	chainConfig := manager.ChainConfig{
		Name: "runtime-chain",
	}
	
	chain, err := fm.CreateChain(chainConfig)
	if err != nil {
		t.Fatalf("CreateChain failed while running: %v", err)
	}
	
	if chain == nil {
		t.Error("Chain should be created while manager is running")
	}
	
	// Should be able to remove chains while running
	err = fm.RemoveChain("runtime-chain")
	if err != nil {
		t.Fatalf("RemoveChain failed while running: %v", err)
	}
}

// Test 10: Empty chain name handling
func TestChain_EmptyName(t *testing.T) {
	config := manager.DefaultFilterManagerConfig()
	fm := manager.NewFilterManager(config)
	
	chainConfig := manager.ChainConfig{
		Name: "", // Empty name
	}
	
	// Creating chain with empty name might be allowed or not
	// depending on validation rules
	chain, err := fm.CreateChain(chainConfig)
	
	if err == nil {
		// If allowed, verify we can still work with it
		if chain.Name != "" {
			t.Error("Chain name should be empty as configured")
		}
		
		// Should not be retrievable by empty name
		_, exists := fm.GetChain("")
		if !exists {
			t.Error("Chain with empty name should be retrievable if creation succeeded")
		}
	}
	// If not allowed, that's also valid behavior
}

// Benchmarks

func BenchmarkFilterManager_CreateChain(b *testing.B) {
	config := manager.DefaultFilterManagerConfig()
	fm := manager.NewFilterManager(config)
	
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		chainConfig := manager.ChainConfig{
			Name: uuid.NewString(),
		}
		fm.CreateChain(chainConfig)
	}
}

func BenchmarkFilterManager_GetChain(b *testing.B) {
	config := manager.DefaultFilterManagerConfig()
	fm := manager.NewFilterManager(config)
	
	chainConfig := manager.ChainConfig{
		Name: "bench-chain",
	}
	fm.CreateChain(chainConfig)
	
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		fm.GetChain("bench-chain")
	}
}

func BenchmarkFilterManager_RemoveChain(b *testing.B) {
	config := manager.DefaultFilterManagerConfig()
	fm := manager.NewFilterManager(config)
	
	// Pre-create chains
	for i := 0; i < b.N; i++ {
		chainConfig := manager.ChainConfig{
			Name: uuid.NewString(),
		}
		fm.CreateChain(chainConfig)
	}
	
	b.ResetTimer()
	// Note: This will eventually fail when chains are exhausted
	// but it measures the removal performance
	for i := 0; i < b.N; i++ {
		fm.RemoveChain(uuid.NewString())
	}
}

func BenchmarkFilterChain_RemoveFilter(b *testing.B) {
	chain := &manager.FilterChain{
		Name:    "bench",
		Filters: make([]manager.Filter, 0),
	}
	
	// Add many filters
	for i := 0; i < 100; i++ {
		filter := &mockFilter{
			id:   uuid.New(),
			name: uuid.NewString(),
		}
		chain.Filters = append(chain.Filters, filter)
	}
	
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		// Remove non-existent filter (worst case)
		chain.RemoveFilter(uuid.New())
	}
}