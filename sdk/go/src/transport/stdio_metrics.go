// Package transport provides communication transports for the MCP Filter SDK.
package transport

import (
	"sync/atomic"
	"time"
)

// StdioMetrics tracks stdio transport performance metrics.
type StdioMetrics struct {
	// Line counters
	linesRead    atomic.Int64
	linesWritten atomic.Int64

	// Size tracking
	bytesRead     atomic.Int64
	bytesWritten  atomic.Int64
	totalMessages atomic.Int64

	// Throughput
	readRate  atomic.Value // float64
	writeRate atomic.Value // float64

	// Timing
	startTime     time.Time
	lastReadTime  atomic.Value // time.Time
	lastWriteTime atomic.Value // time.Time

	// Message size statistics
	minMessageSize atomic.Int64
	maxMessageSize atomic.Int64
	avgMessageSize atomic.Value // float64
}

// NewStdioMetrics creates new stdio metrics tracker.
func NewStdioMetrics() *StdioMetrics {
	sm := &StdioMetrics{
		startTime: time.Now(),
	}
	sm.minMessageSize.Store(int64(^uint64(0) >> 1)) // Max int64
	return sm
}

// RecordLineRead records a line read operation.
func (sm *StdioMetrics) RecordLineRead(bytes int) {
	sm.linesRead.Add(1)
	sm.bytesRead.Add(int64(bytes))
	sm.lastReadTime.Store(time.Now())
	sm.updateMessageStats(bytes)
	sm.updateReadRate()
}

// RecordLineWritten records a line write operation.
func (sm *StdioMetrics) RecordLineWritten(bytes int) {
	sm.linesWritten.Add(1)
	sm.bytesWritten.Add(int64(bytes))
	sm.lastWriteTime.Store(time.Now())
	sm.updateMessageStats(bytes)
	sm.updateWriteRate()
}

// updateMessageStats updates message size statistics.
func (sm *StdioMetrics) updateMessageStats(size int) {
	sm.totalMessages.Add(1)

	// Update min/max
	sizeInt64 := int64(size)
	for {
		min := sm.minMessageSize.Load()
		if sizeInt64 >= min || sm.minMessageSize.CompareAndSwap(min, sizeInt64) {
			break
		}
	}

	for {
		max := sm.maxMessageSize.Load()
		if sizeInt64 <= max || sm.maxMessageSize.CompareAndSwap(max, sizeInt64) {
			break
		}
	}

	// Update average
	total := sm.bytesRead.Load() + sm.bytesWritten.Load()
	messages := sm.totalMessages.Load()
	if messages > 0 {
		sm.avgMessageSize.Store(float64(total) / float64(messages))
	}
}

// updateReadRate calculates current read throughput.
func (sm *StdioMetrics) updateReadRate() {
	elapsed := time.Since(sm.startTime).Seconds()
	if elapsed > 0 {
		rate := float64(sm.bytesRead.Load()) / elapsed
		sm.readRate.Store(rate)
	}
}

// updateWriteRate calculates current write throughput.
func (sm *StdioMetrics) updateWriteRate() {
	elapsed := time.Since(sm.startTime).Seconds()
	if elapsed > 0 {
		rate := float64(sm.bytesWritten.Load()) / elapsed
		sm.writeRate.Store(rate)
	}
}

// GetStats returns current metrics snapshot.
func (sm *StdioMetrics) GetStats() StdioStats {
	avgSize := float64(0)
	if v := sm.avgMessageSize.Load(); v != nil {
		avgSize = v.(float64)
	}

	readRate := float64(0)
	if v := sm.readRate.Load(); v != nil {
		readRate = v.(float64)
	}

	writeRate := float64(0)
	if v := sm.writeRate.Load(); v != nil {
		writeRate = v.(float64)
	}

	return StdioStats{
		LinesRead:       sm.linesRead.Load(),
		LinesWritten:    sm.linesWritten.Load(),
		BytesRead:       sm.bytesRead.Load(),
		BytesWritten:    sm.bytesWritten.Load(),
		TotalMessages:   sm.totalMessages.Load(),
		MinMessageSize:  sm.minMessageSize.Load(),
		MaxMessageSize:  sm.maxMessageSize.Load(),
		AvgMessageSize:  avgSize,
		ReadThroughput:  readRate,
		WriteThroughput: writeRate,
		Uptime:          time.Since(sm.startTime),
	}
}

// StdioStats contains stdio metrics snapshot.
type StdioStats struct {
	LinesRead       int64
	LinesWritten    int64
	BytesRead       int64
	BytesWritten    int64
	TotalMessages   int64
	MinMessageSize  int64
	MaxMessageSize  int64
	AvgMessageSize  float64
	ReadThroughput  float64 // bytes/sec
	WriteThroughput float64 // bytes/sec
	Uptime          time.Duration
}
