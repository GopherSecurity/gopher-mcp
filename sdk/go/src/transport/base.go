// Package transport provides communication transports for the MCP Filter SDK.
package transport

import (
	"sync"
	"sync/atomic"
	"time"
)

// TransportBase provides common functionality for transport implementations.
// It should be embedded in concrete transport types to provide standard
// connection state management and statistics tracking.
//
// Example usage:
//
//	type MyTransport struct {
//	    TransportBase
//	    // Additional fields specific to this transport
//	}
//	
//	func (t *MyTransport) Connect(ctx context.Context) error {
//	    if !t.SetConnected(true) {
//	        return ErrAlreadyConnected
//	    }
//	    // Perform connection logic
//	    t.UpdateConnectTime()
//	    return nil
//	}
type TransportBase struct {
	// Connection state (atomic for thread-safety)
	connected atomic.Bool
	
	// Statistics tracking
	stats TransportStatistics
	
	// Configuration
	config TransportConfig
	
	// Synchronization
	mu sync.RWMutex
}

// NewTransportBase creates a new TransportBase with the given configuration.
func NewTransportBase(config TransportConfig) TransportBase {
	return TransportBase{
		config: config,
		stats: TransportStatistics{
			CustomMetrics: make(map[string]interface{}),
		},
	}
}

// IsConnected returns the current connection state.
// This method is thread-safe.
func (tb *TransportBase) IsConnected() bool {
	return tb.connected.Load()
}

// SetConnected atomically sets the connection state.
// Returns false if the state was already set to the requested value.
func (tb *TransportBase) SetConnected(connected bool) bool {
	return tb.connected.CompareAndSwap(!connected, connected)
}

// GetStats returns a copy of the current statistics.
// This method is thread-safe.
func (tb *TransportBase) GetStats() TransportStatistics {
	tb.mu.RLock()
	defer tb.mu.RUnlock()
	
	// Create a copy of statistics
	statsCopy := tb.stats
	statsCopy.IsConnected = tb.IsConnected()
	
	// Deep copy custom metrics
	if tb.stats.CustomMetrics != nil {
		statsCopy.CustomMetrics = make(map[string]interface{})
		for k, v := range tb.stats.CustomMetrics {
			statsCopy.CustomMetrics[k] = v
		}
	}
	
	return statsCopy
}

// GetConfig returns the transport configuration.
func (tb *TransportBase) GetConfig() TransportConfig {
	tb.mu.RLock()
	defer tb.mu.RUnlock()
	return tb.config
}

// UpdateConnectTime updates the connection timestamp in statistics.
func (tb *TransportBase) UpdateConnectTime() {
	tb.mu.Lock()
	defer tb.mu.Unlock()
	
	tb.stats.ConnectedAt = time.Now()
	tb.stats.ConnectionCount++
	tb.stats.DisconnectedAt = time.Time{} // Reset disconnect time
}

// UpdateDisconnectTime updates the disconnection timestamp in statistics.
func (tb *TransportBase) UpdateDisconnectTime() {
	tb.mu.Lock()
	defer tb.mu.Unlock()
	
	tb.stats.DisconnectedAt = time.Now()
}

// RecordBytesSent updates the bytes sent statistics.
// This method is thread-safe.
func (tb *TransportBase) RecordBytesSent(bytes int) {
	tb.mu.Lock()
	defer tb.mu.Unlock()
	
	tb.stats.BytesSent += int64(bytes)
	tb.stats.MessagesSent++
	tb.stats.LastSendTime = time.Now()
}

// RecordBytesReceived updates the bytes received statistics.
// This method is thread-safe.
func (tb *TransportBase) RecordBytesReceived(bytes int) {
	tb.mu.Lock()
	defer tb.mu.Unlock()
	
	tb.stats.BytesReceived += int64(bytes)
	tb.stats.MessagesReceived++
	tb.stats.LastReceiveTime = time.Now()
}

// RecordSendError increments the send error counter.
// This method is thread-safe.
func (tb *TransportBase) RecordSendError() {
	tb.mu.Lock()
	defer tb.mu.Unlock()
	
	tb.stats.SendErrors++
}

// RecordReceiveError increments the receive error counter.
// This method is thread-safe.
func (tb *TransportBase) RecordReceiveError() {
	tb.mu.Lock()
	defer tb.mu.Unlock()
	
	tb.stats.ReceiveErrors++
}

// RecordConnectionError increments the connection error counter.
// This method is thread-safe.
func (tb *TransportBase) RecordConnectionError() {
	tb.mu.Lock()
	defer tb.mu.Unlock()
	
	tb.stats.ConnectionErrors++
}

// UpdateLatency updates the average latency metric.
// This method uses an exponential moving average for efficiency.
func (tb *TransportBase) UpdateLatency(latency time.Duration) {
	tb.mu.Lock()
	defer tb.mu.Unlock()
	
	if tb.stats.AverageLatency == 0 {
		tb.stats.AverageLatency = latency
	} else {
		// Exponential moving average with alpha = 0.1
		alpha := 0.1
		tb.stats.AverageLatency = time.Duration(
			float64(tb.stats.AverageLatency)*(1-alpha) + float64(latency)*alpha,
		)
	}
}

// SetCustomMetric sets a custom metric value.
// This method is thread-safe.
func (tb *TransportBase) SetCustomMetric(key string, value interface{}) {
	tb.mu.Lock()
	defer tb.mu.Unlock()
	
	if tb.stats.CustomMetrics == nil {
		tb.stats.CustomMetrics = make(map[string]interface{})
	}
	tb.stats.CustomMetrics[key] = value
}

// GetCustomMetric retrieves a custom metric value.
// Returns nil if the metric doesn't exist.
func (tb *TransportBase) GetCustomMetric(key string) interface{} {
	tb.mu.RLock()
	defer tb.mu.RUnlock()
	
	if tb.stats.CustomMetrics == nil {
		return nil
	}
	return tb.stats.CustomMetrics[key]
}

// ResetStats resets all statistics to their initial values.
// Connection state is not affected.
func (tb *TransportBase) ResetStats() {
	tb.mu.Lock()
	defer tb.mu.Unlock()
	
	tb.stats = TransportStatistics{
		CustomMetrics: make(map[string]interface{}),
	}
}

// GetConnectionDuration returns how long the transport has been connected.
// Returns 0 if not currently connected.
func (tb *TransportBase) GetConnectionDuration() time.Duration {
	if !tb.IsConnected() {
		return 0
	}
	
	tb.mu.RLock()
	defer tb.mu.RUnlock()
	
	if tb.stats.ConnectedAt.IsZero() {
		return 0
	}
	
	return time.Since(tb.stats.ConnectedAt)
}

// GetThroughput calculates current throughput in bytes per second.
// Returns separate values for send and receive throughput.
func (tb *TransportBase) GetThroughput() (sendBps, receiveBps float64) {
	tb.mu.RLock()
	defer tb.mu.RUnlock()
	
	duration := tb.GetConnectionDuration().Seconds()
	if duration <= 0 {
		return 0, 0
	}
	
	sendBps = float64(tb.stats.BytesSent) / duration
	receiveBps = float64(tb.stats.BytesReceived) / duration
	
	return sendBps, receiveBps
}