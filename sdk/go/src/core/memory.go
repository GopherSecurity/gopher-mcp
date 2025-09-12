// Package core provides the core interfaces and types for the MCP Filter SDK.
package core

import (
	"sync"
	"sync/atomic"

	"github.com/GopherSecurity/gopher-mcp/src/types"
)

// MemoryStatistics tracks memory usage and allocation patterns.
type MemoryStatistics struct {
	// TotalAllocated is the total bytes allocated
	TotalAllocated uint64

	// TotalReleased is the total bytes released
	TotalReleased uint64

	// CurrentUsage is the current memory usage in bytes
	CurrentUsage int64

	// PeakUsage is the maximum memory usage observed
	PeakUsage int64

	// AllocationCount is the number of allocations made
	AllocationCount uint64

	// ReleaseCount is the number of releases made
	ReleaseCount uint64

	// PoolHits is the number of times a buffer was reused from pool
	PoolHits uint64

	// PoolMisses is the number of times a new buffer had to be allocated
	PoolMisses uint64
}

// MemoryManager manages buffer pools and tracks memory usage across the system.
// It provides centralized memory management with size-based pooling and statistics.
//
// Features:
//   - Multiple buffer pools for different size categories
//   - Memory usage limits and monitoring
//   - Allocation statistics and metrics
//   - Thread-safe operations
type MemoryManager struct {
	// pools maps buffer sizes to their respective pools
	// Key is the buffer size, value is the pool for that size
	pools map[int]*SimpleBufferPool

	// maxMemory is the maximum allowed memory usage in bytes
	maxMemory int64

	// currentUsage tracks the current memory usage
	// Use atomic operations for thread-safe access
	currentUsage int64

	// stats contains memory usage statistics
	stats MemoryStatistics

	// mu protects concurrent access to pools map and stats
	mu sync.RWMutex
}

// NewMemoryManager creates a new memory manager with the specified memory limit.
func NewMemoryManager(maxMemory int64) *MemoryManager {
	return &MemoryManager{
		pools:     make(map[int]*SimpleBufferPool),
		maxMemory: maxMemory,
		stats:     MemoryStatistics{},
	}
}

// GetCurrentUsage returns the current memory usage atomically.
func (mm *MemoryManager) GetCurrentUsage() int64 {
	return atomic.LoadInt64(&mm.currentUsage)
}

// UpdateUsage atomically updates the current memory usage.
func (mm *MemoryManager) UpdateUsage(delta int64) {
	newUsage := atomic.AddInt64(&mm.currentUsage, delta)
	
	// Update peak usage if necessary
	mm.mu.Lock()
	if newUsage > mm.stats.PeakUsage {
		mm.stats.PeakUsage = newUsage
	}
	mm.stats.CurrentUsage = newUsage
	mm.mu.Unlock()
}

// GetStats returns a copy of the current memory statistics.
func (mm *MemoryManager) GetStats() MemoryStatistics {
	mm.mu.RLock()
	defer mm.mu.RUnlock()
	return mm.stats
}

// Buffer pool size categories
const (
	// SmallBufferSize is for small data operations (512 bytes)
	SmallBufferSize = 512

	// MediumBufferSize is for typical data operations (4KB)
	MediumBufferSize = 4 * 1024

	// LargeBufferSize is for large data operations (64KB)
	LargeBufferSize = 64 * 1024

	// HugeBufferSize is for very large data operations (1MB)
	HugeBufferSize = 1024 * 1024
)

// PoolConfig defines configuration for a buffer pool.
type PoolConfig struct {
	// Size is the buffer size for this pool
	Size int

	// MinBuffers is the minimum number of buffers to keep in pool
	MinBuffers int

	// MaxBuffers is the maximum number of buffers in pool
	MaxBuffers int

	// GrowthFactor determines how pool grows (e.g., 2.0 for doubling)
	GrowthFactor float64
}

// DefaultPoolConfigs returns default configurations for standard buffer pools.
func DefaultPoolConfigs() []PoolConfig {
	return []PoolConfig{
		{
			Size:         SmallBufferSize,
			MinBuffers:   10,
			MaxBuffers:   100,
			GrowthFactor: 2.0,
		},
		{
			Size:         MediumBufferSize,
			MinBuffers:   5,
			MaxBuffers:   50,
			GrowthFactor: 1.5,
		},
		{
			Size:         LargeBufferSize,
			MinBuffers:   2,
			MaxBuffers:   20,
			GrowthFactor: 1.5,
		},
		{
			Size:         HugeBufferSize,
			MinBuffers:   1,
			MaxBuffers:   10,
			GrowthFactor: 1.2,
		},
	}
}

// InitializePools sets up the standard buffer pools with default configurations.
func (mm *MemoryManager) InitializePools() {
	mm.mu.Lock()
	defer mm.mu.Unlock()

	configs := DefaultPoolConfigs()
	for _, config := range configs {
		pool := NewSimpleBufferPool(config.Size)
		mm.pools[config.Size] = pool
	}
}

