package core_test

import (
	"sync"
	"sync/atomic"
	"testing"
	"time"

	"github.com/GopherSecurity/gopher-mcp/src/core"
	"github.com/GopherSecurity/gopher-mcp/src/types"
)

// Test 1: NewMemoryManager creation
func TestNewMemoryManager(t *testing.T) {
	maxMemory := int64(1024 * 1024) // 1MB
	mm := core.NewMemoryManager(maxMemory)

	if mm == nil {
		t.Fatal("NewMemoryManager returned nil")
	}

	// Check initial state
	if mm.GetCurrentUsage() != 0 {
		t.Error("Initial usage should be 0")
	}

	if mm.GetMaxMemory() != maxMemory {
		t.Errorf("MaxMemory = %d, want %d", mm.GetMaxMemory(), maxMemory)
	}

	// Cleanup
	mm.Stop()
}

// Test 2: NewMemoryManagerWithCleanup
func TestNewMemoryManagerWithCleanup(t *testing.T) {
	maxMemory := int64(2 * 1024 * 1024) // 2MB
	cleanupInterval := 100 * time.Millisecond

	mm := core.NewMemoryManagerWithCleanup(maxMemory, cleanupInterval)

	if mm == nil {
		t.Fatal("NewMemoryManagerWithCleanup returned nil")
	}

	// Wait for at least one cleanup cycle
	time.Sleep(150 * time.Millisecond)

	// Should still be functional
	if mm.GetMaxMemory() != maxMemory {
		t.Errorf("MaxMemory = %d, want %d", mm.GetMaxMemory(), maxMemory)
	}

	// Test with zero cleanup interval (no cleanup)
	mm2 := core.NewMemoryManagerWithCleanup(maxMemory, 0)
	if mm2 == nil {
		t.Fatal("NewMemoryManagerWithCleanup with 0 interval returned nil")
	}

	// Cleanup
	mm.Stop()
	mm2.Stop()
}

// Test 3: InitializePools
func TestMemoryManager_InitializePools(t *testing.T) {
	mm := core.NewMemoryManager(10 * 1024 * 1024)
	defer mm.Stop()

	// Initialize standard pools
	mm.InitializePools()

	// Test that we can get buffers of standard sizes
	sizes := []int{
		core.SmallBufferSize,
		core.MediumBufferSize,
		core.LargeBufferSize,
		core.HugeBufferSize,
	}

	for _, size := range sizes {
		pool := mm.GetPoolForSize(size)
		if pool == nil {
			t.Errorf("No pool found for size %d", size)
		}
	}
}

// Test 4: Get and Put buffers
func TestMemoryManager_GetPut(t *testing.T) {
	mm := core.NewMemoryManager(10 * 1024 * 1024)
	defer mm.Stop()
	mm.InitializePools()

	// Get a small buffer
	buf := mm.Get(256)
	if buf == nil {
		t.Fatal("Get returned nil")
	}
	if buf.Cap() < 256 {
		t.Errorf("Buffer capacity = %d, want >= 256", buf.Cap())
	}

	// Usage should increase
	usage1 := mm.GetCurrentUsage()
	if usage1 <= 0 {
		t.Error("Usage should increase after Get")
	}

	// Put buffer back
	mm.Put(buf)

	// Usage should decrease
	usage2 := mm.GetCurrentUsage()
	if usage2 >= usage1 {
		t.Error("Usage should decrease after Put")
	}

	// Get multiple buffers
	buffers := make([]*types.Buffer, 5)
	for i := range buffers {
		buffers[i] = mm.Get(1024)
		if buffers[i] == nil {
			t.Fatalf("Get[%d] returned nil", i)
		}
	}

	// Put them all back
	for _, b := range buffers {
		mm.Put(b)
	}

	// Usage should be back to low/zero
	finalUsage := mm.GetCurrentUsage()
	if finalUsage > usage2 {
		t.Error("Usage not properly decremented after returning all buffers")
	}
}

// Test 5: Memory limit enforcement
func TestMemoryManager_MemoryLimit(t *testing.T) {
	maxMemory := int64(1024) // 1KB limit
	mm := core.NewMemoryManager(maxMemory)
	defer mm.Stop()
	mm.InitializePools()

	// Get a buffer within limit
	buf1 := mm.Get(512)
	if buf1 == nil {
		t.Fatal("Get within limit returned nil")
	}

	// Try to get another buffer that would exceed limit
	buf2 := mm.Get(600)
	if buf2 != nil {
		t.Error("Get should return nil when exceeding memory limit")
	}

	// Put back first buffer
	mm.Put(buf1)

	// Now we should be able to get the second buffer
	buf3 := mm.Get(600)
	if buf3 == nil {
		t.Error("Get should succeed after freeing memory")
	}
	mm.Put(buf3)
}

