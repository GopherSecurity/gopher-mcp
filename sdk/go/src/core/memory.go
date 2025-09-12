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
	pools map[int]*types.BufferPool

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
		pools:     make(map[int]*types.BufferPool),
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
		pool := &types.BufferPool{}
		// Initialize the pool with the configuration
		// In a real implementation, the BufferPool would use these configs
		mm.pools[config.Size] = pool
	}
}

// GetPoolForSize returns the appropriate pool for the given size.
// It finds the smallest pool that can accommodate the requested size.
func (mm *MemoryManager) GetPoolForSize(size int) *types.BufferPool {
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