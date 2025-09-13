package transport_test

import (
	"sync"
	"testing"
	"time"

	"github.com/GopherSecurity/gopher-mcp/src/transport"
)

// Test 1: NewTransportBase creation
func TestNewTransportBase(t *testing.T) {
	config := transport.DefaultTransportConfig()
	tb := transport.NewTransportBase(config)

	// Check initial state
	if tb.IsConnected() {
		t.Error("New transport should not be connected")
	}

	// Check config is stored
	storedConfig := tb.GetConfig()
	if storedConfig.ConnectTimeout != config.ConnectTimeout {
		t.Error("Config not stored correctly")
	}

	// Check stats are initialized
	stats := tb.GetStats()
	if stats.BytesSent != 0 || stats.BytesReceived != 0 {
		t.Error("Initial stats should be zero")
	}
	if stats.CustomMetrics == nil {
		t.Error("CustomMetrics should be initialized")
	}
}

// Test 2: Connection state management
func TestTransportBase_ConnectionState(t *testing.T) {
	tb := transport.NewTransportBase(transport.DefaultTransportConfig())

	// Initial state should be disconnected
	if tb.IsConnected() {
		t.Error("Should start disconnected")
	}

	// Set connected
	if !tb.SetConnected(true) {
		t.Error("SetConnected(true) should succeed when disconnected")
	}

	if !tb.IsConnected() {
		t.Error("Should be connected after SetConnected(true)")
	}

	// Try to set connected again (should fail)
	if tb.SetConnected(true) {
		t.Error("SetConnected(true) should fail when already connected")
	}

	// Set disconnected
	if !tb.SetConnected(false) {
		t.Error("SetConnected(false) should succeed when connected")
	}

	if tb.IsConnected() {
		t.Error("Should be disconnected after SetConnected(false)")
	}
}

// Test 3: UpdateConnectTime and UpdateDisconnectTime
func TestTransportBase_ConnectionTimes(t *testing.T) {
	tb := transport.NewTransportBase(transport.DefaultTransportConfig())

	// Update connect time
	tb.UpdateConnectTime()

	stats := tb.GetStats()
	if stats.ConnectedAt.IsZero() {
		t.Error("ConnectedAt should be set")
	}
	if stats.ConnectionCount != 1 {
		t.Errorf("ConnectionCount = %d, want 1", stats.ConnectionCount)
	}
	if !stats.DisconnectedAt.IsZero() {
		t.Error("DisconnectedAt should be zero after connect")
	}

	// Update disconnect time
	tb.UpdateDisconnectTime()

	stats = tb.GetStats()
	if stats.DisconnectedAt.IsZero() {
		t.Error("DisconnectedAt should be set")
	}

	// Connect again to test counter
	tb.UpdateConnectTime()
	stats = tb.GetStats()
	if stats.ConnectionCount != 2 {
		t.Errorf("ConnectionCount = %d, want 2", stats.ConnectionCount)
	}
}

// Test 4: RecordBytesSent and RecordBytesReceived
func TestTransportBase_ByteCounters(t *testing.T) {
	tb := transport.NewTransportBase(transport.DefaultTransportConfig())

	// Record sent bytes
	tb.RecordBytesSent(100)
	tb.RecordBytesSent(200)

	stats := tb.GetStats()
	if stats.BytesSent != 300 {
		t.Errorf("BytesSent = %d, want 300", stats.BytesSent)
	}
	if stats.MessagesSent != 2 {
		t.Errorf("MessagesSent = %d, want 2", stats.MessagesSent)
	}
	if stats.LastSendTime.IsZero() {
		t.Error("LastSendTime should be set")
	}

	// Record received bytes
	tb.RecordBytesReceived(150)
	tb.RecordBytesReceived(250)
	tb.RecordBytesReceived(100)

	stats = tb.GetStats()
	if stats.BytesReceived != 500 {
		t.Errorf("BytesReceived = %d, want 500", stats.BytesReceived)
	}
	if stats.MessagesReceived != 3 {
		t.Errorf("MessagesReceived = %d, want 3", stats.MessagesReceived)
	}
	if stats.LastReceiveTime.IsZero() {
		t.Error("LastReceiveTime should be set")
	}
}

// Test 5: Error counters
func TestTransportBase_ErrorCounters(t *testing.T) {
	tb := transport.NewTransportBase(transport.DefaultTransportConfig())

	// Record various errors
	tb.RecordSendError()
	tb.RecordSendError()

	tb.RecordReceiveError()
	tb.RecordReceiveError()
	tb.RecordReceiveError()

	tb.RecordConnectionError()

	stats := tb.GetStats()
	if stats.SendErrors != 2 {
		t.Errorf("SendErrors = %d, want 2", stats.SendErrors)
	}
	if stats.ReceiveErrors != 3 {
		t.Errorf("ReceiveErrors = %d, want 3", stats.ReceiveErrors)
	}
	if stats.ConnectionErrors != 1 {
		t.Errorf("ConnectionErrors = %d, want 1", stats.ConnectionErrors)
	}
}

