package manager_test

import (
	"sync"
	"testing"

	"github.com/GopherSecurity/gopher-mcp/src/manager"
	"github.com/google/uuid"
)

// Mock filter implementation for testing
type mockFilter struct {
	id   uuid.UUID
	name string
}

func (mf *mockFilter) GetID() uuid.UUID {
	return mf.id
}

func (mf *mockFilter) GetName() string {
	return mf.name
}

func (mf *mockFilter) Process(data []byte) ([]byte, error) {
	return data, nil
}

func (mf *mockFilter) Close() error {
	return nil
}

// Test 1: Create new filter registry
func TestNewFilterRegistry(t *testing.T) {
	registry := manager.NewFilterRegistry()

	if registry == nil {
		t.Fatal("NewFilterRegistry returned nil")
	}

	if registry.Count() != 0 {
		t.Errorf("New registry should have 0 filters, got %d", registry.Count())
	}
}

// Test 2: Add filter to registry
func TestFilterRegistry_Add(t *testing.T) {
	registry := manager.NewFilterRegistry()

	id := uuid.New()
	filter := &mockFilter{
		id:   id,
		name: "test-filter",
	}

	registry.Add(id, filter)

	if registry.Count() != 1 {
		t.Errorf("Registry should have 1 filter, got %d", registry.Count())
	}

	// Verify filter can be retrieved
	retrieved, exists := registry.Get(id)
	if !exists {
		t.Error("Filter should exist in registry")
	}
	if retrieved.GetID() != id {
		t.Error("Retrieved filter has wrong ID")
	}
}

// Test 3: Get filter by name
func TestFilterRegistry_GetByName(t *testing.T) {
	registry := manager.NewFilterRegistry()

	id := uuid.New()
	filter := &mockFilter{
		id:   id,
		name: "named-filter",
	}

	registry.Add(id, filter)

	// Get by name
	retrieved, exists := registry.GetByName("named-filter")
	if !exists {
		t.Error("Filter should be retrievable by name")
	}
	if retrieved.GetID() != id {
		t.Error("Retrieved filter has wrong ID")
	}

	// Try non-existent name
	_, exists = registry.GetByName("non-existent")
	if exists {
		t.Error("Non-existent filter should not be found")
	}
}

// Test 4: Remove filter from registry
func TestFilterRegistry_Remove(t *testing.T) {
	registry := manager.NewFilterRegistry()

	id := uuid.New()
	filter := &mockFilter{
		id:   id,
		name: "removable-filter",
	}

	registry.Add(id, filter)

	// Remove filter
	removed, existed := registry.Remove(id)
	if !existed {
		t.Error("Filter should have existed")
	}
	if removed.GetID() != id {
		t.Error("Wrong filter was removed")
	}

	// Verify it's gone
	if registry.Count() != 0 {
		t.Error("Registry should be empty after removal")
	}

	// Verify name index is cleaned up
	_, exists := registry.GetByName("removable-filter")
	if exists {
		t.Error("Filter should not be retrievable by name after removal")
	}
}

// Test 5: Check name uniqueness
func TestFilterRegistry_CheckNameUniqueness(t *testing.T) {
	registry := manager.NewFilterRegistry()

	// Should be unique initially
	if !registry.CheckNameUniqueness("unique-name") {
		t.Error("Name should be unique in empty registry")
	}

	// Add filter with name
	id := uuid.New()
	filter := &mockFilter{
		id:   id,
		name: "unique-name",
	}
	registry.Add(id, filter)

	// Should not be unique anymore
	if registry.CheckNameUniqueness("unique-name") {
		t.Error("Name should not be unique after adding filter with that name")
	}

	// Different name should still be unique
	if !registry.CheckNameUniqueness("different-name") {
		t.Error("Different name should be unique")
	}
}

// Test 6: Get all filters
func TestFilterRegistry_GetAll(t *testing.T) {
	registry := manager.NewFilterRegistry()

	// Add multiple filters
	filters := make(map[uuid.UUID]*mockFilter)
	for i := 0; i < 5; i++ {
		id := uuid.New()
		filter := &mockFilter{
			id:   id,
			name: string(rune('a' + i)),
		}
		filters[id] = filter
		registry.Add(id, filter)
	}

	// Get all
	all := registry.GetAll()
	if len(all) != 5 {
		t.Errorf("GetAll should return 5 filters, got %d", len(all))
	}

	// Verify all filters are present
	for id := range filters {
		if _, exists := all[id]; !exists {
			t.Errorf("Filter %s missing from GetAll", id)
		}
	}
}

