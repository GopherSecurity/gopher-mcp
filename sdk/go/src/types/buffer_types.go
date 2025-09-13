// Package types provides core type definitions for the MCP Filter SDK.
package types

import "sync"

// BufferPool manages a pool of reusable buffers.
type BufferPool struct {
	pool sync.Pool
}

// NewBufferPool creates a new buffer pool.
func NewBufferPool() *BufferPool {
	return &BufferPool{
		pool: sync.Pool{
			New: func() interface{} {
				return &Buffer{
					data:     make([]byte, 0, 4096),
					capacity: 4096,
				}
			},
		},
	}
}

// Get retrieves a buffer from the pool.
func (p *BufferPool) Get() *Buffer {
	if p == nil {
		return nil
	}
	b := p.pool.Get().(*Buffer)
	b.Reset()
	b.pool = p
	b.pooled = true
	return b
}

// Put returns a buffer to the pool.
func (p *BufferPool) Put(b *Buffer) {
	if p == nil || b == nil {
		return
	}
	b.Reset()
	p.pool.Put(b)
}

// Buffer represents a resizable byte buffer with pooling support.
// It provides efficient memory management for filter data processing.
type Buffer struct {
	// data holds the actual byte data.
	data []byte

	// capacity is the allocated capacity of the buffer.
	capacity int

	// length is the current used length of the buffer.
	length int

	// pooled indicates if this buffer came from a pool.
	pooled bool

	// pool is a reference to the pool that owns this buffer.
	pool *BufferPool
}

// Bytes returns the buffer's data as a byte slice.
func (b *Buffer) Bytes() []byte {
	if b == nil || b.data == nil {
		return nil
	}
	return b.data[:b.length]
}

// Len returns the current length of data in the buffer.
func (b *Buffer) Len() int {
	if b == nil {
		return 0
	}
	return b.length
}

// Cap returns the capacity of the buffer.
func (b *Buffer) Cap() int {
	if b == nil {
		return 0
	}
	return b.capacity
}

// Reset clears the buffer content but keeps the capacity.
func (b *Buffer) Reset() {
	if b != nil {
		b.length = 0
	}
}

// Grow ensures the buffer has at least n more bytes of capacity.
func (b *Buffer) Grow(n int) {
	if b == nil {
		return
	}

	newLen := b.length + n
	if newLen > b.capacity {
		// Need to allocate more space
		newCap := b.capacity * 2
		if newCap < newLen {
			newCap = newLen
		}
		newData := make([]byte, newCap)
		copy(newData, b.data[:b.length])
		b.data = newData
		b.capacity = newCap
	}
}

// Write appends data to the buffer, growing it if necessary.
func (b *Buffer) Write(p []byte) (n int, err error) {
	if b == nil {
		return 0, nil
	}

	b.Grow(len(p))
	copy(b.data[b.length:], p)
	b.length += len(p)
	return len(p), nil
}

// Release returns the buffer to its pool if it's pooled.
func (b *Buffer) Release() {
	if b == nil || !b.pooled || b.pool == nil {
		return
	}

	b.Reset()
	b.pool.Put(b)
}

// SetPool associates this buffer with a pool.
func (b *Buffer) SetPool(pool *BufferPool) {
	if b != nil {
		b.pool = pool
		b.markPooled()
	}
}

// IsPooled returns true if this buffer came from a pool.
func (b *Buffer) IsPooled() bool {
	return b != nil && b.pooled
}

// markPooled marks this buffer as coming from a pool.
// This is an internal method used by pool implementations.
func (b *Buffer) markPooled() {
	if b != nil {
		b.pooled = true
	}
}

// BufferSlice provides a zero-copy view into a Buffer.
// It references a portion of the underlying buffer without copying data.
type BufferSlice struct {
	// buffer is the underlying buffer being sliced.
	buffer *Buffer

	// offset is the starting position in the buffer.
	offset int

	// length is the number of bytes in this slice.
	length int
}