// Test 6: UpdateLatency
func TestTransportBase_UpdateLatency(t *testing.T) {
	tb := transport.NewTransportBase(transport.DefaultTransportConfig())

	// First latency update
	tb.UpdateLatency(100 * time.Millisecond)

	stats := tb.GetStats()
	if stats.AverageLatency != 100*time.Millisecond {
		t.Errorf("Initial AverageLatency = %v, want 100ms", stats.AverageLatency)
	}

	// Second latency update (should use exponential moving average)
	tb.UpdateLatency(200 * time.Millisecond)

	stats = tb.GetStats()
	// With alpha=0.1: 100ms * 0.9 + 200ms * 0.1 = 90ms + 20ms = 110ms
	expectedLatency := 110 * time.Millisecond
	tolerance := 5 * time.Millisecond

	if stats.AverageLatency < expectedLatency-tolerance || stats.AverageLatency > expectedLatency+tolerance {
		t.Errorf("AverageLatency = %v, want ~%v", stats.AverageLatency, expectedLatency)
	}
}

// Test 7: Custom metrics
func TestTransportBase_CustomMetrics(t *testing.T) {
	tb := transport.NewTransportBase(transport.DefaultTransportConfig())

	// Set custom metrics
	tb.SetCustomMetric("protocol", "TCP")
	tb.SetCustomMetric("version", 2)
	tb.SetCustomMetric("compression", true)

	// Get custom metrics
	if val := tb.GetCustomMetric("protocol"); val != "TCP" {
		t.Errorf("protocol = %v, want TCP", val)
	}
	if val := tb.GetCustomMetric("version"); val != 2 {
		t.Errorf("version = %v, want 2", val)
	}
	if val := tb.GetCustomMetric("compression"); val != true {
		t.Errorf("compression = %v, want true", val)
	}

	// Non-existent metric
	if val := tb.GetCustomMetric("missing"); val != nil {
		t.Errorf("missing metric = %v, want nil", val)
	}

	// Check in stats
	stats := tb.GetStats()
	if stats.CustomMetrics["protocol"] != "TCP" {
		t.Error("Custom metrics not in stats")
	}
}

// Test 8: ResetStats
func TestTransportBase_ResetStats(t *testing.T) {
	tb := transport.NewTransportBase(transport.DefaultTransportConfig())

	// Generate some stats
	tb.RecordBytesSent(1000)
	tb.RecordBytesReceived(2000)
	tb.RecordSendError()
	tb.UpdateLatency(50 * time.Millisecond)
	tb.SetCustomMetric("test", "value")
	tb.UpdateConnectTime()

	// Reset stats
	tb.ResetStats()

	stats := tb.GetStats()
	if stats.BytesSent != 0 || stats.BytesReceived != 0 {
		t.Error("Byte counters not reset")
	}
	if stats.SendErrors != 0 {
		t.Error("Error counters not reset")
	}
	if stats.AverageLatency != 0 {
		t.Error("Latency not reset")
	}
	if stats.ConnectionCount != 0 {
		t.Error("Connection count not reset")
	}
	if stats.CustomMetrics == nil {
		t.Error("CustomMetrics should still be initialized")
	}
}

// Test 9: GetConnectionDuration
func TestTransportBase_GetConnectionDuration(t *testing.T) {
	tb := transport.NewTransportBase(transport.DefaultTransportConfig())

	// Not connected, should return 0
	duration := tb.GetConnectionDuration()
	if duration != 0 {
		t.Errorf("Duration when not connected = %v, want 0", duration)
	}

	// Connect and check duration
	tb.SetConnected(true)
	tb.UpdateConnectTime()

	time.Sleep(50 * time.Millisecond)

	duration = tb.GetConnectionDuration()
	if duration < 50*time.Millisecond {
		t.Errorf("Duration = %v, want >= 50ms", duration)
	}

	// Disconnect
	tb.SetConnected(false)
	duration = tb.GetConnectionDuration()
	if duration != 0 {
		t.Errorf("Duration after disconnect = %v, want 0", duration)
	}
}

// Test 10: GetThroughput
func TestTransportBase_GetThroughput(t *testing.T) {
	tb := transport.NewTransportBase(transport.DefaultTransportConfig())

	// Not connected, should return 0,0
	sendBps, receiveBps := tb.GetThroughput()
	if sendBps != 0 || receiveBps != 0 {
		t.Error("Throughput should be 0 when not connected")
	}

	// Connect and record data
	tb.SetConnected(true)
	tb.UpdateConnectTime()

	// Record 1000 bytes sent and 2000 bytes received
	tb.RecordBytesSent(1000)
	tb.RecordBytesReceived(2000)

	// Sleep to have measurable duration
	time.Sleep(100 * time.Millisecond)

	sendBps, receiveBps = tb.GetThroughput()

	// Should be approximately 10000 Bps and 20000 Bps
	// Allow some tolerance due to timing
	if sendBps < 9000 || sendBps > 11000 {
		t.Errorf("Send throughput = %f, want ~10000", sendBps)
	}
	if receiveBps < 19000 || receiveBps > 21000 {
		t.Errorf("Receive throughput = %f, want ~20000", receiveBps)
	}
}