// Test 6: SetMaxMemory
func TestMemoryManager_SetMaxMemory(t *testing.T) {
	mm := core.NewMemoryManager(1024)
	defer mm.Stop()

	// Change memory limit
	newLimit := int64(2048)
	mm.SetMaxMemory(newLimit)

	if mm.GetMaxMemory() != newLimit {
		t.Errorf("MaxMemory = %d, want %d", mm.GetMaxMemory(), newLimit)
	}

	// Set to 0 (unlimited)
	mm.SetMaxMemory(0)
	if mm.GetMaxMemory() != 0 {
		t.Error("MaxMemory should be 0 for unlimited")
	}

	// Should be able to allocate large buffer with no limit
	buf := mm.Get(10000)
	if buf == nil {
		t.Error("Get should succeed with no memory limit")
	}
	mm.Put(buf)
}

// Test 7: CheckMemoryLimit
func TestMemoryManager_CheckMemoryLimit(t *testing.T) {
	mm := core.NewMemoryManager(1024)
	defer mm.Stop()

	// Should not exceed for small allocation
	if mm.CheckMemoryLimit(512) {
		t.Error("CheckMemoryLimit should return false for allocation within limit")
	}

	// Should exceed for large allocation
	if !mm.CheckMemoryLimit(2048) {
		t.Error("CheckMemoryLimit should return true for allocation exceeding limit")
	}

	// Get a buffer to use some memory
	buf := mm.Get(512)
	if buf == nil {
		t.Fatal("Get failed")
	}

	// Check remaining capacity
	if !mm.CheckMemoryLimit(600) {
		t.Error("CheckMemoryLimit should consider current usage")
	}

	mm.Put(buf)

	// With no limit
	mm.SetMaxMemory(0)
	if mm.CheckMemoryLimit(1000000) {
		t.Error("CheckMemoryLimit should always return false with no limit")
	}
}

// Test 8: Statistics tracking
func TestMemoryManager_Statistics(t *testing.T) {
	mm := core.NewMemoryManager(10 * 1024 * 1024)
	defer mm.Stop()
	mm.InitializePools()

	// Get initial stats
	stats1 := mm.GetStatistics()

	// Allocate some buffers
	buffers := make([]*types.Buffer, 3)
	for i := range buffers {
		buffers[i] = mm.Get(1024)
	}

	// Check allocation stats
	stats2 := mm.GetStatistics()
	if stats2.AllocationCount <= stats1.AllocationCount {
		t.Error("AllocationCount should increase")
	}
	if stats2.TotalAllocated <= stats1.TotalAllocated {
		t.Error("TotalAllocated should increase")
	}
	if stats2.CurrentUsage <= 0 {
		t.Error("CurrentUsage should be positive")
	}

	// Return buffers
	for _, buf := range buffers {
		mm.Put(buf)
	}

	// Check release stats
	stats3 := mm.GetStatistics()
	if stats3.ReleaseCount <= stats2.ReleaseCount {
		t.Error("ReleaseCount should increase")
	}
	if stats3.TotalReleased <= stats2.TotalReleased {
		t.Error("TotalReleased should increase")
	}
}

// Test 9: Pool selection
func TestMemoryManager_PoolSelection(t *testing.T) {
	mm := core.NewMemoryManager(10 * 1024 * 1024)
	defer mm.Stop()
	mm.InitializePools()

	tests := []struct {
		requestSize int
		minCapacity int
	}{
		{100, 100},
		{512, 512},
		{513, 513},
		{4096, 4096},
		{4097, 4097},
		{65536, 65536},
		{65537, 65537},
		{1048576, 1048576},
	}

	for _, tt := range tests {
		buf := mm.Get(tt.requestSize)
		if buf == nil {
			t.Errorf("Get(%d) returned nil", tt.requestSize)
			continue
		}

		// Buffer capacity should be at least the requested size
		if buf.Cap() < tt.minCapacity {
			t.Errorf("Get(%d): capacity = %d, want >= %d",
				tt.requestSize, buf.Cap(), tt.minCapacity)
		}

		mm.Put(buf)
	}
}

// Test 10: Concurrent operations
func TestMemoryManager_Concurrent(t *testing.T) {
	mm := core.NewMemoryManager(100 * 1024 * 1024) // 100MB
	defer mm.Stop()
	mm.InitializePools()

	var wg sync.WaitGroup
	numGoroutines := 10
	opsPerGoroutine := 100

	// Track allocations for verification
	var totalAllocated int64
	var totalReleased int64

	for i := 0; i < numGoroutines; i++ {
		wg.Add(1)
		go func(id int) {
			defer wg.Done()

			for j := 0; j < opsPerGoroutine; j++ {
				size := 512 + (id * 100) // Vary sizes by goroutine

				// Get buffer
				buf := mm.Get(size)
				if buf == nil {
					t.Errorf("Goroutine %d: Get failed", id)
					continue
				}

				atomic.AddInt64(&totalAllocated, 1)

				// Use buffer
				buf.Write([]byte{byte(id), byte(j)})

				// Sometimes check stats
				if j%10 == 0 {
					_ = mm.GetStatistics()
					_ = mm.GetCurrentUsage()
				}

				// Put back
				mm.Put(buf)
				atomic.AddInt64(&totalReleased, 1)
			}
		}(i)
	}

	wg.Wait()

	// Verify counts
	stats := mm.GetStatistics()
	expectedOps := int64(numGoroutines * opsPerGoroutine)

	if int64(stats.AllocationCount) != expectedOps {
		t.Errorf("AllocationCount = %d, want %d", stats.AllocationCount, expectedOps)
	}
	if int64(stats.ReleaseCount) != expectedOps {
		t.Errorf("ReleaseCount = %d, want %d", stats.ReleaseCount, expectedOps)
	}
}

