// Package transport provides communication transports for the MCP Filter SDK.
package transport

import (
	"context"
	"sync"
	"time"
)

// TcpReconnectManager handles TCP reconnection logic.
type TcpReconnectManager struct {
	transport       *TcpTransport
	config          ReconnectConfig
	messageQueue    [][]byte
	reconnecting    bool
	attempts        int
	lastAttempt     time.Time
	onReconnect     func()
	onReconnectFail func(error)
	mu              sync.Mutex
}

// ReconnectConfig configures reconnection behavior.
type ReconnectConfig struct {
	Enabled           bool
	MaxAttempts       int
	InitialDelay      time.Duration
	MaxDelay          time.Duration
	BackoffMultiplier float64
	MaxQueueSize      int
}

// DefaultReconnectConfig returns default reconnection configuration.
func DefaultReconnectConfig() ReconnectConfig {
	return ReconnectConfig{
		Enabled:           true,
		MaxAttempts:       10,
		InitialDelay:      1 * time.Second,
		MaxDelay:          60 * time.Second,
		BackoffMultiplier: 2.0,
		MaxQueueSize:      1000,
	}
}

// NewTcpReconnectManager creates a new reconnection manager.
func NewTcpReconnectManager(transport *TcpTransport, config ReconnectConfig) *TcpReconnectManager {
	return &TcpReconnectManager{
		transport:    transport,
		config:       config,
		messageQueue: make([][]byte, 0, config.MaxQueueSize),
	}
}

// HandleConnectionLoss initiates reconnection on connection loss.
func (rm *TcpReconnectManager) HandleConnectionLoss() {
	rm.mu.Lock()
	if rm.reconnecting {
		rm.mu.Unlock()
		return
	}
	rm.reconnecting = true
	rm.attempts = 0
	rm.mu.Unlock()

	go rm.reconnectLoop()
}

// reconnectLoop attempts reconnection with exponential backoff.
func (rm *TcpReconnectManager) reconnectLoop() {
	delay := rm.config.InitialDelay

	for rm.attempts < rm.config.MaxAttempts {
		rm.attempts++
		rm.lastAttempt = time.Now()

		// Wait before attempting
		time.Sleep(delay)

		// Attempt reconnection
		ctx, cancel := context.WithTimeout(context.Background(), 30*time.Second)
		err := rm.transport.Connect(ctx)
		cancel()

		if err == nil {
			// Success
			rm.mu.Lock()
			rm.reconnecting = false
			rm.mu.Unlock()

			// Flush queued messages
			rm.flushQueue()

			// Notify success
			if rm.onReconnect != nil {
				rm.onReconnect()
			}
			return
		}

		// Calculate next delay with exponential backoff
		delay = time.Duration(float64(delay) * rm.config.BackoffMultiplier)
		if delay > rm.config.MaxDelay {
			delay = rm.config.MaxDelay
		}
	}

	// Max attempts reached
	rm.mu.Lock()
	rm.reconnecting = false
	rm.mu.Unlock()

	if rm.onReconnectFail != nil {
		rm.onReconnectFail(ErrMaxReconnectAttempts)
	}
}

// QueueMessage queues message during reconnection.
func (rm *TcpReconnectManager) QueueMessage(data []byte) error {
	rm.mu.Lock()
	defer rm.mu.Unlock()

	if len(rm.messageQueue) >= rm.config.MaxQueueSize {
		return ErrQueueFull
	}

	// Make a copy of the data
	msg := make([]byte, len(data))
	copy(msg, data)
	rm.messageQueue = append(rm.messageQueue, msg)

	return nil
}

// flushQueue sends queued messages after reconnection.
func (rm *TcpReconnectManager) flushQueue() {
	rm.mu.Lock()
	queue := rm.messageQueue
	rm.messageQueue = make([][]byte, 0, rm.config.MaxQueueSize)
	rm.mu.Unlock()

	for _, msg := range queue {
		if err := rm.transport.Send(msg); err != nil {
			// Re-queue failed message
			rm.QueueMessage(msg)
			break
		}
	}
}

// IsReconnecting returns true if currently reconnecting.
func (rm *TcpReconnectManager) IsReconnecting() bool {
	rm.mu.Lock()
	defer rm.mu.Unlock()
	return rm.reconnecting
}

// GetStatus returns reconnection status.
func (rm *TcpReconnectManager) GetStatus() ReconnectStatus {
	rm.mu.Lock()
	defer rm.mu.Unlock()

	return ReconnectStatus{
		Reconnecting:   rm.reconnecting,
		Attempts:       rm.attempts,
		LastAttempt:    rm.lastAttempt,
		QueuedMessages: len(rm.messageQueue),
	}
}

// ReconnectStatus contains reconnection state information.
type ReconnectStatus struct {
	Reconnecting   bool
	Attempts       int
	LastAttempt    time.Time
	QueuedMessages int
}

// Error definitions
var (
	ErrMaxReconnectAttempts = &TransportError{
		Code:    "MAX_RECONNECT_ATTEMPTS",
		Message: "maximum reconnection attempts reached",
	}

	ErrQueueFull = &TransportError{
		Code:    "QUEUE_FULL",
		Message: "message queue is full",
	}
)
