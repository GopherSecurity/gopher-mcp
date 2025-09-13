// Package core provides the core interfaces and types for the MCP Filter SDK.
package core

import (
	"fmt"
	"sync"
	"sync/atomic"
	"time"
)

// Event represents an event that can trigger callbacks.
type Event interface {
	// Name returns the event name.
	Name() string

	// Data returns the event data.
	Data() interface{}
}

// SimpleEvent is a basic implementation of the Event interface.
type SimpleEvent struct {
	name string
	data interface{}
}

// Name returns the event name.
func (e *SimpleEvent) Name() string {
	return e.name
}

// Data returns the event data.
func (e *SimpleEvent) Data() interface{} {
	return e.data
}

// NewEvent creates a new event with the given name and data.
func NewEvent(name string, data interface{}) Event {
	return &SimpleEvent{name: name, data: data}
}

// CallbackFunc is a function that handles events.
type CallbackFunc func(event Event) error

// ErrorCallback is a function that handles callback errors.
type ErrorCallback func(error)

// CallbackID uniquely identifies a registered callback.
type CallbackID uint64

// CallbackStatistics tracks callback execution metrics.
type CallbackStatistics struct {
	// TotalCallbacks is the total number of callbacks triggered
	TotalCallbacks uint64

	// SuccessfulCallbacks is the number of callbacks that completed successfully
	SuccessfulCallbacks uint64

	// FailedCallbacks is the number of callbacks that returned errors
	FailedCallbacks uint64

	// PanickedCallbacks is the number of callbacks that panicked
	PanickedCallbacks uint64

	// TotalExecutionTime is the cumulative execution time
	TotalExecutionTime time.Duration

	// AverageExecutionTime is the average callback execution time
	AverageExecutionTime time.Duration
}

// CallbackManager manages event callbacks with support for sync and async execution.
type CallbackManager struct {
	// callbacks maps event names to their registered handlers
	callbacks map[string]map[CallbackID]CallbackFunc

	// mu protects concurrent access to callbacks
	mu sync.RWMutex

	// async determines if callbacks run asynchronously
	async bool

	// errorHandler handles callback errors
	errorHandler ErrorCallback

	// stats tracks callback statistics
	stats CallbackStatistics

	// nextID generates unique callback IDs
	nextID uint64

	// timeout for async callback execution
	timeout time.Duration
}

// NewCallbackManager creates a new callback manager.
func NewCallbackManager(async bool) *CallbackManager {
	return &CallbackManager{
		callbacks: make(map[string]map[CallbackID]CallbackFunc),
		async:     async,
		timeout:   30 * time.Second, // Default 30 second timeout
	}
}

// SetErrorHandler sets the error handler for callback errors.
func (cm *CallbackManager) SetErrorHandler(handler ErrorCallback) {
	cm.errorHandler = handler
}

// SetTimeout sets the timeout for async callback execution.
func (cm *CallbackManager) SetTimeout(timeout time.Duration) {
	cm.timeout = timeout
}

// Register adds a handler for the specified event.
// Returns a CallbackID that can be used to unregister the handler.
func (cm *CallbackManager) Register(event string, handler CallbackFunc) (CallbackID, error) {
	if event == "" {
		return 0, fmt.Errorf("event name cannot be empty")
	}
	if handler == nil {
		return 0, fmt.Errorf("handler cannot be nil")
	}

	cm.mu.Lock()
	defer cm.mu.Unlock()

	// Generate unique ID
	id := CallbackID(atomic.AddUint64(&cm.nextID, 1))

	// Initialize event map if needed
	if cm.callbacks[event] == nil {
		cm.callbacks[event] = make(map[CallbackID]CallbackFunc)
	}

	// Register the handler
	cm.callbacks[event][id] = handler

	return id, nil
}

