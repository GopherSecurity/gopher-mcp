// Package manager provides filter and chain management for the MCP Filter SDK.
package manager

import (
	"sync"
	
	"github.com/google/uuid"
)

// FilterRegistry provides thread-safe filter registration.
type FilterRegistry struct {
	// Primary index by UUID
	filters map[uuid.UUID]Filter
	
	// Secondary index by name
	nameIndex map[string]uuid.UUID
	
	// Synchronization
	mu sync.RWMutex
}

// Filter interface (placeholder)
type Filter interface {
	GetID() uuid.UUID
	GetName() string
	Process(data []byte) ([]byte, error)
	Close() error
}

// NewFilterRegistry creates a new filter registry.
func NewFilterRegistry() *FilterRegistry {
	return &FilterRegistry{
		filters:   make(map[uuid.UUID]Filter),
		nameIndex: make(map[string]uuid.UUID),
	}
}

// Add adds a filter to the registry.
func (fr *FilterRegistry) Add(id uuid.UUID, filter Filter) {
	fr.mu.Lock()
	defer fr.mu.Unlock()
	
	fr.filters[id] = filter
	if name := filter.GetName(); name != "" {
		fr.nameIndex[name] = id
	}
}

// Remove removes a filter from the registry.
func (fr *FilterRegistry) Remove(id uuid.UUID) (Filter, bool) {
	fr.mu.Lock()
	defer fr.mu.Unlock()
	
	filter, exists := fr.filters[id]
	if !exists {
		return nil, false
	}
	
	delete(fr.filters, id)
	if name := filter.GetName(); name != "" {
		delete(fr.nameIndex, name)
	}
	
	return filter, true
}

// Get retrieves a filter by ID.
func (fr *FilterRegistry) Get(id uuid.UUID) (Filter, bool) {
	fr.mu.RLock()
	defer fr.mu.RUnlock()
	
	filter, exists := fr.filters[id]
	return filter, exists
}

// GetByName retrieves a filter by name.
func (fr *FilterRegistry) GetByName(name string) (Filter, bool) {
	fr.mu.RLock()
	defer fr.mu.RUnlock()
	
	id, exists := fr.nameIndex[name]
	if !exists {
		return nil, false
	}
	
	return fr.filters[id], true
}

// CheckNameUniqueness checks if a name is unique.
func (fr *FilterRegistry) CheckNameUniqueness(name string) bool {
	fr.mu.RLock()
	defer fr.mu.RUnlock()
	
	_, exists := fr.nameIndex[name]
	return !exists
}

// GetAll returns all filters.
func (fr *FilterRegistry) GetAll() map[uuid.UUID]Filter {
	fr.mu.RLock()
	defer fr.mu.RUnlock()
	
	result := make(map[uuid.UUID]Filter)
	for id, filter := range fr.filters {
		result[id] = filter
	}
	return result
}

// Count returns the number of registered filters.
func (fr *FilterRegistry) Count() int {
	fr.mu.RLock()
	defer fr.mu.RUnlock()
	
	return len(fr.filters)
}