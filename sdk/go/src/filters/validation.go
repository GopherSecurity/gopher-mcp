package filters

import (
	"encoding/json"
	"fmt"
	"sync"
	"time"
)

// ValidationFilter validates JSON-RPC messages.
type ValidationFilter struct {
	id            string
	name          string
	maxSize       int
	validateJSON  bool
	mu            sync.RWMutex
	stats         FilterStats
	enabled       bool
}

// NewValidationFilter creates a new validation filter.
func NewValidationFilter(maxSize int) *ValidationFilter {
	return &ValidationFilter{
		id:           fmt.Sprintf("validation-%d", time.Now().UnixNano()),
		name:         "ValidationFilter",
		maxSize:      maxSize,
		validateJSON: true,
		enabled:      true,
	}
}

// GetID returns the filter ID.
func (f *ValidationFilter) GetID() string {
	return f.id
}

// GetName returns the filter name.
func (f *ValidationFilter) GetName() string {
	return f.name
}

// GetType returns the filter type.
func (f *ValidationFilter) GetType() string {
	return "validation"
}

// GetVersion returns the filter version.
func (f *ValidationFilter) GetVersion() string {
	return "1.0.0"
}

// GetDescription returns the filter description.
func (f *ValidationFilter) GetDescription() string {
	return "JSON-RPC message validation filter"
}

// Process validates the data and passes it through if valid.
func (f *ValidationFilter) Process(data []byte) ([]byte, error) {
	if !f.enabled {
		return data, nil
	}

	f.mu.Lock()
	f.stats.ProcessedCount++
	f.stats.BytesIn += int64(len(data))
	f.stats.LastProcessed = time.Now()
	f.mu.Unlock()

	// Check size limit
	if f.maxSize > 0 && len(data) > f.maxSize {
		f.mu.Lock()
		f.stats.Errors++
		f.mu.Unlock()
		return nil, fmt.Errorf("message size %d exceeds limit %d", len(data), f.maxSize)
	}

	// Validate JSON structure if enabled
	if f.validateJSON && len(data) > 0 {
		var msg map[string]interface{}
		if err := json.Unmarshal(data, &msg); err != nil {
			f.mu.Lock()
			f.stats.Errors++
			f.mu.Unlock()
			return nil, fmt.Errorf("invalid JSON: %w", err)
		}

		// Check for required JSON-RPC fields
		if _, ok := msg["jsonrpc"]; !ok {
			f.mu.Lock()
			f.stats.Errors++
			f.mu.Unlock()
			return nil, fmt.Errorf("missing jsonrpc field")
		}
	}

	f.mu.Lock()
	f.stats.BytesOut += int64(len(data))
	f.mu.Unlock()

	return data, nil
}

// SetEnabled enables or disables the filter.
func (f *ValidationFilter) SetEnabled(enabled bool) {
	f.mu.Lock()
	defer f.mu.Unlock()
	f.enabled = enabled
}

// IsEnabled returns whether the filter is enabled.
func (f *ValidationFilter) IsEnabled() bool {
	f.mu.RLock()
	defer f.mu.RUnlock()
	return f.enabled
}

// GetStats returns filter statistics.
func (f *ValidationFilter) GetStats() FilterStats {
	f.mu.RLock()
	defer f.mu.RUnlock()
	return f.stats
}

// Reset resets filter statistics.
func (f *ValidationFilter) Reset() {
	f.mu.Lock()
	defer f.mu.Unlock()
	f.stats = FilterStats{}
}

// SetID sets the filter ID.
func (f *ValidationFilter) SetID(id string) {
	f.id = id
}

// Priority returns the filter priority.
func (f *ValidationFilter) Priority() int {
	return 1 // Highest priority - validate first
}

// EstimateLatency estimates processing latency.
func (f *ValidationFilter) EstimateLatency() time.Duration {
	return 100 * time.Microsecond
}

// HasKnownVulnerabilities returns whether the filter has known vulnerabilities.
func (f *ValidationFilter) HasKnownVulnerabilities() bool {
	return false
}

// IsStateless returns whether the filter is stateless.
func (f *ValidationFilter) IsStateless() bool {
	return true
}

// UsesDeprecatedFeatures returns whether the filter uses deprecated features.
func (f *ValidationFilter) UsesDeprecatedFeatures() bool {
	return false
}