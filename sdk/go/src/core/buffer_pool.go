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

// Common buffer sizes for pooling (all power-of-2)
var commonBufferSizes = []int{
	512,    // 512B
	1024,   // 1KB
	2048,   // 2KB
	4096,   // 4KB
	8192,   // 8KB
	16384,  // 16KB
	32768,  // 32KB
	65536,  // 64KB
}

// NewBufferPool creates a new buffer pool with power-of-2 sizes.
func NewBufferPool(minSize, maxSize int) *BufferPool {
	bp := &BufferPool{
		pools:   make(map[int]*sync.Pool),
		sizes:   make([]int, 0),
		minSize: minSize,
		maxSize: maxSize,
	}

	// Use common sizes within range
	for _, size := range commonBufferSizes {
		if size >= minSize && size <= maxSize {
			bp.sizes = append(bp.sizes, size)
			poolSize := size // Capture size for closure
			bp.pools[size] = &sync.Pool{
				New: func() interface{} {
					buf := &types.Buffer{}
					buf.Grow(poolSize)
					return buf
				},
			}
		}
	}

	// Ensure sizes are sorted
	sort.Ints(bp.sizes)

	return bp
}

// NewDefaultBufferPool creates a buffer pool with default common sizes.
func NewDefaultBufferPool() *BufferPool {
	return NewBufferPool(512, 65536)
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