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