// Unregister removes a handler by its ID.
func (cm *CallbackManager) Unregister(event string, id CallbackID) error {
	cm.mu.Lock()
	defer cm.mu.Unlock()

	if handlers, ok := cm.callbacks[event]; ok {
		delete(handlers, id)
		if len(handlers) == 0 {
			delete(cm.callbacks, event)
		}
		return nil
	}

	return fmt.Errorf("callback not found for event %s with id %d", event, id)
}

// Trigger calls all registered handlers for the specified event.
func (cm *CallbackManager) Trigger(event string, data interface{}) error {
	evt := NewEvent(event, data)

	// Get handlers
	cm.mu.RLock()
	handlers := make([]CallbackFunc, 0)
	if eventHandlers, ok := cm.callbacks[event]; ok {
		for _, handler := range eventHandlers {
			handlers = append(handlers, handler)
		}
	}
	cm.mu.RUnlock()

	if len(handlers) == 0 {
		return nil
	}

	if cm.async {
		return cm.triggerAsync(evt, handlers)
	}
	return cm.triggerSync(evt, handlers)
}

// triggerSync executes callbacks synchronously.
func (cm *CallbackManager) triggerSync(event Event, handlers []CallbackFunc) error {
	var errors []error

	for _, handler := range handlers {
		startTime := time.Now()
		err := cm.executeCallback(handler, event)
		duration := time.Since(startTime)

		cm.updateStats(err == nil, false, duration)

		if err != nil {
			errors = append(errors, err)
			if cm.errorHandler != nil {
				cm.errorHandler(err)
			}
		}
	}

	if len(errors) > 0 {
		return fmt.Errorf("callback errors: %v", errors)
	}
	return nil
}

// triggerAsync executes callbacks asynchronously with timeout support.
func (cm *CallbackManager) triggerAsync(event Event, handlers []CallbackFunc) error {
	var wg sync.WaitGroup
	errChan := make(chan error, len(handlers))
	done := make(chan struct{})

	for _, handler := range handlers {
		wg.Add(1)
		go func(h CallbackFunc) {
			defer wg.Done()

			startTime := time.Now()
			err := cm.executeCallback(h, event)
			duration := time.Since(startTime)

			cm.updateStats(err == nil, false, duration)

			if err != nil {
				errChan <- err
				if cm.errorHandler != nil {
					cm.errorHandler(err)
				}
			}
		}(handler)
	}

	// Wait for completion or timeout
	go func() {
		wg.Wait()
		close(done)
	}()

	select {
	case <-done:
		// All callbacks completed
		close(errChan)
		var errors []error
		for err := range errChan {
			errors = append(errors, err)
		}
		if len(errors) > 0 {
			return fmt.Errorf("async callback errors: %v", errors)
		}
		return nil
	case <-time.After(cm.timeout):
		return fmt.Errorf("callback execution timeout after %v", cm.timeout)
	}
}

// executeCallback executes a single callback with panic recovery.
func (cm *CallbackManager) executeCallback(handler CallbackFunc, event Event) (err error) {
	defer func() {
		if r := recover(); r != nil {
			err = fmt.Errorf("callback panicked: %v", r)
			cm.updateStats(false, true, 0)
		}
	}()

	return handler(event)
}

// updateStats updates callback statistics.
func (cm *CallbackManager) updateStats(success bool, panicked bool, duration time.Duration) {
	cm.mu.Lock()
	defer cm.mu.Unlock()

	cm.stats.TotalCallbacks++

	if panicked {
		cm.stats.PanickedCallbacks++
	} else if success {
		cm.stats.SuccessfulCallbacks++
	} else {
		cm.stats.FailedCallbacks++
	}

	cm.stats.TotalExecutionTime += duration
	if cm.stats.TotalCallbacks > 0 {
		cm.stats.AverageExecutionTime = cm.stats.TotalExecutionTime / time.Duration(cm.stats.TotalCallbacks)
	}
}

// GetStatistics returns callback execution statistics.
func (cm *CallbackManager) GetStatistics() CallbackStatistics {
	cm.mu.RLock()
	defer cm.mu.RUnlock()
	return cm.stats
}
