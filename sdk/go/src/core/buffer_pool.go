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

// selectBucket chooses the appropriate pool bucket for a given size.
// It rounds up to the next power of 2 to minimize waste.
func (bp *BufferPool) selectBucket(size int) int {
	// Cap at maxSize
	if size > bp.maxSize {
		return 0 // Signal direct allocation
	}

	// Find next power of 2
	bucket := bp.nextPowerOf2(size)
	
	// Check if bucket exists in our pools
	if _, exists := bp.pools[bucket]; exists {
		return bucket
	}

	// Find nearest available bucket
	for _, poolSize := range bp.sizes {
		if poolSize >= size {
			return poolSize
		}
	}

	return 0 // Fall back to direct allocation
}

// nextPowerOf2 returns the next power of 2 greater than or equal to n.
func (bp *BufferPool) nextPowerOf2(n int) int {
	if n <= 0 {
		return 1
	}
	
	// If n is already a power of 2, return it
	if n&(n-1) == 0 {
		return n
	}

	// Find the next power of 2
	power := 1
	for power < n {
		power <<= 1
	}
	return power
}

// nearestPoolSize finds the smallest pool size >= requested size.
// Uses binary search on the sorted sizes array for efficiency.
func (bp *BufferPool) nearestPoolSize(size int) int {
	bp.mu.RLock()
	defer bp.mu.RUnlock()

	// Handle edge cases
	if len(bp.sizes) == 0 {
		return 0
	}
	if size <= bp.sizes[0] {
		return bp.sizes[0]
	}
	if size > bp.sizes[len(bp.sizes)-1] {
		return 0 // Too large
	}

	// Binary search for the smallest size >= requested
	left, right := 0, len(bp.sizes)-1
	result := bp.sizes[right]

	for left <= right {
		mid := left + (right-left)/2
		
		if bp.sizes[mid] >= size {
			result = bp.sizes[mid]
			right = mid - 1
		} else {
			left = mid + 1
		}
	}

	return result
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