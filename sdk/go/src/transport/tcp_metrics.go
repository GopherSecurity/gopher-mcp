// Package transport provides communication transports for the MCP Filter SDK.
package transport

import (
	"sync"
	"sync/atomic"
	"time"
)

// TcpMetrics tracks TCP transport performance metrics.
type TcpMetrics struct {
	// Connection metrics
	connectionCount      atomic.Int64
	activeConnections    atomic.Int64
	reconnectionAttempts atomic.Int64
	failedConnections    atomic.Int64

	// Latency tracking
	latencies   []time.Duration
	latencyMu   sync.RWMutex
	percentiles LatencyPercentiles

	// Throughput
	bytesSent        atomic.Int64
	bytesReceived    atomic.Int64
	messagesSent     atomic.Int64
	messagesReceived atomic.Int64

	// Per-connection stats
	connStats map[string]*ConnectionStats
	connMu    sync.RWMutex

	// Timing
	startTime time.Time
	lastReset time.Time
}

// ConnectionStats tracks per-connection statistics.
type ConnectionStats struct {
	Address          string
	Connected        time.Time
	BytesSent        int64
	BytesReceived    int64
	MessagesSent     int64
	MessagesReceived int64
	Errors           int64
	LastActivity     time.Time
}

// LatencyPercentiles contains latency percentile values.
type LatencyPercentiles struct {
	P50  time.Duration
	P90  time.Duration
	P95  time.Duration
	P99  time.Duration
	P999 time.Duration
}

// NewTcpMetrics creates new TCP metrics tracker.
func NewTcpMetrics() *TcpMetrics {
	return &TcpMetrics{
		latencies: make([]time.Duration, 0, 10000),
		connStats: make(map[string]*ConnectionStats),
		startTime: time.Now(),
		lastReset: time.Now(),
	}
}

// RecordConnection records a new connection.
func (tm *TcpMetrics) RecordConnection(address string) {
	tm.connectionCount.Add(1)
	tm.activeConnections.Add(1)

	tm.connMu.Lock()
	tm.connStats[address] = &ConnectionStats{
		Address:   address,
		Connected: time.Now(),
	}
	tm.connMu.Unlock()
}

// RecordDisconnection records a disconnection.
func (tm *TcpMetrics) RecordDisconnection(address string) {
	tm.activeConnections.Add(-1)

	tm.connMu.Lock()
	delete(tm.connStats, address)
	tm.connMu.Unlock()
}

// RecordReconnectionAttempt records a reconnection attempt.
func (tm *TcpMetrics) RecordReconnectionAttempt(success bool) {
	tm.reconnectionAttempts.Add(1)
	if !success {
		tm.failedConnections.Add(1)
	}
}

// RecordLatency records a request-response latency.
func (tm *TcpMetrics) RecordLatency(latency time.Duration) {
	tm.latencyMu.Lock()
	tm.latencies = append(tm.latencies, latency)

	// Keep only last 10000 samples
	if len(tm.latencies) > 10000 {
		tm.latencies = tm.latencies[len(tm.latencies)-10000:]
	}
	tm.latencyMu.Unlock()

	// Update percentiles periodically
	if len(tm.latencies)%100 == 0 {
		tm.updatePercentiles()
	}
}

// updatePercentiles calculates latency percentiles.
func (tm *TcpMetrics) updatePercentiles() {
	tm.latencyMu.RLock()
	if len(tm.latencies) == 0 {
		tm.latencyMu.RUnlock()
		return
	}

	// Copy and sort latencies
	sorted := make([]time.Duration, len(tm.latencies))
	copy(sorted, tm.latencies)
	tm.latencyMu.RUnlock()

	// Simple bubble sort for percentile calculation
	for i := 0; i < len(sorted); i++ {
		for j := i + 1; j < len(sorted); j++ {
			if sorted[j] < sorted[i] {
				sorted[i], sorted[j] = sorted[j], sorted[i]
			}
		}
	}

	// Calculate percentiles
	tm.percentiles = LatencyPercentiles{
		P50:  sorted[len(sorted)*50/100],
		P90:  sorted[len(sorted)*90/100],
		P95:  sorted[len(sorted)*95/100],
		P99:  sorted[len(sorted)*99/100],
		P999: sorted[len(sorted)*999/1000],
	}
}

