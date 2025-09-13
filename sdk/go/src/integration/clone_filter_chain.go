// Package integration provides MCP SDK integration.
package integration

import (
	"fmt"
	"sync"
	"sync/atomic"
	"time"
)

// CloneOptions configures chain cloning.
type CloneOptions struct {
	DeepCopy        bool
	ClearStatistics bool
	NewID           string
	NewName         string
	ModifyFilters   []FilterModification
	ExcludeFilters  []string
	IncludeOnly     []string
	ReverseOrder    bool
	ShareResources  bool
}

// FilterModification specifies how to modify a filter during cloning.
type FilterModification struct {
	FilterID     string
	NewConfig    map[string]interface{}
	ReplaceWith  Filter
	InsertBefore Filter
	InsertAfter  Filter
}

// ClonedChain represents a cloned filter chain.
type ClonedChain struct {
	Original        *FilterChain
	Clone           *FilterChain
	CloneTime       time.Time
	Modifications   []string
	SharedResources bool
}

// CloneFilterChain creates a copy of an existing filter chain.
func (fc *FilteredMCPClient) CloneFilterChain(
	chainID string,
	options CloneOptions,
) (*ClonedChain, error) {
	// Find original chain
	original := fc.findChain(chainID)
	if original == nil {
		return nil, fmt.Errorf("chain not found: %s", chainID)
	}

	// Create clone
	clone := &FilterChain{
		id:           generateChainID(),
		name:         original.name + "_clone",
		description:  original.description,
		mode:         original.mode,
		filters:      []Filter{},
		mu:           sync.RWMutex{},
		createdAt:    time.Now(),
		lastModified: time.Now(),
		tags:         make(map[string]string),
	}

	// Apply custom ID and name if provided
	if options.NewID != "" {
		clone.id = options.NewID
	}
	if options.NewName != "" {
		clone.name = options.NewName
	}

	// Clone configuration
	clone.maxFilters = original.maxFilters
	clone.timeout = original.timeout
	clone.retryPolicy = original.retryPolicy
	clone.cacheEnabled = original.cacheEnabled
	clone.cacheTTL = original.cacheTTL
	clone.maxConcurrency = original.maxConcurrency
	clone.bufferSize = original.bufferSize

	// Copy tags
	for k, v := range original.tags {
		clone.tags[k] = v
	}

	// Clone filters
	modifications := []string{}
	err := fc.cloneFilters(original, clone, options, &modifications)
	if err != nil {
		return nil, fmt.Errorf("failed to clone filters: %w", err)
	}

	// Apply filter order modification
	if options.ReverseOrder {
		fc.reverseFilters(clone)
		modifications = append(modifications, "Reversed filter order")
	}

	// Clear statistics if requested
	if options.ClearStatistics {
		fc.clearChainStatistics(clone)
		modifications = append(modifications, "Cleared statistics")
	}

	// Register cloned chain
	fc.mu.Lock()
	if fc.customChains == nil {
		fc.customChains = make(map[string]*FilterChain)
	}
	fc.customChains[clone.id] = clone
	fc.mu.Unlock()

	// Create clone result
	result := &ClonedChain{
		Original:        original,
		Clone:           clone,
		CloneTime:       time.Now(),
		Modifications:   modifications,
		SharedResources: options.ShareResources,
	}

	return result, nil
}

// cloneFilters clones filters from original to clone chain.
func (fc *FilteredMCPClient) cloneFilters(
	original, clone *FilterChain,
	options CloneOptions,
	modifications *[]string,
) error {
	// Build filter inclusion/exclusion map
	includeMap := make(map[string]bool)
	excludeMap := make(map[string]bool)

	if len(options.IncludeOnly) > 0 {
		for _, id := range options.IncludeOnly {
			includeMap[id] = true
		}
	}

	for _, id := range options.ExcludeFilters {
		excludeMap[id] = true
	}

	// Clone each filter
	for _, filter := range original.filters {
		filterID := filter.GetID()

		// Check inclusion/exclusion
		if len(includeMap) > 0 && !includeMap[filterID] {
			*modifications = append(*modifications, fmt.Sprintf("Excluded filter: %s", filter.GetName()))
			continue
		}
		if excludeMap[filterID] {
			*modifications = append(*modifications, fmt.Sprintf("Excluded filter: %s", filter.GetName()))
			continue
		}

		// Check for modifications
		var clonedFilter Filter
		modified := false

		for _, mod := range options.ModifyFilters {
			if mod.FilterID == filterID {
				if mod.ReplaceWith != nil {
					// Replace filter entirely
					clonedFilter = mod.ReplaceWith
					*modifications = append(*modifications, fmt.Sprintf("Replaced filter: %s", filter.GetName()))
					modified = true
					break
				}

				// Clone and modify
				if options.DeepCopy {
					clonedFilter = fc.deepCloneFilter(filter)
				} else {
					clonedFilter = fc.shallowCloneFilter(filter)
				}

				// Apply configuration changes
				if mod.NewConfig != nil {
					clonedFilter.UpdateConfig(mod.NewConfig)
					*modifications = append(*modifications, fmt.Sprintf("Modified config for: %s", filter.GetName()))
				}

				// Handle insertions
				if mod.InsertBefore != nil {
					clone.Add(mod.InsertBefore)
					*modifications = append(*modifications, fmt.Sprintf("Inserted filter before: %s", filter.GetName()))
				}

				modified = true

				// Add the modified filter
				clone.Add(clonedFilter)

				if mod.InsertAfter != nil {
					clone.Add(mod.InsertAfter)
					*modifications = append(*modifications, fmt.Sprintf("Inserted filter after: %s", filter.GetName()))
				}

				break
			}
		}

		// If not modified, clone normally
		if !modified {
			if options.DeepCopy {
				clonedFilter = fc.deepCloneFilter(filter)
			} else {
				clonedFilter = fc.shallowCloneFilter(filter)
			}
			clone.Add(clonedFilter)
		}
	}

	return nil
}

