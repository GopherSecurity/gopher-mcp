// Package manager provides filter and chain management for the MCP Filter SDK.
package manager

import (
	"sync"
	"time"
	
	"github.com/google/uuid"
)

// Event types
type (
	FilterRegisteredEvent struct {
		FilterID   uuid.UUID
		FilterName string
		Timestamp  time.Time
	}
	
	FilterUnregisteredEvent struct {
		FilterID   uuid.UUID
		FilterName string
		Timestamp  time.Time
	}
	
	ChainCreatedEvent struct {
		ChainName string
		Timestamp time.Time
	}
	
	ChainRemovedEvent struct {
		ChainName string
		Timestamp time.Time
	}
	
	ProcessingStartEvent struct {
		FilterID  uuid.UUID
		ChainName string
		Timestamp time.Time
	}
	
	ProcessingCompleteEvent struct {
		FilterID  uuid.UUID
		ChainName string
		Duration  time.Duration
		Success   bool
		Timestamp time.Time
	}
	
	ManagerStartedEvent struct {
		Timestamp time.Time
	}
	
	ManagerStoppedEvent struct {
		Timestamp time.Time
	}
)

// EventBus manages event subscriptions and emissions.
type EventBus struct {
	subscribers map[string][]EventHandler
	buffer      chan interface{}
	stopCh      chan struct{}
	mu          sync.RWMutex
}

// EventHandler processes events.
type EventHandler func(event interface{})

// NewEventBus creates a new event bus.
func NewEventBus(bufferSize int) *EventBus {
	return &EventBus{
		subscribers: make(map[string][]EventHandler),
		buffer:      make(chan interface{}, bufferSize),
		stopCh:      make(chan struct{}),
	}
}

// Subscribe adds an event handler for a specific event type.
func (eb *EventBus) Subscribe(eventType string, handler EventHandler) {
	eb.mu.Lock()
	defer eb.mu.Unlock()
	
	eb.subscribers[eventType] = append(eb.subscribers[eventType], handler)
}

// Unsubscribe removes all handlers for an event type.
func (eb *EventBus) Unsubscribe(eventType string) {
	eb.mu.Lock()
	defer eb.mu.Unlock()
	
	delete(eb.subscribers, eventType)
}

// Emit sends an event to all subscribers.
func (eb *EventBus) Emit(event interface{}) {
	select {
	case eb.buffer <- event:
	default:
		// Buffer full, drop event
	}
}

// Start begins event processing.
func (eb *EventBus) Start() {
	go eb.processEvents()
}

// Stop stops event processing.
func (eb *EventBus) Stop() {
	close(eb.stopCh)
}

// processEvents processes queued events.
func (eb *EventBus) processEvents() {
	for {
		select {
		case event := <-eb.buffer:
			eb.dispatch(event)
		case <-eb.stopCh:
			// Process remaining events
			for len(eb.buffer) > 0 {
				event := <-eb.buffer
				eb.dispatch(event)
			}
			return
		}
	}
}

// dispatch sends event to appropriate handlers.
func (eb *EventBus) dispatch(event interface{}) {
	eb.mu.RLock()
	defer eb.mu.RUnlock()
	
	// Get event type name
	var eventType string
	switch event.(type) {
	case FilterRegisteredEvent:
		eventType = "FilterRegistered"
	case FilterUnregisteredEvent:
		eventType = "FilterUnregistered"
	case ChainCreatedEvent:
		eventType = "ChainCreated"
	case ChainRemovedEvent:
		eventType = "ChainRemoved"
	case ProcessingStartEvent:
		eventType = "ProcessingStart"
	case ProcessingCompleteEvent:
		eventType = "ProcessingComplete"
	case ManagerStartedEvent:
		eventType = "ManagerStarted"
	case ManagerStoppedEvent:
		eventType = "ManagerStopped"
	default:
		eventType = "Unknown"
	}
	
	// Call handlers
	if handlers, ok := eb.subscribers[eventType]; ok {
		for _, handler := range handlers {
			handler(event)
		}
	}
	
	// Call wildcard handlers
	if handlers, ok := eb.subscribers["*"]; ok {
		for _, handler := range handlers {
			handler(event)
		}
	}
}

// SetupEventHandlers configures default event handlers for the manager.
func (fm *FilterManager) SetupEventHandlers() {
	// Subscribe to filter events
	fm.events.Subscribe("FilterRegistered", func(event interface{}) {
		if e, ok := event.(FilterRegisteredEvent); ok {
			// Log or handle filter registration
			_ = e
		}
	})
	
	fm.events.Subscribe("FilterUnregistered", func(event interface{}) {
		if e, ok := event.(FilterUnregisteredEvent); ok {
			// Log or handle filter unregistration
			_ = e
		}
	})
	
	// Subscribe to chain events
	fm.events.Subscribe("ChainCreated", func(event interface{}) {
		if e, ok := event.(ChainCreatedEvent); ok {
			// Log or handle chain creation
			_ = e
		}
	})
	
	fm.events.Subscribe("ChainRemoved", func(event interface{}) {
		if e, ok := event.(ChainRemovedEvent); ok {
			// Log or handle chain removal
			_ = e
		}
	})
	
	// Subscribe to processing events
	fm.events.Subscribe("ProcessingComplete", func(event interface{}) {
		if e, ok := event.(ProcessingCompleteEvent); ok {
			// Update statistics
			_ = e
		}
	})
}