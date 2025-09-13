package core_test

import (
	"sync"
	"testing"

	"github.com/GopherSecurity/gopher-mcp/src/core"
	"github.com/GopherSecurity/gopher-mcp/src/types"
)

// Test 1: NewBufferPool with valid range
func TestNewBufferPool_ValidRange(t *testing.T) {
	minSize := 512
	maxSize := 65536
	pool := core.NewBufferPool(minSize, maxSize)

	if pool == nil {
		t.Fatal("NewBufferPool returned nil")
	}

	// Get a buffer to verify pool works
	buf := pool.Get(1024)
	if buf == nil {
		t.Fatal("Get returned nil buffer")
	}
	if buf.Cap() < 1024 {
		t.Errorf("Buffer capacity = %d, want >= 1024", buf.Cap())
	}
}

// Test 2: NewDefaultBufferPool
func TestNewDefaultBufferPool(t *testing.T) {
	pool := core.NewDefaultBufferPool()

	if pool == nil {
		t.Fatal("NewDefaultBufferPool returned nil")
	}

	// Test with various sizes
	sizes := []int{256, 512, 1024, 2048, 4096}
	for _, size := range sizes {
		buf := pool.Get(size)
		if buf == nil {
			t.Errorf("Get(%d) returned nil", size)
			continue
		}
		if buf.Cap() < size {
			t.Errorf("Buffer capacity = %d, want >= %d", buf.Cap(), size)
		}
	}
}

// Test 3: Get buffer within pool range
func TestBufferPool_Get_WithinRange(t *testing.T) {
	pool := core.NewBufferPool(512, 8192)

	testCases := []struct {
		requestSize int
		minCapacity int
	}{
		{256, 512},   // Below min, should get min size
		{512, 512},   // Exact min
		{768, 1024},  // Between sizes, should round up
		{1024, 1024}, // Exact pool size
		{3000, 4096}, // Between sizes, should round up
		{8192, 8192}, // Exact max
	}

	for _, tc := range testCases {
		buf := pool.Get(tc.requestSize)
		if buf == nil {
			t.Errorf("Get(%d) returned nil", tc.requestSize)
			continue
		}
		if buf.Cap() < tc.minCapacity {
			t.Errorf("Get(%d): capacity = %d, want >= %d", tc.requestSize, buf.Cap(), tc.minCapacity)
		}
	}
}

// Test 4: Get buffer outside pool range
func TestBufferPool_Get_OutsideRange(t *testing.T) {
	pool := core.NewBufferPool(512, 4096)

	// Request larger than max
	largeSize := 10000
	buf := pool.Get(largeSize)

	if buf == nil {
		t.Fatal("Get returned nil for large size")
	}
	if buf.Cap() < largeSize {
		t.Errorf("Buffer capacity = %d, want >= %d", buf.Cap(), largeSize)
	}
}

// Test 5: Put buffer back to pool
func TestBufferPool_Put(t *testing.T) {
	pool := core.NewBufferPool(512, 4096)

	// Get a buffer
	buf1 := pool.Get(1024)
	if buf1 == nil {
		t.Fatal("Get returned nil")
	}

	// Write some data
	testData := []byte("test data")
	buf1.Write(testData)

	// Put it back
	pool.Put(buf1)

	// Get another buffer (might be the same one)
	buf2 := pool.Get(1024)
	if buf2 == nil {
		t.Fatal("Get returned nil after Put")
	}

	// Buffer should be reset
	if buf2.Len() != 0 {
		t.Errorf("Returned buffer not reset: len = %d, want 0", buf2.Len())
	}
}

// Test 6: Put nil buffer
func TestBufferPool_Put_Nil(t *testing.T) {
	pool := core.NewBufferPool(512, 4096)

	// Should not panic
	pool.Put(nil)

	// Pool should still work
	buf := pool.Get(1024)
	if buf == nil {
		t.Fatal("Get returned nil after Put(nil)")
	}
}

// Test 7: GetStatistics
func TestBufferPool_GetStatistics(t *testing.T) {
	pool := core.NewBufferPool(512, 4096)

	// Initial stats
	stats1 := pool.GetStatistics()

	// Get some buffers
	buffers := make([]*types.Buffer, 5)
	for i := range buffers {
		buffers[i] = pool.Get(1024)
	}

	// Check stats increased
	stats2 := pool.GetStatistics()
	if stats2.Gets <= stats1.Gets {
		t.Errorf("Gets didn't increase: %d <= %d", stats2.Gets, stats1.Gets)
	}

	// Put buffers back
	for _, buf := range buffers {
		pool.Put(buf)
	}

	// Check puts increased
	stats3 := pool.GetStatistics()
	if stats3.Puts <= stats2.Puts {
		t.Errorf("Puts didn't increase: %d <= %d", stats3.Puts, stats2.Puts)
	}
}

