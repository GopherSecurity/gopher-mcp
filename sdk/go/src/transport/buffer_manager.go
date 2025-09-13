// Package transport provides communication transports for the MCP Filter SDK.
package transport

import (
	"bytes"
	"fmt"
	"sync"
	"sync/atomic"
)

// BufferManager manages buffer allocation and sizing for transport operations.
type BufferManager struct {
	// Configuration
	minSize      int
	maxSize      int
	defaultSize  int
	growthFactor float64
	shrinkFactor float64

	// Buffer pools by size
	pools map[int]*sync.Pool

	// Statistics
	allocations    atomic.Int64
	resizes        atomic.Int64
	overflows      atomic.Int64
	totalAllocated atomic.Int64

	// Dynamic sizing
	commonSizes   []int
	sizeHistogram map[int]int

	mu sync.RWMutex
}

// BufferManagerConfig configures buffer management behavior.
type BufferManagerConfig struct {
	MinSize      int     // Minimum buffer size
	MaxSize      int     // Maximum buffer size
	DefaultSize  int     // Default allocation size
	GrowthFactor float64 // Growth multiplier for resize
	ShrinkFactor float64 // Shrink threshold
	PoolSizes    []int   // Pre-configured pool sizes
}

// DefaultBufferManagerConfig returns default configuration.
func DefaultBufferManagerConfig() BufferManagerConfig {
	return BufferManagerConfig{
		MinSize:      512,
		MaxSize:      16 * 1024 * 1024, // 16MB
		DefaultSize:  4096,
		GrowthFactor: 2.0,
		ShrinkFactor: 0.25,
		PoolSizes:    []int{512, 1024, 4096, 8192, 16384, 65536},
	}
}

// NewBufferManager creates a new buffer manager.
func NewBufferManager(config BufferManagerConfig) *BufferManager {
	bm := &BufferManager{
		minSize:       config.MinSize,
		maxSize:       config.MaxSize,
		defaultSize:   config.DefaultSize,
		growthFactor:  config.GrowthFactor,
		shrinkFactor:  config.ShrinkFactor,
		pools:         make(map[int]*sync.Pool),
		commonSizes:   config.PoolSizes,
		sizeHistogram: make(map[int]int),
	}

	// Initialize pools for common sizes
	for _, size := range config.PoolSizes {
		bm.pools[size] = &sync.Pool{
			New: func() interface{} {
				return &ManagedBuffer{
					Buffer:   bytes.NewBuffer(make([]byte, 0, size)),
					manager:  bm,
					capacity: size,
				}
			},
		}
	}

	return bm
}

// ManagedBuffer wraps a bytes.Buffer with management metadata.
type ManagedBuffer struct {
	*bytes.Buffer
	manager  *BufferManager
	capacity int
	resized  bool
}

// Acquire gets a buffer of at least the specified size.
func (bm *BufferManager) Acquire(minSize int) *ManagedBuffer {
	bm.allocations.Add(1)
	bm.totalAllocated.Add(int64(minSize))

	// Track size for optimization
	bm.recordSize(minSize)

	// Find appropriate pool size
	poolSize := bm.findPoolSize(minSize)

	// Get from pool or create new
	if pool, exists := bm.pools[poolSize]; exists {
		if buf := pool.Get(); buf != nil {
			mb := buf.(*ManagedBuffer)
			mb.Reset()
			return mb
		}
	}

	// Create new buffer
	return &ManagedBuffer{
		Buffer:   bytes.NewBuffer(make([]byte, 0, poolSize)),
		manager:  bm,
		capacity: poolSize,
	}
}

// Release returns a buffer to the pool.
func (bm *BufferManager) Release(buf *ManagedBuffer) {
	if buf == nil {
		return
	}

	// Don't pool oversized buffers
	if buf.capacity > bm.maxSize {
		return
	}

	// Return to appropriate pool
	if pool, exists := bm.pools[buf.capacity]; exists {
		buf.Reset()
		pool.Put(buf)
	}
}

// Resize adjusts buffer capacity if needed.
func (bm *BufferManager) Resize(buf *ManagedBuffer, newSize int) (*ManagedBuffer, error) {
	if newSize > bm.maxSize {
		bm.overflows.Add(1)
		return nil, fmt.Errorf("requested size %d exceeds maximum %d", newSize, bm.maxSize)
	}

	if newSize <= buf.capacity {
		return buf, nil
	}

	bm.resizes.Add(1)

	// Calculate new capacity with growth factor
	newCapacity := int(float64(buf.capacity) * bm.growthFactor)
	if newCapacity < newSize {
		newCapacity = newSize
	}
	if newCapacity > bm.maxSize {
		newCapacity = bm.maxSize
	}

	// Create new buffer and copy data
	newBuf := bm.Acquire(newCapacity)
	newBuf.Write(buf.Bytes())

	// Mark old buffer for release
	buf.resized = true

	return newBuf, nil
}

