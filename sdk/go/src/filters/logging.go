package filters

import (
	"fmt"
	"log"
	"sync"
	"time"
)

// LoggingFilter logs data passing through the filter chain.
type LoggingFilter struct {
	id          string
	name        string
	logPrefix   string
	logPayload  bool
	maxLogSize  int
	mu          sync.RWMutex
	stats       FilterStats
	enabled     bool
}

// NewLoggingFilter creates a new logging filter.
func NewLoggingFilter(logPrefix string, logPayload bool) *LoggingFilter {
	return &LoggingFilter{
		id:         fmt.Sprintf("logging-%d", time.Now().UnixNano()),
		name:       "LoggingFilter",
		logPrefix:  logPrefix,
		logPayload: logPayload,
		maxLogSize: 1024, // Max 1KB of payload to log
		enabled:    true,
	}
}

// GetID returns the filter ID.
func (f *LoggingFilter) GetID() string {
	return f.id
}

// GetName returns the filter name.
func (f *LoggingFilter) GetName() string {
	return f.name
}

// GetType returns the filter type.
func (f *LoggingFilter) GetType() string {
	return "logging"
}

// GetVersion returns the filter version.
func (f *LoggingFilter) GetVersion() string {
	return "1.0.0"
}

// GetDescription returns the filter description.
func (f *LoggingFilter) GetDescription() string {
	return "Logging filter for debugging and monitoring"
}

// Process logs the data and passes it through unchanged.
func (f *LoggingFilter) Process(data []byte) ([]byte, error) {
	if !f.enabled {
		return data, nil
	}

	f.mu.Lock()
	f.stats.ProcessedCount++
	f.stats.BytesIn += int64(len(data))
	f.stats.BytesOut += int64(len(data))
	f.stats.LastProcessed = time.Now()
	f.mu.Unlock()

	// Log the data
	timestamp := time.Now().Format("2006-01-02 15:04:05.000")
	log.Printf("[%s%s] Processing %d bytes", f.logPrefix, timestamp, len(data))

	if f.logPayload && len(data) > 0 {
		payloadSize := len(data)
		if payloadSize > f.maxLogSize {
			payloadSize = f.maxLogSize
		}
		
		// Log first part of payload
		log.Printf("[%sPayload] %s", f.logPrefix, string(data[:payloadSize]))
		
		if len(data) > f.maxLogSize {
			log.Printf("[%sPayload] ... (%d more bytes)", f.logPrefix, len(data)-f.maxLogSize)
		}
	}

	return data, nil
}

// SetEnabled enables or disables the filter.
func (f *LoggingFilter) SetEnabled(enabled bool) {
	f.mu.Lock()
	defer f.mu.Unlock()
	f.enabled = enabled
}

// IsEnabled returns whether the filter is enabled.
func (f *LoggingFilter) IsEnabled() bool {
	f.mu.RLock()
	defer f.mu.RUnlock()
	return f.enabled
}

// GetStats returns filter statistics.
func (f *LoggingFilter) GetStats() FilterStats {
	f.mu.RLock()
	defer f.mu.RUnlock()
	return f.stats
}

// Reset resets filter statistics.
func (f *LoggingFilter) Reset() {
	f.mu.Lock()
	defer f.mu.Unlock()
	f.stats = FilterStats{}
}

// SetID sets the filter ID.
func (f *LoggingFilter) SetID(id string) {
	f.id = id
}

// Priority returns the filter priority.
func (f *LoggingFilter) Priority() int {
	return 10 // High priority - log early in the chain
}

// EstimateLatency estimates processing latency.
func (f *LoggingFilter) EstimateLatency() time.Duration {
	return 100 * time.Microsecond
}

// HasKnownVulnerabilities returns whether the filter has known vulnerabilities.
func (f *LoggingFilter) HasKnownVulnerabilities() bool {
	return false
}

// IsStateless returns whether the filter is stateless.
func (f *LoggingFilter) IsStateless() bool {
	return true
}

// UsesDeprecatedFeatures returns whether the filter uses deprecated features.
func (f *LoggingFilter) UsesDeprecatedFeatures() bool {
	return false
}

// SetLogPayload sets whether to log payload data.
func (f *LoggingFilter) SetLogPayload(enabled bool) {
	f.mu.Lock()
	defer f.mu.Unlock()
	f.logPayload = enabled
}

// SetMaxLogSize sets the maximum payload size to log.
func (f *LoggingFilter) SetMaxLogSize(size int) {
	f.mu.Lock()
	defer f.mu.Unlock()
	f.maxLogSize = size
}