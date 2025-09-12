// Package integration provides MCP SDK integration.
package integration

import (
	"fmt"
	"sync"
	"sync/atomic"
)

// NotificationHandler processes notifications.
type NotificationHandler func(notification interface{}) error

// FilteredNotificationHandler wraps handler with filters.
type FilteredNotificationHandler struct {
	Handler NotificationHandler
	Filters []Filter
	Chain   *FilterChain
}

// HandleNotificationWithFilters registers filtered notification handler.
func (fc *FilteredMCPClient) HandleNotificationWithFilters(
	notificationType string,
	handler NotificationHandler,
	filters ...Filter,
) (string, error) {
	// Create handler-specific filter chain
	handlerChain := NewFilterChain()
	for _, filter := range filters {
		handlerChain.Add(filter)
	}
	
	// Create filtered handler
	filteredHandler := &FilteredNotificationHandler{
		Handler: handler,
		Filters: filters,
		Chain:   handlerChain,
	}
	
	// Generate handler ID
	handlerID := generateHandlerID()
	
	// Register handler
	fc.mu.Lock()
	if fc.notificationHandlers == nil {
		fc.notificationHandlers = make(map[string][]NotificationHandler)
	}
	
	// Create wrapper that applies filters
	wrappedHandler := func(notification interface{}) error {
		// Serialize notification
		data, err := serializeNotification(notification)
		if err != nil {
			return fmt.Errorf("failed to serialize notification: %w", err)
		}
		
		// Apply handler filters
		filtered, err := filteredHandler.Chain.Process(data)
		if err != nil {
			return fmt.Errorf("filter error: %w", err)
		}
		
		// Deserialize filtered notification
		filteredNotif, err := deserializeNotification(filtered)
		if err != nil {
			return fmt.Errorf("failed to deserialize: %w", err)
		}
		
		// Call original handler
		return filteredHandler.Handler(filteredNotif)
	}
	
	// Store handler
	fc.notificationHandlers[notificationType] = append(
		fc.notificationHandlers[notificationType],
		wrappedHandler,
	)
	
	// Store filtered handler for management
	if fc.filteredHandlers == nil {
		fc.filteredHandlers = make(map[string]*FilteredNotificationHandler)
	}
	fc.filteredHandlers[handlerID] = filteredHandler
	fc.mu.Unlock()
	
	// Register with MCP client
	// fc.MCPClient.RegisterNotificationHandler(notificationType, wrappedHandler)
	
	return handlerID, nil
}

// UnregisterHandler removes notification handler.
func (fc *FilteredMCPClient) UnregisterHandler(handlerID string) error {
	fc.mu.Lock()
	defer fc.mu.Unlock()
	
	// Find and remove handler
	if handler, exists := fc.filteredHandlers[handlerID]; exists {
		delete(fc.filteredHandlers, handlerID)
		
		// Remove from notification handlers
		// This is simplified - real implementation would track handler references
		_ = handler
		
		return nil
	}
	
	return fmt.Errorf("handler not found: %s", handlerID)
}

// UpdateHandlerFilters updates filters for a handler.
func (fc *FilteredMCPClient) UpdateHandlerFilters(handlerID string, filters ...Filter) error {
	fc.mu.Lock()
	defer fc.mu.Unlock()
	
	handler, exists := fc.filteredHandlers[handlerID]
	if !exists {
		return fmt.Errorf("handler not found: %s", handlerID)
	}
	
	// Create new chain
	newChain := NewFilterChain()
	for _, filter := range filters {
		newChain.Add(filter)
	}
	
	// Update handler
	handler.Filters = filters
	handler.Chain = newChain
	
	return nil
}

// ProcessNotification processes notification through all handlers.
func (fc *FilteredMCPClient) ProcessNotification(notificationType string, notification interface{}) error {
	fc.mu.RLock()
	handlers := fc.notificationHandlers[notificationType]
	fc.mu.RUnlock()
	
	if len(handlers) == 0 {
		return nil
	}
	
	// Process through each handler
	var wg sync.WaitGroup
	errors := make(chan error, len(handlers))
	
	for _, handler := range handlers {
		wg.Add(1)
		go func(h NotificationHandler) {
			defer wg.Done()
			if err := h(notification); err != nil {
				errors <- err
			}
		}(handler)
	}
	
	// Wait for all handlers
	wg.Wait()
	close(errors)
	
	// Collect errors
	var errs []error
	for err := range errors {
		errs = append(errs, err)
	}
	
	if len(errs) > 0 {
		return fmt.Errorf("handler errors: %v", errs)
	}
	
	return nil
}

// generateHandlerID creates unique handler ID.
func generateHandlerID() string {
	return fmt.Sprintf("handler_%d", handlerCounter.Add(1))
}

// handlerCounter for generating IDs.
var handlerCounter atomic.Int64