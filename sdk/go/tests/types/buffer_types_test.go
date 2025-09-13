package types_test

import (
	"bytes"
	"testing"

	"github.com/GopherSecurity/gopher-mcp/src/types"
)

func TestBuffer_BasicOperations(t *testing.T) {
	t.Run("Create and Write", func(t *testing.T) {
		buf := &types.Buffer{}
		data := []byte("Hello, World!")

		n, err := buf.Write(data)
		if err != nil {
			t.Fatalf("Write failed: %v", err)
		}
		if n != len(data) {
			t.Errorf("Write returned %d, want %d", n, len(data))
		}
		if buf.Len() != len(data) {
			t.Errorf("Buffer length = %d, want %d", buf.Len(), len(data))
		}
		if !bytes.Equal(buf.Bytes(), data) {
			t.Errorf("Buffer content = %s, want %s", buf.Bytes(), data)
		}
	})

	t.Run("Reset", func(t *testing.T) {
		buf := &types.Buffer{}
		buf.Write([]byte("Some data"))

		if buf.Len() == 0 {
			t.Error("Buffer should contain data before reset")
		}

		buf.Reset()

		if buf.Len() != 0 {
			t.Errorf("Buffer length after reset = %d, want 0", buf.Len())
		}
	})

	t.Run("Grow", func(t *testing.T) {
		buf := &types.Buffer{}
		buf.Write([]byte("Initial"))
		initialCap := buf.Cap()

		// Grow beyond initial capacity
		buf.Grow(1000)

		if buf.Cap() <= initialCap {
			t.Errorf("Buffer capacity after grow = %d, should be > %d", buf.Cap(), initialCap)
		}
	})
}

func TestBuffer_NilSafety(t *testing.T) {
	var buf *types.Buffer

	// All methods should handle nil gracefully
	if buf.Len() != 0 {
		t.Error("Nil buffer Len() should return 0")
	}
	if buf.Cap() != 0 {
		t.Error("Nil buffer Cap() should return 0")
	}
	if buf.Bytes() != nil {
		t.Error("Nil buffer Bytes() should return nil")
	}

	buf.Reset()   // Should not panic
	buf.Grow(100) // Should not panic

	n, err := buf.Write([]byte("test"))
	if n != 0 || err != nil {
		t.Error("Nil buffer Write should return 0, nil")
	}
}

func TestBufferPool_Operations(t *testing.T) {
	t.Run("Create Pool", func(t *testing.T) {
		pool := types.NewBufferPool()

		if pool == nil {
			t.Fatal("NewBufferPool returned nil")
		}
	})

	t.Run("Get and Put", func(t *testing.T) {
		pool := types.NewBufferPool()

		// Get buffer from pool
		buf1 := pool.Get()
		if buf1 == nil {
			t.Fatal("Pool.Get returned nil")
		}
		if !buf1.IsPooled() {
			t.Error("Buffer from pool should be marked as pooled")
		}

		// Write data
		testData := []byte("Test data")
		buf1.Write(testData)

		// Return to pool
		pool.Put(buf1)

		// Get another buffer (should be reused)
		buf2 := pool.Get()
		if buf2 == nil {
			t.Fatal("Pool.Get returned nil")
		}
		if buf2.Len() != 0 {
			t.Error("Buffer from pool should be reset")
		}
	})

	t.Run("Nil Pool Safety", func(t *testing.T) {
		var pool *types.BufferPool

		buf := pool.Get()
		if buf != nil {
			t.Error("Nil pool Get() should return nil")
		}

		pool.Put(&types.Buffer{}) // Should not panic
	})
}

func TestBuffer_Pooling(t *testing.T) {
	t.Run("Release", func(t *testing.T) {
		pool := types.NewBufferPool()
		buf := pool.Get()

		if !buf.IsPooled() {
			t.Error("Buffer from pool should be pooled")
		}

		buf.Write([]byte("Some data"))
		buf.Release()

		// After release, buffer should be reset
		if buf.Len() != 0 {
			t.Error("Released buffer should be reset")
		}
	})

	t.Run("IsPooled", func(t *testing.T) {
		// Non-pooled buffer
		normalBuf := &types.Buffer{}
		if normalBuf.IsPooled() {
			t.Error("Normal buffer should not be pooled")
		}

		// Pooled buffer
		pool := types.NewBufferPool()
		pooledBuf := pool.Get()
		if !pooledBuf.IsPooled() {
			t.Error("Buffer from pool should be pooled")
		}
	})

	t.Run("SetPool", func(t *testing.T) {
		buf := &types.Buffer{}
		pool := types.NewBufferPool()

		buf.SetPool(pool)
		if !buf.IsPooled() {
			t.Error("Buffer should be marked as pooled after SetPool")
		}
	})
}

