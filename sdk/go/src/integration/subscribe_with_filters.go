// Package integration provides MCP SDK integration.
package integration

import (
	"fmt"
	"sync"
	"sync/atomic"
)

// Subscription represents an active subscription.
type Subscription struct {
	ID         string
	Resource   string
	Filters    []Filter
	Chain      *FilterChain
	Active     bool
	mu         sync.RWMutex
}

// SubscribeWithFilters subscribes to resources with filters.
func (fc *FilteredMCPClient) SubscribeWithFilters(resource string, filters ...Filter) (*Subscription, error) {
	// Create subscription-specific filter chain
	subChain := NewFilterChain()
	for _, filter := range filters {
		subChain.Add(filter)
	}
	
	// Create subscription
	subscription := &Subscription{
		ID:       generateSubscriptionID(),
		Resource: resource,
		Filters:  filters,
		Chain:    subChain,
		Active:   true,
	}
	
	// Register subscription
	fc.mu.Lock()
	if fc.subscriptions == nil {
		fc.subscriptions = make(map[string]*Subscription)
	}
	fc.subscriptions[subscription.ID] = subscription
	fc.mu.Unlock()
	
	// Subscribe through MCP client
	// err := fc.MCPClient.Subscribe(resource)
	// if err != nil {
	//     fc.mu.Lock()
	//     delete(fc.subscriptions, subscription.ID)
	//     fc.mu.Unlock()
	//     return nil, err
	// }
	
	// Start handling notifications for this subscription
	go fc.handleSubscriptionNotifications(subscription)
	
	return subscription, nil
}

// handleSubscriptionNotifications processes notifications for a subscription.
func (fc *FilteredMCPClient) handleSubscriptionNotifications(sub *Subscription) {
	for {
		sub.mu.RLock()
		if !sub.Active {
			sub.mu.RUnlock()
			break
		}
		sub.mu.RUnlock()
		
		// In real implementation, would receive notifications from MCP client
		// notification := fc.MCPClient.ReceiveNotification()
		
		// Simulate notification
		notification := map[string]interface{}{
			"resource": sub.Resource,
			"data":     "notification_data",
		}
		
		// Apply subscription filters
		notifData, err := serializeNotification(notification)
		if err != nil {
			continue
		}
		
		filtered, err := sub.Chain.Process(notifData)
		if err != nil {
			// Log filter error
			continue
		}
		
		// Deliver filtered notification
		fc.deliverNotification(sub.ID, filtered)
		
		// For simulation, break after one iteration
		break
	}
}

// deliverNotification delivers filtered notification to handlers.
func (fc *FilteredMCPClient) deliverNotification(subscriptionID string, data []byte) {
	// Deserialize notification
	notification, err := deserializeNotification(data)
	if err != nil {
		return
	}
	
	// Call registered handlers
	fc.mu.RLock()
	handlers := fc.notificationHandlers[subscriptionID]
	fc.mu.RUnlock()
	
	for _, handler := range handlers {
		handler(notification)
	}
}

// Unsubscribe cancels a subscription.
func (sub *Subscription) Unsubscribe() error {
	sub.mu.Lock()
	sub.Active = false
	sub.mu.Unlock()
	
	// Unsubscribe through MCP client
	// return fc.MCPClient.Unsubscribe(sub.Resource)
	return nil
}

// UpdateFilters updates subscription filters.
func (sub *Subscription) UpdateFilters(filters ...Filter) {
	sub.mu.Lock()
	defer sub.mu.Unlock()
	
	// Create new chain
	newChain := NewFilterChain()
	for _, filter := range filters {
		newChain.Add(filter)
	}
	
	sub.Filters = filters
	sub.Chain = newChain
}

// generateSubscriptionID creates unique subscription ID.
func generateSubscriptionID() string {
	// In real implementation, use UUID or similar
	return fmt.Sprintf("sub_%d", subscriptionCounter.Add(1))
}

// subscriptionCounter for generating IDs.
var subscriptionCounter atomic.Int64

// serializeNotification converts notification to bytes.
func serializeNotification(notification interface{}) ([]byte, error) {
	return []byte(fmt.Sprintf("%v", notification)), nil
}

// deserializeNotification converts bytes to notification.
func deserializeNotification(data []byte) (interface{}, error) {
	return map[string]interface{}{
		"data": string(data),
	}, nil
}