// GetPoolForSize returns the appropriate pool for the given size.
// It finds the smallest pool that can accommodate the requested size.
func (mm *MemoryManager) GetPoolForSize(size int) *SimpleBufferPool {
	mm.mu.RLock()
	defer mm.mu.RUnlock()

	// Find the appropriate pool size
	poolSize := mm.selectPoolSize(size)
	return mm.pools[poolSize]
}

// selectPoolSize determines which pool size to use for a given request.
func (mm *MemoryManager) selectPoolSize(size int) int {
	switch {
	case size <= SmallBufferSize:
		return SmallBufferSize
	case size <= MediumBufferSize:
		return MediumBufferSize
	case size <= LargeBufferSize:
		return LargeBufferSize
	case size <= HugeBufferSize:
		return HugeBufferSize
	default:
		// For sizes larger than huge, use exact size
		return size
	}
}

// Get retrieves a buffer of at least the specified size.
// It selects the appropriate pool based on size and tracks memory usage.
//
// Parameters:
//   - size: The minimum size of the buffer needed
//
// Returns:
//   - *types.Buffer: A buffer with at least the requested capacity
func (mm *MemoryManager) Get(size int) *types.Buffer {
	// Check memory limit
	currentUsage := atomic.LoadInt64(&mm.currentUsage)
	if mm.maxMemory > 0 && currentUsage+int64(size) > mm.maxMemory {
		// Memory limit exceeded
		return nil
	}

	// Get the appropriate pool
	pool := mm.GetPoolForSize(size)
	
	var buffer *types.Buffer
	if pool != nil {
		// Get from pool
		buffer = pool.Get(size)
		
		mm.mu.Lock()
		mm.stats.PoolHits++
		mm.mu.Unlock()
	} else {
		// No pool for this size, allocate directly
		buffer = &types.Buffer{}
		buffer.Grow(size)
		
		mm.mu.Lock()
		mm.stats.PoolMisses++
		mm.mu.Unlock()
	}

	// Update memory usage
	if buffer != nil {
		mm.UpdateUsage(int64(buffer.Cap()))
		
		mm.mu.Lock()
		mm.stats.AllocationCount++
		mm.stats.TotalAllocated += uint64(buffer.Cap())
		mm.mu.Unlock()
	}

	return buffer
}

// Put returns a buffer to the appropriate pool for reuse.
// The buffer is cleared for security before being pooled.
// If memory limit is exceeded, the buffer may be released instead of pooled.
//
// Parameters:
//   - buffer: The buffer to return to the pool
func (mm *MemoryManager) Put(buffer *types.Buffer) {
	if buffer == nil {
		return
	}

	// Clear buffer contents for security
	buffer.Reset()
	
	// Update memory usage
	bufferSize := buffer.Cap()
	mm.UpdateUsage(-int64(bufferSize))
	
	mm.mu.Lock()
	mm.stats.ReleaseCount++
	mm.stats.TotalReleased += uint64(bufferSize)
	mm.mu.Unlock()

	// Check if we should pool or release
	currentUsage := atomic.LoadInt64(&mm.currentUsage)
	if mm.maxMemory > 0 && currentUsage > mm.maxMemory*80/100 {
		// Over 80% memory usage, release buffer instead of pooling
		// This helps reduce memory pressure
		return
	}

	// Return to appropriate pool
	poolSize := mm.selectPoolSize(bufferSize)
	pool := mm.GetPoolForSize(bufferSize)
	
	if pool != nil && poolSize == bufferSize {
		// Only return to pool if it matches the pool size exactly
		pool.Put(buffer)
	}
	// Otherwise let the buffer be garbage collected
}

// SetMaxMemory updates the maximum memory limit.
// Setting to 0 disables the memory limit.
func (mm *MemoryManager) SetMaxMemory(bytes int64) {
	atomic.StoreInt64(&mm.maxMemory, bytes)
	
	// Trigger cleanup if over limit
	if bytes > 0 {
		currentUsage := atomic.LoadInt64(&mm.currentUsage)
		if currentUsage > bytes {
			mm.triggerCleanup()
		}
	}
}

// GetMaxMemory returns the current memory limit.
func (mm *MemoryManager) GetMaxMemory() int64 {
	return atomic.LoadInt64(&mm.maxMemory)
}

// triggerCleanup attempts to free memory when approaching limit.
func (mm *MemoryManager) triggerCleanup() {
	mm.mu.Lock()
	defer mm.mu.Unlock()

	// Clear pools to free memory
	for size, pool := range mm.pools {
		// Create new empty pool
		mm.pools[size] = NewSimpleBufferPool(size)
		_ = pool // Old pool will be garbage collected
	}
}

// CheckMemoryLimit returns true if allocation would exceed limit.
func (mm *MemoryManager) CheckMemoryLimit(size int) bool {
	maxMem := atomic.LoadInt64(&mm.maxMemory)
	if maxMem <= 0 {
		return false // No limit
	}

	currentUsage := atomic.LoadInt64(&mm.currentUsage)
	return currentUsage+int64(size) > maxMem
}