// Test 7: Concurrent add operations
func TestFilterRegistry_ConcurrentAdd(t *testing.T) {
	registry := manager.NewFilterRegistry()

	var wg sync.WaitGroup
	numGoroutines := 100

	for i := 0; i < numGoroutines; i++ {
		wg.Add(1)
		go func(idx int) {
			defer wg.Done()
			id := uuid.New()
			filter := &mockFilter{
				id:   id,
				name: string(rune('a' + (idx % 26))),
			}
			registry.Add(id, filter)
		}(i)
	}

	wg.Wait()

	// Should have all filters
	if registry.Count() != numGoroutines {
		t.Errorf("Registry should have %d filters, got %d", numGoroutines, registry.Count())
	}
}

// Test 8: Concurrent read operations
func TestFilterRegistry_ConcurrentRead(t *testing.T) {
	registry := manager.NewFilterRegistry()

	// Add some filters
	ids := make([]uuid.UUID, 10)
	for i := 0; i < 10; i++ {
		id := uuid.New()
		ids[i] = id
		filter := &mockFilter{
			id:   id,
			name: string(rune('a' + i)),
		}
		registry.Add(id, filter)
	}

	var wg sync.WaitGroup
	numReaders := 100

	// Concurrent reads
	for i := 0; i < numReaders; i++ {
		wg.Add(1)
		go func(idx int) {
			defer wg.Done()
			// Random operations
			id := ids[idx%len(ids)]
			registry.Get(id)
			registry.GetByName(string(rune('a' + (idx % 10))))
			registry.GetAll()
			registry.Count()
		}(i)
	}

	wg.Wait()

	// Verify registry is still intact
	if registry.Count() != 10 {
		t.Error("Registry state corrupted after concurrent reads")
	}
}

// Test 9: Mixed concurrent operations
func TestFilterRegistry_ConcurrentMixed(t *testing.T) {
	registry := manager.NewFilterRegistry()

	var wg sync.WaitGroup
	numOperations := 100

	// Track added IDs for removal
	var mu sync.Mutex
	addedIDs := make([]uuid.UUID, 0)

	for i := 0; i < numOperations; i++ {
		wg.Add(1)
		go func(idx int) {
			defer wg.Done()

			switch idx % 3 {
			case 0: // Add
				id := uuid.New()
				filter := &mockFilter{
					id:   id,
					name: uuid.NewString(),
				}
				registry.Add(id, filter)
				mu.Lock()
				addedIDs = append(addedIDs, id)
				mu.Unlock()

			case 1: // Read
				registry.GetAll()
				registry.Count()

			case 2: // Remove (if possible)
				mu.Lock()
				if len(addedIDs) > 0 {
					id := addedIDs[0]
					addedIDs = addedIDs[1:]
					mu.Unlock()
					registry.Remove(id)
				} else {
					mu.Unlock()
				}
			}
		}(i)
	}

	wg.Wait()

	// Registry should be in consistent state
	count := registry.Count()
	all := registry.GetAll()
	if len(all) != count {
		t.Error("Registry count doesn't match GetAll length")
	}
}

// Test 10: Empty name handling
func TestFilterRegistry_EmptyName(t *testing.T) {
	registry := manager.NewFilterRegistry()

	id := uuid.New()
	filter := &mockFilter{
		id:   id,
		name: "", // Empty name
	}

	registry.Add(id, filter)

	// Should be added by ID
	if registry.Count() != 1 {
		t.Error("Filter with empty name should still be added")
	}

	// Should be retrievable by ID
	_, exists := registry.Get(id)
	if !exists {
		t.Error("Filter should be retrievable by ID")
	}

	// Should not be in name index
	_, exists = registry.GetByName("")
	if exists {
		t.Error("Empty name should not be indexed")
	}
}

// Benchmarks

func BenchmarkFilterRegistry_Add(b *testing.B) {
	registry := manager.NewFilterRegistry()

	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		id := uuid.New()
		filter := &mockFilter{
			id:   id,
			name: uuid.NewString(),
		}
		registry.Add(id, filter)
	}
}

func BenchmarkFilterRegistry_Get(b *testing.B) {
	registry := manager.NewFilterRegistry()

	// Pre-populate
	id := uuid.New()
	filter := &mockFilter{
		id:   id,
		name: "bench-filter",
	}
	registry.Add(id, filter)

	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		registry.Get(id)
	}
}

func BenchmarkFilterRegistry_GetByName(b *testing.B) {
	registry := manager.NewFilterRegistry()

	// Pre-populate
	id := uuid.New()
	filter := &mockFilter{
		id:   id,
		name: "bench-filter",
	}
	registry.Add(id, filter)

	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		registry.GetByName("bench-filter")
	}
}

func BenchmarkFilterRegistry_ConcurrentOps(b *testing.B) {
	registry := manager.NewFilterRegistry()

	b.RunParallel(func(pb *testing.PB) {
		for pb.Next() {
			id := uuid.New()
			filter := &mockFilter{
				id:   id,
				name: uuid.NewString(),
			}
			registry.Add(id, filter)
			registry.Get(id)
		}
	})
}
