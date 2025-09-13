package core_test

import (
	"sync"
	"testing"

	"github.com/GopherSecurity/gopher-mcp/src/core"
)

// Test 1: NewArena with default chunk size
func TestNewArena_DefaultChunkSize(t *testing.T) {
	arena := core.NewArena(0)
	if arena == nil {
		t.Fatal("NewArena returned nil")
	}

	// Allocate something to verify it works
	data := arena.Allocate(100)
	if len(data) != 100 {
		t.Errorf("Allocated size = %d, want 100", len(data))
	}
}

// Test 2: NewArena with custom chunk size
func TestNewArena_CustomChunkSize(t *testing.T) {
	chunkSize := 1024
	arena := core.NewArena(chunkSize)
	if arena == nil {
		t.Fatal("NewArena returned nil")
	}

	// Allocate to verify it works
	data := arena.Allocate(512)
	if len(data) != 512 {
		t.Errorf("Allocated size = %d, want 512", len(data))
	}
}

// Test 3: Allocate basic functionality
func TestArena_Allocate_Basic(t *testing.T) {
	arena := core.NewArena(1024)

	sizes := []int{10, 20, 30, 40, 50}
	allocations := make([][]byte, 0)

	for _, size := range sizes {
		data := arena.Allocate(size)
		if len(data) != size {
			t.Errorf("Allocated size = %d, want %d", len(data), size)
		}
		allocations = append(allocations, data)
	}

	// Verify allocations are usable
	for i, alloc := range allocations {
		for j := range alloc {
			alloc[j] = byte(i)
		}
	}

	// Verify data integrity
	for i, alloc := range allocations {
		for j := range alloc {
			if alloc[j] != byte(i) {
				t.Errorf("Data corruption at allocation %d, byte %d", i, j)
			}
		}
	}
}

// Test 4: Allocate larger than chunk size
func TestArena_Allocate_LargerThanChunk(t *testing.T) {
	chunkSize := 1024
	arena := core.NewArena(chunkSize)

	// Allocate more than chunk size
	largeSize := chunkSize * 2
	data := arena.Allocate(largeSize)

	if len(data) != largeSize {
		t.Errorf("Allocated size = %d, want %d", len(data), largeSize)
	}

	// Verify the allocation is usable
	for i := range data {
		data[i] = byte(i % 256)
	}

	for i := range data {
		if data[i] != byte(i%256) {
			t.Errorf("Data mismatch at index %d", i)
		}
	}
}

// Test 5: Reset functionality
func TestArena_Reset(t *testing.T) {
	arena := core.NewArena(1024)

	// First allocation
	data1 := arena.Allocate(100)
	for i := range data1 {
		data1[i] = 0xFF
	}

	// Reset arena
	arena.Reset()

	// New allocation after reset
	data2 := arena.Allocate(100)

	// Check that we got a fresh allocation (might reuse memory but should be at offset 0)
	if len(data2) != 100 {
		t.Errorf("Allocated size after reset = %d, want 100", len(data2))
	}

	// The new allocation should be usable
	for i := range data2 {
		data2[i] = 0xAA
	}

	for i := range data2 {
		if data2[i] != 0xAA {
			t.Errorf("Data mismatch at index %d after reset", i)
		}
	}
}

// Test 6: Destroy functionality
func TestArena_Destroy(t *testing.T) {
	arena := core.NewArena(1024)

	// Allocate some memory
	_ = arena.Allocate(100)
	_ = arena.Allocate(200)

	initialTotal := arena.TotalAllocated()
	if initialTotal == 0 {
		t.Error("TotalAllocated should be > 0 before destroy")
	}

	// Destroy arena
	arena.Destroy()

	// Total should be 0 after destroy
	total := arena.TotalAllocated()
	if total != 0 {
		t.Errorf("TotalAllocated after destroy = %d, want 0", total)
	}
}

