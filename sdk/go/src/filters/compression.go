// Package filters provides built-in filters for the MCP SDK.
package filters

import (
	"bytes"
	"compress/gzip"
	"fmt"
	"io"
	"sync"
	"time"
)

// CompressionFilter applies gzip compression to data.
type CompressionFilter struct {
	id          string
	name        string
	level       int
	mu          sync.RWMutex
	stats       FilterStats
	enabled     bool
}

// FilterStats tracks filter performance metrics.
type FilterStats struct {
	ProcessedCount int64
	BytesIn        int64
	BytesOut       int64
	Errors         int64
	LastProcessed  time.Time
}

// NewCompressionFilter creates a new compression filter.
func NewCompressionFilter(level int) *CompressionFilter {
	if level < gzip.DefaultCompression || level > gzip.BestCompression {
		level = gzip.DefaultCompression
	}
	
	return &CompressionFilter{
		id:      fmt.Sprintf("compression-%d", time.Now().UnixNano()),
		name:    "CompressionFilter",
		level:   level,
		enabled: true,
	}
}

// GetID returns the filter ID.
func (f *CompressionFilter) GetID() string {
	return f.id
}

// GetName returns the filter name.
func (f *CompressionFilter) GetName() string {
	return f.name
}

// GetType returns the filter type.
func (f *CompressionFilter) GetType() string {
	return "compression"
}

// GetVersion returns the filter version.
func (f *CompressionFilter) GetVersion() string {
	return "1.0.0"
}

// GetDescription returns the filter description.
func (f *CompressionFilter) GetDescription() string {
	return fmt.Sprintf("GZIP compression filter (level %d)", f.level)
}

// Process compresses the input data.
func (f *CompressionFilter) Process(data []byte) ([]byte, error) {
	if !f.enabled || len(data) == 0 {
		return data, nil
	}

	f.mu.Lock()
	f.stats.ProcessedCount++
	f.stats.BytesIn += int64(len(data))
	f.stats.LastProcessed = time.Now()
	f.mu.Unlock()

	var buf bytes.Buffer
	writer, err := gzip.NewWriterLevel(&buf, f.level)
	if err != nil {
		f.mu.Lock()
		f.stats.Errors++
		f.mu.Unlock()
		return nil, fmt.Errorf("failed to create gzip writer: %w", err)
	}

	if _, err := writer.Write(data); err != nil {
		f.mu.Lock()
		f.stats.Errors++
		f.mu.Unlock()
		writer.Close()
		return nil, fmt.Errorf("failed to compress data: %w", err)
	}

	if err := writer.Close(); err != nil {
		f.mu.Lock()
		f.stats.Errors++
		f.mu.Unlock()
		return nil, fmt.Errorf("failed to close gzip writer: %w", err)
	}

	compressed := buf.Bytes()
	
	f.mu.Lock()
	f.stats.BytesOut += int64(len(compressed))
	f.mu.Unlock()

	return compressed, nil
}

// Decompress decompresses gzipped data.
func (f *CompressionFilter) Decompress(data []byte) ([]byte, error) {
	if !f.enabled || len(data) == 0 {
		return data, nil
	}

	reader, err := gzip.NewReader(bytes.NewReader(data))
	if err != nil {
		return nil, fmt.Errorf("failed to create gzip reader: %w", err)
	}
	defer reader.Close()

	decompressed, err := io.ReadAll(reader)
	if err != nil {
		return nil, fmt.Errorf("failed to decompress data: %w", err)
	}

	return decompressed, nil
}

// SetEnabled enables or disables the filter.
func (f *CompressionFilter) SetEnabled(enabled bool) {
	f.mu.Lock()
	defer f.mu.Unlock()
	f.enabled = enabled
}

// IsEnabled returns whether the filter is enabled.
func (f *CompressionFilter) IsEnabled() bool {
	f.mu.RLock()
	defer f.mu.RUnlock()
	return f.enabled
}

// GetStats returns filter statistics.
func (f *CompressionFilter) GetStats() FilterStats {
	f.mu.RLock()
	defer f.mu.RUnlock()
	return f.stats
}

// Reset resets filter statistics.
func (f *CompressionFilter) Reset() {
	f.mu.Lock()
	defer f.mu.Unlock()
	f.stats = FilterStats{}
}

// SetID sets the filter ID.
func (f *CompressionFilter) SetID(id string) {
	f.id = id
}

// Priority returns the filter priority.
func (f *CompressionFilter) Priority() int {
	return 100
}

// EstimateLatency estimates processing latency.
func (f *CompressionFilter) EstimateLatency() time.Duration {
	return 1 * time.Millisecond
}

// HasKnownVulnerabilities returns whether the filter has known vulnerabilities.
func (f *CompressionFilter) HasKnownVulnerabilities() bool {
	return false
}

// IsStateless returns whether the filter is stateless.
func (f *CompressionFilter) IsStateless() bool {
	return true
}

// UsesDeprecatedFeatures returns whether the filter uses deprecated features.
func (f *CompressionFilter) UsesDeprecatedFeatures() bool {
	return false
}