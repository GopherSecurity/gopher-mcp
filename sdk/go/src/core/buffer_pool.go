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

// Get retrieves a buffer from the appropriate pool or allocates new.
func (bp *BufferPool) Get(size int) *types.Buffer {
	// Find appropriate pool size
	poolSize := bp.nearestPoolSize(size)
	if poolSize == 0 {
		// Direct allocation for sizes outside pool range
		bp.mu.Lock()
		bp.stats.Misses++
		bp.mu.Unlock()
		
		buf := &types.Buffer{}
		buf.Grow(size)
		return buf
	}

	// Get from pool
	bp.mu.RLock()
	pool, exists := bp.pools[poolSize]
	bp.mu.RUnlock()

	if !exists {
		// Shouldn't happen, but handle gracefully
		buf := &types.Buffer{}
		buf.Grow(size)
		return buf
	}

	// Get buffer from pool
	buf := pool.Get().(*types.Buffer)
	
	// Clear contents for security
	buf.Reset()
	
	// Ensure sufficient capacity
	if buf.Cap() < size {
		buf.Grow(size - buf.Cap())
	}

	// Mark as pooled and update stats
	// Note: We can't directly set the pool since types.BufferPool is different
	// Just mark the buffer as pooled
	
	bp.mu.Lock()
	bp.stats.Gets++
	bp.stats.Hits++
	bp.mu.Unlock()

	return buf
}

// Put returns a buffer to the appropriate pool.
func (bp *BufferPool) Put(buffer *types.Buffer) {
	if buffer == nil {
		return
	}

	// Zero-fill buffer for security
	bp.zeroFill(buffer)
	
	// Clear buffer state
	buffer.Reset()

	// Check if buffer belongs to a pool
	if !buffer.IsPooled() {
		// Non-pooled buffer, let it be garbage collected
		bp.mu.Lock()
		bp.stats.Puts++
		bp.mu.Unlock()
		return
	}

	// Find matching pool by capacity
	bufCap := buffer.Cap()
	poolSize := bp.nearestPoolSize(bufCap)

	// Only return to pool if size matches exactly
	if poolSize != bufCap {
		// Size doesn't match any pool, let it be GC'd
		bp.mu.Lock()
		bp.stats.Puts++
		bp.mu.Unlock()
		return
	}

	bp.mu.RLock()
	pool, exists := bp.pools[poolSize]
	bp.mu.RUnlock()

	if exists {
		// Return to pool
		pool.Put(buffer)
		
		bp.mu.Lock()
		bp.stats.Puts++
		bp.mu.Unlock()
	}
}

// zeroFill securely clears buffer contents.
// Uses optimized methods based on buffer size.
func (bp *BufferPool) zeroFill(buffer *types.Buffer) {
	if buffer == nil || buffer.Len() == 0 {
		return
	}

	data := buffer.Bytes()
	size := len(data)

	// Use different methods based on size
	if size < 4096 {
		// For small buffers, use range loop
		for i := range data {
			data[i] = 0
		}
	} else {
		// For large buffers, use copy with zero slice
		var zero = make([]byte, 4096)
		for i := 0; i < size; i += 4096 {
			end := i + 4096
			if end > size {
				end = size
			}
			copy(data[i:end], zero)
		}
	}
}

// GetStatistics returns pool usage statistics.
func (bp *BufferPool) GetStatistics() types.PoolStatistics {
	bp.mu.RLock()
	defer bp.mu.RUnlock()
	
	stats := bp.stats
	
	// Calculate hit rate
	total := stats.Gets
	if total > 0 {
		hitRate := float64(stats.Hits) / float64(total)
		// Store in a field if PoolStatistics has one
		_ = hitRate
	}
	
	// Calculate current pool sizes
	pooledBuffers := 0
	for _, pool := range bp.pools {
		// Can't directly count sync.Pool items, but track via stats
		_ = pool
		pooledBuffers++
	}
	stats.Size = pooledBuffers
	
	return stats
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