// deepCloneFilter creates a deep copy of a filter.
func (fc *FilteredMCPClient) deepCloneFilter(filter Filter) Filter {
	// Create new filter instance with copied state
	cloned := filter.Clone()

	// Generate new ID for deep copy
	cloned.SetID(generateFilterID())

	// Clone configuration deeply
	config := filter.GetConfiguration()
	newConfig := make(map[string]interface{})
	for k, v := range config {
		newConfig[k] = deepCopyValue(v)
	}
	cloned.UpdateConfig(newConfig)

	return cloned
}

// shallowCloneFilter creates a shallow copy of a filter.
func (fc *FilteredMCPClient) shallowCloneFilter(filter Filter) Filter {
	// Return reference to same filter (shared)
	if fc.isStatelessFilter(filter) {
		return filter
	}

	// For stateful filters, create new instance
	return filter.Clone()
}

// isStatelessFilter checks if filter is stateless.
func (fc *FilteredMCPClient) isStatelessFilter(filter Filter) bool {
	// Check if filter maintains state
	return filter.IsStateless()
}

// reverseFilters reverses the order of filters in a chain.
func (fc *FilteredMCPClient) reverseFilters(chain *FilterChain) {
	n := len(chain.filters)
	for i := 0; i < n/2; i++ {
		chain.filters[i], chain.filters[n-1-i] = chain.filters[n-1-i], chain.filters[i]
	}
}

// clearChainStatistics clears statistics for a chain.
func (fc *FilteredMCPClient) clearChainStatistics(chain *FilterChain) {
	chainID := chain.GetID()

	fc.metricsCollector.mu.Lock()
	defer fc.metricsCollector.mu.Unlock()

	// Clear chain metrics
	delete(fc.metricsCollector.chainMetrics, chainID)

	// Clear filter metrics for chain filters
	for _, filter := range chain.filters {
		delete(fc.metricsCollector.filterMetrics, filter.GetID())
	}
}

// findChain finds a chain by ID.
func (fc *FilteredMCPClient) findChain(chainID string) *FilterChain {
	// Check standard chains
	switch chainID {
	case "request":
		return fc.requestChain
	case "response":
		return fc.responseChain
	case "notification":
		return fc.notificationChain
	}

	// Check custom chains
	fc.mu.RLock()
	defer fc.mu.RUnlock()

	if fc.customChains != nil {
		return fc.customChains[chainID]
	}

	return nil
}

// MergeChains merges multiple chains into one.
func (fc *FilteredMCPClient) MergeChains(chainIDs []string, name string) (*FilterChain, error) {
	if len(chainIDs) == 0 {
		return nil, fmt.Errorf("no chains to merge")
	}

	// Create new chain
	merged := &FilterChain{
		id:           generateChainID(),
		name:         name,
		description:  "Merged chain",
		filters:      []Filter{},
		mu:           sync.RWMutex{},
		createdAt:    time.Now(),
		lastModified: time.Now(),
		tags:         make(map[string]string),
	}

	// Merge filters from all chains
	for _, chainID := range chainIDs {
		chain := fc.findChain(chainID)
		if chain == nil {
			return nil, fmt.Errorf("chain not found: %s", chainID)
		}

		// Add all filters from this chain
		for _, filter := range chain.filters {
			merged.Add(fc.shallowCloneFilter(filter))
		}

		// Merge tags
		for k, v := range chain.tags {
			merged.tags[k] = v
		}
	}

	// Register merged chain
	fc.mu.Lock()
	if fc.customChains == nil {
		fc.customChains = make(map[string]*FilterChain)
	}
	fc.customChains[merged.id] = merged
	fc.mu.Unlock()

	return merged, nil
}

// Helper functions
func generateChainID() string {
	return fmt.Sprintf("chain_%d", chainIDCounter.Add(1))
}

func generateFilterID() string {
	return fmt.Sprintf("filter_%d", filterIDCounter.Add(1))
}

var (
	chainIDCounter  atomic.Int64
	filterIDCounter atomic.Int64
)

func deepCopyValue(v interface{}) interface{} {
	// Implementation would handle deep copying of various types
	return v
}