// RecordBytes records bytes sent or received.
func (tm *TcpMetrics) RecordBytes(sent, received int64, address string) {
	tm.bytesSent.Add(sent)
	tm.bytesReceived.Add(received)

	tm.connMu.Lock()
	if stats, exists := tm.connStats[address]; exists {
		stats.BytesSent += sent
		stats.BytesReceived += received
		stats.LastActivity = time.Now()
	}
	tm.connMu.Unlock()
}

// RecordMessage records message sent or received.
func (tm *TcpMetrics) RecordMessage(sent bool, address string) {
	if sent {
		tm.messagesSent.Add(1)
	} else {
		tm.messagesReceived.Add(1)
	}

	tm.connMu.Lock()
	if stats, exists := tm.connStats[address]; exists {
		if sent {
			stats.MessagesSent++
		} else {
			stats.MessagesReceived++
		}
		stats.LastActivity = time.Now()
	}
	tm.connMu.Unlock()
}

// RecordError records a connection error.
func (tm *TcpMetrics) RecordError(address string) {
	tm.connMu.Lock()
	if stats, exists := tm.connStats[address]; exists {
		stats.Errors++
	}
	tm.connMu.Unlock()
}

// GetThroughput calculates current throughput.
func (tm *TcpMetrics) GetThroughput() (sendRate, receiveRate float64) {
	elapsed := time.Since(tm.startTime).Seconds()
	if elapsed > 0 {
		sendRate = float64(tm.bytesSent.Load()) / elapsed
		receiveRate = float64(tm.bytesReceived.Load()) / elapsed
	}
	return
}

// GetConnectionStats returns per-connection statistics.
func (tm *TcpMetrics) GetConnectionStats() map[string]ConnectionStats {
	tm.connMu.RLock()
	defer tm.connMu.RUnlock()

	result := make(map[string]ConnectionStats)
	for addr, stats := range tm.connStats {
		result[addr] = *stats
	}
	return result
}

// GetAggregateStats returns aggregate statistics.
func (tm *TcpMetrics) GetAggregateStats() TcpStats {
	sendRate, receiveRate := tm.GetThroughput()

	return TcpStats{
		ConnectionCount:      tm.connectionCount.Load(),
		ActiveConnections:    tm.activeConnections.Load(),
		ReconnectionAttempts: tm.reconnectionAttempts.Load(),
		FailedConnections:    tm.failedConnections.Load(),
		BytesSent:            tm.bytesSent.Load(),
		BytesReceived:        tm.bytesReceived.Load(),
		MessagesSent:         tm.messagesSent.Load(),
		MessagesReceived:     tm.messagesReceived.Load(),
		LatencyPercentiles:   tm.percentiles,
		SendThroughput:       sendRate,
		ReceiveThroughput:    receiveRate,
		Uptime:               time.Since(tm.startTime),
	}
}

// TcpStats contains TCP metrics snapshot.
type TcpStats struct {
	ConnectionCount      int64
	ActiveConnections    int64
	ReconnectionAttempts int64
	FailedConnections    int64
	BytesSent            int64
	BytesReceived        int64
	MessagesSent         int64
	MessagesReceived     int64
	LatencyPercentiles   LatencyPercentiles
	SendThroughput       float64
	ReceiveThroughput    float64
	Uptime               time.Duration
}

// Reset clears all metrics.
func (tm *TcpMetrics) Reset() {
	tm.connectionCount.Store(0)
	tm.activeConnections.Store(0)
	tm.reconnectionAttempts.Store(0)
	tm.failedConnections.Store(0)
	tm.bytesSent.Store(0)
	tm.bytesReceived.Store(0)
	tm.messagesSent.Store(0)
	tm.messagesReceived.Store(0)

	tm.latencyMu.Lock()
	tm.latencies = tm.latencies[:0]
	tm.latencyMu.Unlock()

	tm.connMu.Lock()
	tm.connStats = make(map[string]*ConnectionStats)
	tm.connMu.Unlock()

	tm.lastReset = time.Now()
}
