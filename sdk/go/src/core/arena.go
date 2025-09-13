// Package core provides the core interfaces and types for the MCP Filter SDK.
package core

import (
	"sync"
)

// Arena provides efficient batch memory allocation within a scope.
// It allocates memory in large chunks and sub-allocates from them,
// reducing allocation overhead for many small allocations.
//
// Arena is useful for:
//   - Temporary allocations that are freed together
//   - Reducing GC pressure from many small allocations
//   - Improving cache locality for related data
type Arena struct {
	// chunks holds all allocated memory chunks
	chunks [][]byte

	// current is the active chunk being allocated from
	current []byte

	// offset is the current position in the active chunk
	offset int

	// chunkSize is the size of each chunk to allocate
	chunkSize int

	// totalAllocated tracks total memory allocated
	totalAllocated int64

	// mu protects concurrent access
	mu sync.Mutex
}

// NewArena creates a new arena with the specified chunk size.
func NewArena(chunkSize int) *Arena {
	if chunkSize <= 0 {
		chunkSize = 64 * 1024 // Default 64KB chunks
	}

	return &Arena{
		chunks:    make([][]byte, 0),
		chunkSize: chunkSize,
	}
}

// Allocate returns a byte slice of the requested size from the arena.
// The returned slice is only valid until Reset() or Destroy() is called.
func (a *Arena) Allocate(size int) []byte {
	a.mu.Lock()
	defer a.mu.Unlock()

	// Check if we need a new chunk
	if a.current == nil || a.offset+size > len(a.current) {
		// Allocate new chunk
		chunkSize := a.chunkSize
		if size > chunkSize {
			chunkSize = size // Ensure chunk is large enough
		}

		chunk := make([]byte, chunkSize)
		a.chunks = append(a.chunks, chunk)
		a.current = chunk
		a.offset = 0
		a.totalAllocated += int64(chunkSize)
	}

	// Sub-allocate from current chunk
	result := a.current[a.offset : a.offset+size]
	a.offset += size

	return result
}

// Reset clears all allocations but keeps chunks for reuse.
// This is efficient when the arena will be used again.
func (a *Arena) Reset() {
	a.mu.Lock()
	defer a.mu.Unlock()

	// Keep first chunk if it exists
	if len(a.chunks) > 0 {
		a.current = a.chunks[0]
		a.chunks = a.chunks[:1]
		a.offset = 0
	} else {
		a.current = nil
		a.offset = 0
	}
}

// Destroy releases all memory held by the arena.
// The arena should not be used after calling Destroy.
func (a *Arena) Destroy() {
	a.mu.Lock()
	defer a.mu.Unlock()

	a.chunks = nil
	a.current = nil
	a.offset = 0
	a.totalAllocated = 0
}

// TotalAllocated returns the total memory allocated by the arena.
func (a *Arena) TotalAllocated() int64 {
	a.mu.Lock()
	defer a.mu.Unlock()
	return a.totalAllocated
}
