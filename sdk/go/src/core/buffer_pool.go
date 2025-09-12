// Package core provides the core interfaces and types for the MCP Filter SDK.
package core

import (
	"sync"

	"github.com/GopherSecurity/gopher-mcp/src/types"
)

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