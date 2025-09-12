// Package core provides the core interfaces and types for the MCP Filter SDK.
package core

import (
	"sort"
	"sync"

	"github.com/GopherSecurity/gopher-mcp/src/types"
)

// BufferPool manages multiple buffer pools of different sizes.
type BufferPool struct {
	// pools maps size to sync.Pool
	pools map[int]*sync.Pool

	// sizes contains sorted pool sizes for efficient lookup
	sizes []int

	// stats tracks pool usage statistics
	stats types.PoolStatistics

	// minSize is the minimum buffer size
	minSize int

	// maxSize is the maximum buffer size
	maxSize int

	// mu protects concurrent access
	mu sync.RWMutex
}

// NewBufferPool creates a new buffer pool with power-of-2 sizes.
func NewBufferPool(minSize, maxSize int) *BufferPool {
	bp := &BufferPool{
		pools:   make(map[int]*sync.Pool),
		sizes:   make([]int, 0),
		minSize: minSize,
		maxSize: maxSize,
	}

	// Create pools for power-of-2 sizes
	for size := minSize; size <= maxSize; size *= 2 {
		bp.sizes = append(bp.sizes, size)
		poolSize := size // Capture size for closure
		bp.pools[size] = &sync.Pool{
			New: func() interface{} {
				return &types.Buffer{}
			},
		}
	}

	// Ensure sizes are sorted
	sort.Ints(bp.sizes)

	return bp
}

// SimpleBufferPool implements the BufferPool interface with basic pooling.
type SimpleBufferPool struct {
	pool sync.Pool
	size int
	stats types.PoolStatistics
	mu sync.Mutex
}

// NewSimpleBufferPool creates a new buffer pool for the specified size.
func NewSimpleBufferPool(size int) *SimpleBufferPool {
	bp := &SimpleBufferPool{
		size: size,
		stats: types.PoolStatistics{},
	}
	
	bp.pool = sync.Pool{
		New: func() interface{} {
			bp.mu.Lock()
			bp.stats.Misses++
			bp.mu.Unlock()
			
			return &types.Buffer{}
		},
	}
	
	return bp
}

// Get retrieves a buffer from the pool with at least the specified size.
func (bp *SimpleBufferPool) Get(size int) *types.Buffer {
	bp.mu.Lock()
	bp.stats.Gets++
	bp.mu.Unlock()

	buffer := bp.pool.Get().(*types.Buffer)
	if buffer.Cap() < size {
		buffer.Grow(size - buffer.Cap())
	}
	
	bp.mu.Lock()
	bp.stats.Hits++
	bp.mu.Unlock()
	
	return buffer
}

// Put returns a buffer to the pool for reuse.
func (bp *SimpleBufferPool) Put(buffer *types.Buffer) {
	if buffer == nil {
		return
	}
	
	buffer.Reset()
	bp.pool.Put(buffer)
	
	bp.mu.Lock()
	bp.stats.Puts++
	bp.mu.Unlock()
}

// Stats returns statistics about the pool's usage.
func (bp *SimpleBufferPool) Stats() types.PoolStatistics {
	bp.mu.Lock()
	defer bp.mu.Unlock()
	return bp.stats
}