func TestBufferSlice(t *testing.T) {
	t.Run("Basic Slice", func(t *testing.T) {
		slice := &types.BufferSlice{}

		if slice.Len() != 0 {
			t.Errorf("Empty slice length = %d, want 0", slice.Len())
		}

		if slice.Bytes() != nil {
			t.Error("Empty slice Bytes() should return nil")
		}
	})

	t.Run("SubSlice", func(t *testing.T) {
		// BufferSlice with actual data would need proper initialization
		// For now, just test the method doesn't panic
		slice := types.BufferSlice{}

		// Test SubSlice on empty slice
		subSlice := slice.SubSlice(2, 5)
		if subSlice.Len() != 0 {
			t.Errorf("SubSlice of empty slice should have length 0, got %d", subSlice.Len())
		}

		// Test SubSlice with invalid bounds
		subSlice = slice.SubSlice(-1, 5)
		if subSlice.Len() != 0 {
			t.Error("SubSlice with negative start should return empty slice")
		}
	})

	t.Run("Slice Method", func(t *testing.T) {
		slice := &types.BufferSlice{}

		// Test various slicing operations
		result := slice.Slice(0, 10)
		if result.Len() != 0 {
			t.Errorf("Slice of empty BufferSlice should have length 0, got %d", result.Len())
		}

		// Test with negative start
		result = slice.Slice(-1, 5)
		if result.Len() != 0 {
			t.Error("Slice with negative start should handle gracefully")
		}

		// Test with end < start
		result = slice.Slice(5, 2)
		if result.Len() != 0 {
			t.Error("Slice with end < start should return empty slice")
		}
	})

	t.Run("Nil Safety", func(t *testing.T) {
		var slice *types.BufferSlice

		if slice.Len() != 0 {
			t.Error("Nil slice Len() should return 0")
		}

		if slice.Bytes() != nil {
			t.Error("Nil slice Bytes() should return nil")
		}

		result := slice.SubSlice(0, 10)
		if result.Len() != 0 {
			t.Error("SubSlice on nil should return empty slice")
		}

		result = slice.Slice(0, 10)
		if result.Len() != 0 {
			t.Error("Slice on nil should return empty slice")
		}
	})
}

func TestPoolStatistics(t *testing.T) {
	stats := types.PoolStatistics{
		Gets:   100,
		Puts:   95,
		Hits:   80,
		Misses: 20,
	}

	if stats.Gets != 100 {
		t.Errorf("Gets = %d, want 100", stats.Gets)
	}
	if stats.Puts != 95 {
		t.Errorf("Puts = %d, want 95", stats.Puts)
	}
	if stats.Hits != 80 {
		t.Errorf("Hits = %d, want 80", stats.Hits)
	}
	if stats.Misses != 20 {
		t.Errorf("Misses = %d, want 20", stats.Misses)
	}
}

func TestBuffer_LargeData(t *testing.T) {
	buf := &types.Buffer{}

	// Write large amount of data
	largeData := make([]byte, 10000)
	for i := range largeData {
		largeData[i] = byte(i % 256)
	}

	n, err := buf.Write(largeData)
	if err != nil {
		t.Fatalf("Failed to write large data: %v", err)
	}
	if n != len(largeData) {
		t.Errorf("Write returned %d, want %d", n, len(largeData))
	}
	if buf.Len() != len(largeData) {
		t.Errorf("Buffer length = %d, want %d", buf.Len(), len(largeData))
	}
	if !bytes.Equal(buf.Bytes(), largeData) {
		t.Error("Buffer content doesn't match written data")
	}
}

func TestBuffer_MultipleWrites(t *testing.T) {
	buf := &types.Buffer{}

	// Multiple writes should append
	writes := []string{"Hello", " ", "World", "!"}
	for _, str := range writes {
		buf.Write([]byte(str))
	}

	expected := "Hello World!"
	if string(buf.Bytes()) != expected {
		t.Errorf("Buffer content = %s, want %s", buf.Bytes(), expected)
	}
}

func BenchmarkBufferWrite(b *testing.B) {
	buf := &types.Buffer{}
	data := []byte("Benchmark test data")

	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		buf.Reset()
		buf.Write(data)
	}
}

func BenchmarkBufferGrow(b *testing.B) {
	buf := &types.Buffer{}

	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		buf.Reset()
		buf.Grow(1000)
	}
}

func BenchmarkBufferPool(b *testing.B) {
	pool := types.NewBufferPool()
	data := []byte("Pool benchmark data")

	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		buf := pool.Get()
		buf.Write(data)
		buf.Release()
	}
}

func BenchmarkBufferPoolParallel(b *testing.B) {
	pool := types.NewBufferPool()
	data := []byte("Parallel pool benchmark")

	b.RunParallel(func(pb *testing.PB) {
		for pb.Next() {
			buf := pool.Get()
			buf.Write(data)
			pool.Put(buf)
		}
	})
}