// Bytes returns the slice data without copying.
// This provides direct access to the underlying buffer data.
func (s *BufferSlice) Bytes() []byte {
	if s == nil || s.buffer == nil || s.buffer.data == nil {
		return nil
	}

	// Ensure we don't exceed buffer bounds
	end := s.offset + s.length
	if s.offset >= len(s.buffer.data) {
		return nil
	}
	if end > len(s.buffer.data) {
		end = len(s.buffer.data)
	}

	return s.buffer.data[s.offset:end]
}

// Len returns the length of the slice.
func (s *BufferSlice) Len() int {
	if s == nil {
		return 0
	}
	return s.length
}

// SubSlice creates a new BufferSlice that is a subset of this slice.
// The start and end parameters are relative to this slice, not the underlying buffer.
func (s *BufferSlice) SubSlice(start, end int) BufferSlice {
	if s == nil || start < 0 || end < start || start > s.length {
		return BufferSlice{}
	}

	if end > s.length {
		end = s.length
	}

	return BufferSlice{
		buffer: s.buffer,
		offset: s.offset + start,
		length: end - start,
	}
}

// Slice creates a new BufferSlice with the specified start and end positions.
// This method validates bounds and handles edge cases to prevent panics.
func (s *BufferSlice) Slice(start, end int) BufferSlice {
	if s == nil {
		return BufferSlice{}
	}

	// Validate and adjust bounds
	if start < 0 {
		start = 0
	}
	if end < start {
		return BufferSlice{}
	}
	if start > s.length {
		return BufferSlice{}
	}
	if end > s.length {
		end = s.length
	}

	return BufferSlice{
		buffer: s.buffer,
		offset: s.offset + start,
		length: end - start,
	}
}

// PoolStatistics contains metrics about buffer pool usage.
type PoolStatistics struct {
	// Gets is the number of buffers retrieved from the pool.
	Gets uint64

	// Puts is the number of buffers returned to the pool.
	Puts uint64

	// Hits is the number of times a pooled buffer was reused.
	Hits uint64

	// Misses is the number of times a new buffer had to be created.
	Misses uint64

	// Size is the current number of buffers in the pool.
	Size int
}

// BufferStatistics tracks usage metrics for buffer operations.
// All fields should be accessed atomically in concurrent environments.
type BufferStatistics struct {
	// AllocatedBuffers is the current number of allocated buffers.
	AllocatedBuffers int64

	// PooledBuffers is the current number of buffers in pools.
	PooledBuffers int64

	// TotalAllocations is the cumulative number of buffer allocations.
	TotalAllocations uint64

	// TotalReleases is the cumulative number of buffer releases.
	TotalReleases uint64

	// CurrentUsage is the current memory usage in bytes.
	CurrentUsage int64

	// PeakUsage is the peak memory usage in bytes.
	PeakUsage int64

	// HitRate is the ratio of pool hits to total gets (0.0 to 1.0).
	HitRate float64

	// AverageSize is the average buffer size in bytes.
	AverageSize int64

	// FragmentationRatio is the ratio of unused to total allocated memory.
	FragmentationRatio float64
}

// Calculate computes derived metrics from the raw statistics.
// This should be called periodically to update calculated fields.
func (s *BufferStatistics) Calculate() {
	if s == nil {
		return
	}

	// Calculate hit rate if we have data
	if s.TotalAllocations > 0 {
		hits := s.TotalAllocations - s.TotalReleases
		if hits > 0 {
			s.HitRate = float64(s.PooledBuffers) / float64(hits)
			if s.HitRate > 1.0 {
				s.HitRate = 1.0
			}
		}
	}

	// Calculate average size
	if s.AllocatedBuffers > 0 && s.CurrentUsage > 0 {
		s.AverageSize = s.CurrentUsage / s.AllocatedBuffers
	}

	// Calculate fragmentation ratio
	if s.CurrentUsage > 0 && s.AllocatedBuffers > 0 {
		// Estimate based on average vs actual usage
		expectedUsage := s.AverageSize * s.AllocatedBuffers
		if expectedUsage > s.CurrentUsage {
			s.FragmentationRatio = float64(expectedUsage-s.CurrentUsage) / float64(expectedUsage)
		}
	}
}