// findPoolSize finds the appropriate pool size for a given minimum size.
func (bm *BufferManager) findPoolSize(minSize int) int {
	// Use default if very small
	if minSize <= bm.defaultSize {
		return bm.defaultSize
	}

	// Find smallest pool that fits
	for _, size := range bm.commonSizes {
		if size >= minSize {
			return size
		}
	}

	// Round up to power of 2 for sizes not in pools
	capacity := 1
	for capacity < minSize {
		capacity *= 2
	}

	if capacity > bm.maxSize {
		return bm.maxSize
	}

	return capacity
}

// recordSize tracks size usage for optimization.
func (bm *BufferManager) recordSize(size int) {
	bm.mu.Lock()
	defer bm.mu.Unlock()

	// Round to nearest bucket
	bucket := ((size + 511) / 512) * 512
	bm.sizeHistogram[bucket]++

	// Periodically optimize pool sizes
	if bm.allocations.Load()%1000 == 0 {
		bm.optimizePools()
	}
}

// optimizePools adjusts pool sizes based on usage patterns.
func (bm *BufferManager) optimizePools() {
	// Find most common sizes
	type sizeCount struct {
		size  int
		count int
	}

	var sizes []sizeCount
	for size, count := range bm.sizeHistogram {
		sizes = append(sizes, sizeCount{size, count})
	}

	// Sort by frequency
	for i := 0; i < len(sizes); i++ {
		for j := i + 1; j < len(sizes); j++ {
			if sizes[j].count > sizes[i].count {
				sizes[i], sizes[j] = sizes[j], sizes[i]
			}
		}
	}

	// Update common sizes with top entries
	newCommon := make([]int, 0, len(bm.commonSizes))
	for i := 0; i < len(sizes) && i < cap(newCommon); i++ {
		newCommon = append(newCommon, sizes[i].size)
	}

	// Add new pools for frequently used sizes
	for _, size := range newCommon {
		if _, exists := bm.pools[size]; !exists {
			bm.pools[size] = &sync.Pool{
				New: func() interface{} {
					return &ManagedBuffer{
						Buffer:   bytes.NewBuffer(make([]byte, 0, size)),
						manager:  bm,
						capacity: size,
					}
				},
			}
		}
	}

	bm.commonSizes = newCommon
}

// ShouldShrink checks if buffer should be shrunk.
func (bm *BufferManager) ShouldShrink(buf *ManagedBuffer) bool {
	used := buf.Len()
	capacity := buf.capacity

	if capacity <= bm.defaultSize {
		return false
	}

	utilization := float64(used) / float64(capacity)
	return utilization < bm.shrinkFactor
}

// Shrink reduces buffer size if underutilized.
func (bm *BufferManager) Shrink(buf *ManagedBuffer) *ManagedBuffer {
	if !bm.ShouldShrink(buf) {
		return buf
	}

	// Calculate new size
	newSize := buf.Len() * 2
	if newSize < bm.defaultSize {
		newSize = bm.defaultSize
	}

	// Create smaller buffer
	newBuf := bm.Acquire(newSize)
	newBuf.Write(buf.Bytes())

	// Release old buffer
	bm.Release(buf)

	return newBuf
}

// Stats returns buffer manager statistics.
func (bm *BufferManager) Stats() BufferStats {
	bm.mu.RLock()
	defer bm.mu.RUnlock()

	return BufferStats{
		Allocations:    bm.allocations.Load(),
		Resizes:        bm.resizes.Load(),
		Overflows:      bm.overflows.Load(),
		TotalAllocated: bm.totalAllocated.Load(),
		PoolCount:      len(bm.pools),
		CommonSizes:    append([]int{}, bm.commonSizes...),
	}
}

// BufferStats contains buffer management statistics.
type BufferStats struct {
	Allocations    int64
	Resizes        int64
	Overflows      int64
	TotalAllocated int64
	PoolCount      int
	CommonSizes    []int
}

// OptimizeForMessageSize adjusts configuration based on observed message sizes.
func (bm *BufferManager) OptimizeForMessageSize(avgSize, maxSize int) {
	bm.mu.Lock()
	defer bm.mu.Unlock()

	// Adjust default size
	if avgSize > 0 && avgSize != bm.defaultSize {
		bm.defaultSize = ((avgSize + 511) / 512) * 512 // Round to 512 bytes
	}

	// Adjust max size if needed
	if maxSize > bm.maxSize {
		bm.maxSize = maxSize
	}

	// Create pool for average size if not exists
	if _, exists := bm.pools[bm.defaultSize]; !exists {
		bm.pools[bm.defaultSize] = &sync.Pool{
			New: func() interface{} {
				return &ManagedBuffer{
					Buffer:   bytes.NewBuffer(make([]byte, 0, bm.defaultSize)),
					manager:  bm,
					capacity: bm.defaultSize,
				}
			},
		}
	}
}

// Reset clears statistics and optimizations.
func (bm *BufferManager) Reset() {
	bm.mu.Lock()
	defer bm.mu.Unlock()

	bm.allocations.Store(0)
	bm.resizes.Store(0)
	bm.overflows.Store(0)
	bm.totalAllocated.Store(0)
	bm.sizeHistogram = make(map[int]int)
}