// Test 8: Concurrent Get and Put
func TestBufferPool_Concurrent(t *testing.T) {
	pool := core.NewBufferPool(512, 65536)

	var wg sync.WaitGroup
	numGoroutines := 10
	opsPerGoroutine := 100

	for i := 0; i < numGoroutines; i++ {
		wg.Add(1)
		go func(id int) {
			defer wg.Done()

			for j := 0; j < opsPerGoroutine; j++ {
				// Get buffer
				size := 512 * (1 + j%8) // Vary sizes
				buf := pool.Get(size)
				if buf == nil {
					t.Errorf("Goroutine %d: Get(%d) returned nil", id, size)
					continue
				}

				// Use buffer
				testData := []byte{byte(id), byte(j)}
				buf.Write(testData)

				// Put back
				pool.Put(buf)
			}
		}(i)
	}

	wg.Wait()

	// Verify stats are reasonable
	stats := pool.GetStatistics()
	expectedOps := numGoroutines * opsPerGoroutine
	if stats.Gets < uint64(expectedOps) {
		t.Errorf("Gets = %d, want >= %d", stats.Gets, expectedOps)
	}
	if stats.Puts < uint64(expectedOps) {
		t.Errorf("Puts = %d, want >= %d", stats.Puts, expectedOps)
	}
}

// Test 9: SimpleBufferPool basic operations
func TestSimpleBufferPool_Basic(t *testing.T) {
	pool := core.NewSimpleBufferPool(1024)

	// Get buffer
	buf := pool.Get(512)
	if buf == nil {
		t.Fatal("Get returned nil")
	}
	if buf.Cap() < 512 {
		t.Errorf("Buffer capacity = %d, want >= 512", buf.Cap())
	}

	// Write data
	buf.Write([]byte("test"))

	// Put back
	pool.Put(buf)

	// Get stats
	stats := pool.Stats()
	if stats.Gets == 0 {
		t.Error("Gets should be > 0")
	}
	if stats.Puts == 0 {
		t.Error("Puts should be > 0")
	}
}

// Test 10: SimpleBufferPool with larger than initial size
func TestSimpleBufferPool_Grow(t *testing.T) {
	initialSize := 512
	pool := core.NewSimpleBufferPool(initialSize)

	// Request larger buffer
	largerSize := 2048
	buf := pool.Get(largerSize)

	if buf == nil {
		t.Fatal("Get returned nil")
	}
	if buf.Cap() < largerSize {
		t.Errorf("Buffer capacity = %d, want >= %d", buf.Cap(), largerSize)
	}

	// Put back and get again
	pool.Put(buf)

	buf2 := pool.Get(largerSize)
	if buf2 == nil {
		t.Fatal("Second Get returned nil")
	}
	// The returned buffer should still have the grown capacity
	if buf2.Cap() < largerSize {
		t.Errorf("Reused buffer capacity = %d, want >= %d", buf2.Cap(), largerSize)
	}
}

// Benchmarks

func BenchmarkBufferPool_Get(b *testing.B) {
	pool := core.NewDefaultBufferPool()

	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		buf := pool.Get(1024)
		pool.Put(buf)
	}
}

func BenchmarkBufferPool_Get_Various(b *testing.B) {
	pool := core.NewDefaultBufferPool()
	sizes := []int{512, 1024, 2048, 4096, 8192}

	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		size := sizes[i%len(sizes)]
		buf := pool.Get(size)
		pool.Put(buf)
	}
}

func BenchmarkSimpleBufferPool_Get(b *testing.B) {
	pool := core.NewSimpleBufferPool(1024)

	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		buf := pool.Get(1024)
		pool.Put(buf)
	}
}

func BenchmarkBufferPool_Concurrent(b *testing.B) {
	pool := core.NewDefaultBufferPool()

	b.RunParallel(func(pb *testing.PB) {
		for pb.Next() {
			buf := pool.Get(1024)
			buf.Write([]byte("test"))
			pool.Put(buf)
		}
	})
}