// Test 7: TotalAllocated tracking
func TestArena_TotalAllocated(t *testing.T) {
	chunkSize := 1024
	arena := core.NewArena(chunkSize)

	// Initially should be 0
	if arena.TotalAllocated() != 0 {
		t.Errorf("Initial TotalAllocated = %d, want 0", arena.TotalAllocated())
	}

	// First allocation triggers chunk allocation
	arena.Allocate(100)
	total1 := arena.TotalAllocated()
	if total1 < int64(chunkSize) {
		t.Errorf("TotalAllocated after first allocation = %d, want >= %d", total1, chunkSize)
	}

	// Small allocation within same chunk shouldn't increase total
	arena.Allocate(100)
	total2 := arena.TotalAllocated()
	if total2 != total1 {
		t.Errorf("TotalAllocated changed for allocation within chunk: %d != %d", total2, total1)
	}

	// Large allocation should increase total
	arena.Allocate(chunkSize * 2)
	total3 := arena.TotalAllocated()
	if total3 <= total2 {
		t.Errorf("TotalAllocated didn't increase for large allocation: %d <= %d", total3, total2)
	}
}

// Test 8: Multiple chunk allocations
func TestArena_MultipleChunks(t *testing.T) {
	chunkSize := 100
	arena := core.NewArena(chunkSize)

	// Allocate enough to require multiple chunks
	allocations := make([][]byte, 0)
	for i := 0; i < 10; i++ {
		data := arena.Allocate(50)
		if len(data) != 50 {
			t.Errorf("Allocation %d: size = %d, want 50", i, len(data))
		}
		allocations = append(allocations, data)
	}

	// Write different data to each allocation
	for i, alloc := range allocations {
		for j := range alloc {
			alloc[j] = byte(i)
		}
	}

	// Verify all allocations maintain their data
	for i, alloc := range allocations {
		for j := range alloc {
			if alloc[j] != byte(i) {
				t.Errorf("Data corruption in allocation %d at byte %d", i, j)
			}
		}
	}
}

// Test 9: Zero-size allocation
func TestArena_Allocate_ZeroSize(t *testing.T) {
	arena := core.NewArena(1024)

	data := arena.Allocate(0)
	if len(data) != 0 {
		t.Errorf("Zero allocation returned slice with length %d", len(data))
	}

	// Should still be able to allocate after zero allocation
	data2 := arena.Allocate(10)
	if len(data2) != 10 {
		t.Errorf("Allocation after zero allocation: size = %d, want 10", len(data2))
	}
}

// Test 10: Concurrent allocations
func TestArena_Concurrent(t *testing.T) {
	arena := core.NewArena(1024)

	var wg sync.WaitGroup
	numGoroutines := 10
	allocsPerGoroutine := 100

	for i := 0; i < numGoroutines; i++ {
		wg.Add(1)
		go func(id int) {
			defer wg.Done()
			for j := 0; j < allocsPerGoroutine; j++ {
				data := arena.Allocate(10)
				if len(data) != 10 {
					t.Errorf("Goroutine %d, allocation %d: size = %d, want 10", id, j, len(data))
				}
				// Write to verify it's usable
				for k := range data {
					data[k] = byte(id)
				}
			}
		}(i)
	}

	wg.Wait()

	// Verify total allocated is reasonable
	total := arena.TotalAllocated()
	minExpected := int64(numGoroutines * allocsPerGoroutine * 10)
	if total < minExpected {
		t.Errorf("TotalAllocated = %d, want >= %d", total, minExpected)
	}
}

// Benchmarks

func BenchmarkArena_Allocate_Small(b *testing.B) {
	arena := core.NewArena(64 * 1024)

	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		_ = arena.Allocate(32)
	}
}

func BenchmarkArena_Allocate_Medium(b *testing.B) {
	arena := core.NewArena(64 * 1024)

	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		_ = arena.Allocate(1024)
	}
}

func BenchmarkArena_Allocate_Large(b *testing.B) {
	arena := core.NewArena(64 * 1024)

	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		_ = arena.Allocate(64 * 1024)
	}
}

func BenchmarkArena_Reset(b *testing.B) {
	arena := core.NewArena(64 * 1024)

	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		for j := 0; j < 100; j++ {
			arena.Allocate(100)
		}
		arena.Reset()
	}
}

func BenchmarkArena_Concurrent(b *testing.B) {
	arena := core.NewArena(64 * 1024)

	b.RunParallel(func(pb *testing.PB) {
		for pb.Next() {
			_ = arena.Allocate(128)
		}
	})
}