// Test 11: Concurrent access
func TestTransportBase_Concurrent(t *testing.T) {
	tb := transport.NewTransportBase(transport.DefaultTransportConfig())

	var wg sync.WaitGroup
	numGoroutines := 10
	opsPerGoroutine := 100

	// Concurrent stats updates
	for i := 0; i < numGoroutines; i++ {
		wg.Add(1)
		go func(id int) {
			defer wg.Done()
			for j := 0; j < opsPerGoroutine; j++ {
				tb.RecordBytesSent(id)
				tb.RecordBytesReceived(id * 2)
				if j%10 == 0 {
					tb.RecordSendError()
				}
				if j%20 == 0 {
					tb.UpdateLatency(time.Duration(id) * time.Millisecond)
				}
				tb.SetCustomMetric("goroutine", id)
			}
		}(i)
	}

	// Concurrent reads
	for i := 0; i < numGoroutines; i++ {
		wg.Add(1)
		go func() {
			defer wg.Done()
			for j := 0; j < opsPerGoroutine; j++ {
				_ = tb.GetStats()
				_ = tb.IsConnected()
				_ = tb.GetConnectionDuration()
				tb.GetThroughput()
			}
		}()
	}

	wg.Wait()

	// Verify final stats are consistent
	stats := tb.GetStats()

	// Each goroutine sends its ID value 100 times
	// Sum of 0..9 = 45, times 100 = 4500
	expectedSent := int64(45 * opsPerGoroutine)
	if stats.BytesSent != expectedSent {
		t.Errorf("BytesSent = %d, want %d", stats.BytesSent, expectedSent)
	}

	expectedReceived := expectedSent * 2
	if stats.BytesReceived != expectedReceived {
		t.Errorf("BytesReceived = %d, want %d", stats.BytesReceived, expectedReceived)
	}

	// Each goroutine records 10 send errors (100/10)
	expectedSendErrors := int64(numGoroutines * 10)
	if stats.SendErrors != expectedSendErrors {
		t.Errorf("SendErrors = %d, want %d", stats.SendErrors, expectedSendErrors)
	}
}

// Test 12: GetStats returns a copy
func TestTransportBase_GetStats_ReturnsCopy(t *testing.T) {
	tb := transport.NewTransportBase(transport.DefaultTransportConfig())

	// Set some data
	tb.RecordBytesSent(100)
	tb.SetCustomMetric("key", "value")

	// Get stats
	stats1 := tb.GetStats()

	// Modify the returned stats
	stats1.BytesSent = 999
	stats1.CustomMetrics["key"] = "modified"
	stats1.CustomMetrics["new"] = "added"

	// Get stats again
	stats2 := tb.GetStats()

	// Original should be unchanged
	if stats2.BytesSent != 100 {
		t.Errorf("BytesSent = %d, want 100 (not modified)", stats2.BytesSent)
	}
	if stats2.CustomMetrics["key"] != "value" {
		t.Errorf("CustomMetric = %v, want 'value' (not modified)", stats2.CustomMetrics["key"])
	}
	if _, exists := stats2.CustomMetrics["new"]; exists {
		t.Error("New key should not exist in original")
	}
}

// Benchmarks

func BenchmarkTransportBase_RecordBytesSent(b *testing.B) {
	tb := transport.NewTransportBase(transport.DefaultTransportConfig())

	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		tb.RecordBytesSent(100)
	}
}

func BenchmarkTransportBase_GetStats(b *testing.B) {
	tb := transport.NewTransportBase(transport.DefaultTransportConfig())

	// Add some data
	for i := 0; i < 10; i++ {
		tb.SetCustomMetric("key"+string(rune('0'+i)), i)
	}

	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		_ = tb.GetStats()
	}
}

func BenchmarkTransportBase_UpdateLatency(b *testing.B) {
	tb := transport.NewTransportBase(transport.DefaultTransportConfig())

	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		tb.UpdateLatency(time.Duration(i) * time.Microsecond)
	}
}

func BenchmarkTransportBase_Concurrent(b *testing.B) {
	tb := transport.NewTransportBase(transport.DefaultTransportConfig())

	b.RunParallel(func(pb *testing.PB) {
		i := 0
		for pb.Next() {
			if i%3 == 0 {
				tb.RecordBytesSent(100)
			} else if i%3 == 1 {
				tb.RecordBytesReceived(200)
			} else {
				_ = tb.GetStats()
			}
			i++
		}
	})
}
