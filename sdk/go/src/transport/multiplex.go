// Package transport provides communication transports for the MCP Filter SDK.
package transport

import (
	"context"
	"fmt"
	"sync"
	"sync/atomic"
	"time"
)

// MultiplexTransport allows multiple transports with fallback.
type MultiplexTransport struct {
	TransportBase

	// Transports
	primary   Transport
	fallbacks []Transport
	active    atomic.Value // *Transport

	// Configuration
	config MultiplexConfig

	// Health monitoring
	healthChecks map[Transport]*HealthStatus
	healthMu     sync.RWMutex

	// Load balancing
	roundRobin atomic.Uint64
}

// MultiplexConfig configures multiplex transport behavior.
type MultiplexConfig struct {
	AutoFallback        bool
	HealthCheckInterval time.Duration
	LoadBalancing       bool
	FailoverDelay       time.Duration
}

// HealthStatus tracks transport health.
type HealthStatus struct {
	Healthy      bool
	LastCheck    time.Time
	FailureCount int
	SuccessCount int
}

// NewMultiplexTransport creates a new multiplex transport.
func NewMultiplexTransport(primary Transport, fallbacks []Transport, config MultiplexConfig) *MultiplexTransport {
	mt := &MultiplexTransport{
		TransportBase: NewTransportBase(DefaultTransportConfig()),
		primary:       primary,
		fallbacks:     fallbacks,
		config:        config,
		healthChecks:  make(map[Transport]*HealthStatus),
	}

	mt.active.Store(primary)

	// Initialize health status
	mt.healthChecks[primary] = &HealthStatus{Healthy: true}
	for _, fb := range fallbacks {
		mt.healthChecks[fb] = &HealthStatus{Healthy: true}
	}

	return mt
}

// Connect connects all transports.
func (mt *MultiplexTransport) Connect(ctx context.Context) error {
	if !mt.SetConnected(true) {
		return ErrAlreadyConnected
	}

	// Try primary first
	if err := mt.primary.Connect(ctx); err == nil {
		mt.active.Store(mt.primary)
		mt.UpdateConnectTime()
		go mt.monitorHealth()
		return nil
	}

	// Try fallbacks
	for _, fb := range mt.fallbacks {
		if err := fb.Connect(ctx); err == nil {
			mt.active.Store(fb)
			mt.UpdateConnectTime()
			go mt.monitorHealth()
			return nil
		}
	}

	mt.SetConnected(false)
	return fmt.Errorf("all transports failed to connect")
}

// Send sends data through active transport.
func (mt *MultiplexTransport) Send(data []byte) error {
	transport := mt.getActiveTransport()
	if transport == nil {
		return ErrNotConnected
	}

	err := transport.Send(data)
	if err != nil && mt.config.AutoFallback {
		// Try fallback
		if newTransport := mt.selectFallback(); newTransport != nil {
			mt.active.Store(newTransport)
			return newTransport.Send(data)
		}
	}

	return err
}

// Receive receives data from active transport.
func (mt *MultiplexTransport) Receive() ([]byte, error) {
	transport := mt.getActiveTransport()
	if transport == nil {
		return nil, ErrNotConnected
	}

	data, err := transport.Receive()
	if err != nil && mt.config.AutoFallback {
		// Try fallback
		if newTransport := mt.selectFallback(); newTransport != nil {
			mt.active.Store(newTransport)
			return newTransport.Receive()
		}
	}

	return data, err
}

// getActiveTransport returns the currently active transport.
func (mt *MultiplexTransport) getActiveTransport() Transport {
	if v := mt.active.Load(); v != nil {
		return v.(Transport)
	}
	return nil
}

// selectFallback selects a healthy fallback transport.
func (mt *MultiplexTransport) selectFallback() Transport {
	mt.healthMu.RLock()
	defer mt.healthMu.RUnlock()

	// Check primary first
	if status, ok := mt.healthChecks[mt.primary]; ok && status.Healthy {
		return mt.primary
	}

	// Check fallbacks
	for _, fb := range mt.fallbacks {
		if status, ok := mt.healthChecks[fb]; ok && status.Healthy {
			return fb
		}
	}

	return nil
}

// monitorHealth monitors transport health.
func (mt *MultiplexTransport) monitorHealth() {
	ticker := time.NewTicker(mt.config.HealthCheckInterval)
	defer ticker.Stop()

	for mt.IsConnected() {
		<-ticker.C
		mt.checkAllHealth()
	}
}

// checkAllHealth checks health of all transports.
func (mt *MultiplexTransport) checkAllHealth() {
	mt.healthMu.Lock()
	defer mt.healthMu.Unlock()

	// Check primary
	mt.checkTransportHealth(mt.primary)

	// Check fallbacks
	for _, fb := range mt.fallbacks {
		mt.checkTransportHealth(fb)
	}
}

// checkTransportHealth checks individual transport health.
func (mt *MultiplexTransport) checkTransportHealth(t Transport) {
	status := mt.healthChecks[t]

	// Simple health check - try to get stats
	if t.IsConnected() {
		status.Healthy = true
		status.SuccessCount++
		status.FailureCount = 0
	} else {
		status.Healthy = false
		status.FailureCount++
		status.SuccessCount = 0
	}

	status.LastCheck = time.Now()
}

// Disconnect disconnects all transports.
func (mt *MultiplexTransport) Disconnect() error {
	if !mt.SetConnected(false) {
		return nil
	}

	// Disconnect all
	mt.primary.Disconnect()
	for _, fb := range mt.fallbacks {
		fb.Disconnect()
	}

	mt.UpdateDisconnectTime()
	return nil
}
