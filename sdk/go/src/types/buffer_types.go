// Package types provides core type definitions for the MCP Filter SDK.
package types

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