// Test UpdateUsage
func TestMemoryManager_UpdateUsage(t *testing.T) {
	mm := core.NewMemoryManager(10 * 1024 * 1024)
	defer mm.Stop()

	// Initial usage should be 0
	if mm.GetCurrentUsage() != 0 {
		t.Error("Initial usage should be 0")
	}

	// Increase usage
	mm.UpdateUsage(1024)
	if mm.GetCurrentUsage() != 1024 {
		t.Errorf("Usage = %d, want 1024", mm.GetCurrentUsage())
	}

	// Increase more
	mm.UpdateUsage(512)
	if mm.GetCurrentUsage() != 1536 {
		t.Errorf("Usage = %d, want 1536", mm.GetCurrentUsage())
	}

	// Decrease usage
	mm.UpdateUsage(-1536)
	if mm.GetCurrentUsage() != 0 {
		t.Errorf("Usage = %d, want 0", mm.GetCurrentUsage())
	}

	// Check peak usage is tracked
	mm.UpdateUsage(2048)
	stats := mm.GetStats()
	if stats.PeakUsage < 2048 {
		t.Errorf("PeakUsage = %d, want >= 2048", stats.PeakUsage)
	}
}

// Test GetPoolHitRate
func TestMemoryManager_GetPoolHitRate(t *testing.T) {
	mm := core.NewMemoryManager(10 * 1024 * 1024)
	defer mm.Stop()
	mm.InitializePools()

	// Initial hit rate should be 0
	if mm.GetPoolHitRate() != 0 {
		t.Error("Initial hit rate should be 0")
	}

	// Get some buffers (should be hits from pool)
	for i := 0; i < 10; i++ {
		buf := mm.Get(512)
		if buf != nil {
			mm.Put(buf)
		}
	}

	// Hit rate should be positive
	hitRate := mm.GetPoolHitRate()
	if hitRate <= 0 {
		t.Errorf("Hit rate = %f, want > 0", hitRate)
	}
}

// Test cleanup trigger
func TestMemoryManager_CleanupTrigger(t *testing.T) {
	mm := core.NewMemoryManagerWithCleanup(1024, 50*time.Millisecond)
	defer mm.Stop()
	mm.InitializePools()

	// Allocate to near limit
	buf := mm.Get(700)
	if buf == nil {
		t.Fatal("Get failed")
	}

	// Wait for cleanup
	time.Sleep(100 * time.Millisecond)

	// Put back buffer
	mm.Put(buf)

	// Stats should show cleanup happened
	stats := mm.GetStatistics()
	if stats.CurrentUsage > 0 {
		t.Log("Current usage after cleanup:", stats.CurrentUsage)
	}
}

// Benchmarks

func BenchmarkMemoryManager_Get(b *testing.B) {
	mm := core.NewMemoryManager(100 * 1024 * 1024)
	defer mm.Stop()
	mm.InitializePools()

	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		buf := mm.Get(1024)
		mm.Put(buf)
	}
}

func BenchmarkMemoryManager_GetVariousSizes(b *testing.B) {
	mm := core.NewMemoryManager(100 * 1024 * 1024)
	defer mm.Stop()
	mm.InitializePools()

	sizes := []int{256, 1024, 4096, 16384, 65536}

	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		size := sizes[i%len(sizes)]
		buf := mm.Get(size)
		mm.Put(buf)
	}
}

func BenchmarkMemoryManager_Concurrent(b *testing.B) {
	mm := core.NewMemoryManager(100 * 1024 * 1024)
	defer mm.Stop()
	mm.InitializePools()

	b.RunParallel(func(pb *testing.PB) {
		for pb.Next() {
			buf := mm.Get(1024)
			buf.Write([]byte("test"))
			mm.Put(buf)
		}
	})
}

func BenchmarkMemoryManager_Statistics(b *testing.B) {
	mm := core.NewMemoryManager(100 * 1024 * 1024)
	defer mm.Stop()
	mm.InitializePools()

	// Do some allocations first
	for i := 0; i < 100; i++ {
		buf := mm.Get(1024)
		mm.Put(buf)
	}

	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		_ = mm.GetStatistics()
	}
}

func BenchmarkMemoryManager_CheckMemoryLimit(b *testing.B) {
	mm := core.NewMemoryManager(100 * 1024 * 1024)
	defer mm.Stop()

	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		_ = mm.CheckMemoryLimit(1024)
	}
}
