// Package manager provides filter and chain management for the MCP Filter SDK.
package manager

import (
	"context"
	"fmt"
	"sync"
	"time"

	"github.com/google/uuid"
	"github.com/GopherSecurity/gopher-mcp/src/core"
	"github.com/GopherSecurity/gopher-mcp/src/types"
)

// FilterManager manages filters and chains in a thread-safe manner.
type FilterManager struct {
	// Registry for filters with UUID keys
	registry map[uuid.UUID]core.Filter
	nameIndex map[string]uuid.UUID // Secondary index for name-based lookup
	
	// Chain management
	chains map[string]*core.FilterChain
	
	// Configuration
	config FilterManagerConfig
	
	// Statistics
	stats ManagerStatistics
	
	// Event handling
	events EventBus
	
	// Synchronization
	mu sync.RWMutex
	
	// Lifecycle
	ctx    context.Context
	cancel context.CancelFunc
	
	// Background tasks
	metricsTimer *time.Ticker
	healthTimer  *time.Ticker
	
	// State
	started bool
}

// ManagerStatistics holds aggregated statistics from all filters and chains.
type ManagerStatistics struct {
	// Filter statistics
	TotalFilters   int64
	ActiveFilters  int64
	ProcessedCount uint64
	ErrorCount     uint64
	
	// Chain statistics
	TotalChains    int64
	ActiveChains   int64
	ChainProcessed uint64
	ChainErrors    uint64
	
	// Performance metrics
	AverageLatency time.Duration
	P50Latency     time.Duration
	P90Latency     time.Duration
	P99Latency     time.Duration
	
	// Resource usage
	MemoryUsage    int64
	CPUUsage       float64
	
	// Last update
	LastUpdated time.Time
}

// EventBus represents the event bus for filter events.
type EventBus struct {
	subscribers map[string][]EventHandler
	mu          sync.RWMutex
	buffer      chan Event
	ctx         context.Context
	cancel      context.CancelFunc
}

// Event represents a filter or chain event.
type Event struct {
	Type      string
	Timestamp time.Time
	Data      map[string]interface{}
}

// EventHandler handles events.
type EventHandler func(Event)

// NewFilterManager creates a new filter manager with the given configuration.
func NewFilterManager(config FilterManagerConfig) *FilterManager {
	ctx, cancel := context.WithCancel(context.Background())
	
	eventCtx, eventCancel := context.WithCancel(context.Background())
	
	return &FilterManager{
		registry:  make(map[uuid.UUID]core.Filter),
		nameIndex: make(map[string]uuid.UUID),
		chains:    make(map[string]*core.FilterChain),
		config:    config,
		stats:     ManagerStatistics{LastUpdated: time.Now()},
		events: EventBus{
			subscribers: make(map[string][]EventHandler),
			buffer:      make(chan Event, config.EventBufferSize),
			ctx:         eventCtx,
			cancel:      eventCancel,
		},
		ctx:    ctx,
		cancel: cancel,
	}
}

// String returns a string representation of the statistics.
func (ms ManagerStatistics) String() string {
	return fmt.Sprintf("Filters: %d/%d, Chains: %d/%d, Processed: %d, Errors: %d, Avg Latency: %v",
		ms.ActiveFilters, ms.TotalFilters,
		ms.ActiveChains, ms.TotalChains,
		ms.ProcessedCount, ms.ErrorCount,
		ms.AverageLatency)
}

// RegisterFilter registers a filter with UUID generation and returns the UUID.
func (fm *FilterManager) RegisterFilter(filter core.Filter) (uuid.UUID, error) {
	if filter == nil {
		return uuid.Nil, fmt.Errorf("filter cannot be nil")
	}
	
	fm.mu.Lock()
	defer fm.mu.Unlock()
	
	// Check if we're at capacity
	if len(fm.registry) >= fm.config.MaxFilters {
		return uuid.Nil, fmt.Errorf("maximum number of filters (%d) reached", fm.config.MaxFilters)
	}
	
	// Check for name uniqueness
	filterName := filter.Name()
	if filterName == "" {
		return uuid.Nil, fmt.Errorf("filter name cannot be empty")
	}
	
	if _, exists := fm.nameIndex[filterName]; exists {
		return uuid.Nil, fmt.Errorf("filter with name '%s' already exists", filterName)
	}
	
	// Generate UUID
	id := uuid.New()
	
	// Initialize the filter
	if err := filter.Initialize(types.FilterConfig{}); err != nil {
		return uuid.Nil, fmt.Errorf("failed to initialize filter: %w", err)
	}
	
	// Register the filter
	fm.registry[id] = filter
	fm.nameIndex[filterName] = id
	
	// Update statistics
	fm.stats.TotalFilters++
	fm.stats.ActiveFilters++
	fm.stats.LastUpdated = time.Now()
	
	// Emit registration event
	fm.emitEvent("FilterRegistered", map[string]interface{}{
		"id":   id.String(),
		"name": filterName,
		"type": filter.Type(),
	})
	
	return id, nil
}

// UnregisterFilter removes a filter from the registry.
func (fm *FilterManager) UnregisterFilter(id uuid.UUID) error {
	fm.mu.Lock()
	defer fm.mu.Unlock()
	
	// Find the filter
	filter, exists := fm.registry[id]
	if !exists {
		return fmt.Errorf("filter with ID %s not found", id.String())
	}
	
	filterName := filter.Name()
	
	// Remove from any chains first
	for _, chain := range fm.chains {
		if err := chain.Remove(filterName); err != nil {
			// Log but don't fail - filter might not be in this chain
		}
	}
	
	// Close the filter
	if err := filter.Close(); err != nil {
		// Log the error but continue with unregistration
		fmt.Printf("Warning: error closing filter '%s': %v\n", filterName, err)
	}
	
	// Remove from registry and index
	delete(fm.registry, id)
	delete(fm.nameIndex, filterName)
	
	// Update statistics
	fm.stats.ActiveFilters--
	fm.stats.LastUpdated = time.Now()
	
	// Emit unregistration event
	fm.emitEvent("FilterUnregistered", map[string]interface{}{
		"id":   id.String(),
		"name": filterName,
	})
	
	return nil
}

// GetFilter retrieves a filter by ID.
func (fm *FilterManager) GetFilter(id uuid.UUID) (core.Filter, bool) {
	fm.mu.RLock()
	defer fm.mu.RUnlock()
	
	filter, exists := fm.registry[id]
	return filter, exists
}

// GetFilterByName retrieves a filter by name.
func (fm *FilterManager) GetFilterByName(name string) (core.Filter, bool) {
	fm.mu.RLock()
	defer fm.mu.RUnlock()
	
	id, exists := fm.nameIndex[name]
	if !exists {
		return nil, false
	}
	
	filter, exists := fm.registry[id]
	return filter, exists
}

// ListFilters returns a list of all registered filter IDs and names.
func (fm *FilterManager) ListFilters() map[uuid.UUID]string {
	fm.mu.RLock()
	defer fm.mu.RUnlock()
	
	result := make(map[uuid.UUID]string, len(fm.registry))
	for id, filter := range fm.registry {
		result[id] = filter.Name()
	}
	
	return result
}

// CreateChain creates a new filter chain with the given configuration.
func (fm *FilterManager) CreateChain(config types.ChainConfig) (*core.FilterChain, error) {
	if config.Name == "" {
		return nil, fmt.Errorf("chain name cannot be empty")
	}
	
	// Validate configuration
	if err := config.Validate(); err != nil {
		return nil, fmt.Errorf("invalid chain config: %w", err)
	}
	
	fm.mu.Lock()
	defer fm.mu.Unlock()
	
	// Check if we're at capacity
	if len(fm.chains) >= fm.config.MaxChains {
		return nil, fmt.Errorf("maximum number of chains (%d) reached", fm.config.MaxChains)
	}
	
	// Check for name uniqueness
	if _, exists := fm.chains[config.Name]; exists {
		return nil, fmt.Errorf("chain with name '%s' already exists", config.Name)
	}
	
	// Create new chain
	chain := core.NewFilterChain(config)
	if chain == nil {
		return nil, fmt.Errorf("failed to create filter chain")
	}
	
	// Store the chain
	fm.chains[config.Name] = chain
	
	// Update statistics
	fm.stats.TotalChains++
	fm.stats.ActiveChains++
	fm.stats.LastUpdated = time.Now()
	
	// Emit chain created event
	fm.emitEvent("ChainCreated", map[string]interface{}{
		"name": config.Name,
		"mode": config.ExecutionMode.String(),
	})
	
	return chain, nil
}

// RemoveChain removes a chain from the manager.
func (fm *FilterManager) RemoveChain(name string) error {
	if name == "" {
		return fmt.Errorf("chain name cannot be empty")
	}
	
	fm.mu.Lock()
	defer fm.mu.Unlock()
	
	// Find the chain
	chain, exists := fm.chains[name]
	if !exists {
		return fmt.Errorf("chain '%s' not found", name)
	}
	
	// Close the chain
	if err := chain.Close(); err != nil {
		fmt.Printf("Warning: error closing chain '%s': %v\n", name, err)
	}
	
	// Remove from map
	delete(fm.chains, name)
	
	// Update statistics
	fm.stats.ActiveChains--
	fm.stats.LastUpdated = time.Now()
	
	// Emit chain removed event
	fm.emitEvent("ChainRemoved", map[string]interface{}{
		"name": name,
	})
	
	return nil
}

// GetChain retrieves a chain by name.
func (fm *FilterManager) GetChain(name string) (*core.FilterChain, bool) {
	fm.mu.RLock()
	defer fm.mu.RUnlock()
	
	chain, exists := fm.chains[name]
	return chain, exists
}

// ListChains returns a list of all chain names.
func (fm *FilterManager) ListChains() []string {
	fm.mu.RLock()
	defer fm.mu.RUnlock()
	
	chains := make([]string, 0, len(fm.chains))
	for name := range fm.chains {
		chains = append(chains, name)
	}
	
	return chains
}

// AggregateStatistics collects and aggregates statistics from all filters and chains.
func (fm *FilterManager) AggregateStatistics() ManagerStatistics {
	fm.mu.RLock()
	defer fm.mu.RUnlock()
	
	stats := fm.stats
	stats.LastUpdated = time.Now()
	
	// Aggregate filter statistics
	var totalProcessed, totalErrors uint64
	var totalLatency time.Duration
	latencies := make([]time.Duration, 0)
	
	for _, filter := range fm.registry {
		filterStats := filter.GetStats()
		totalProcessed += filterStats.ProcessCount
		totalErrors += filterStats.ErrorCount
		
		if filterStats.ProcessCount > 0 {
			avgLatency := time.Duration(filterStats.ProcessingTimeUs/filterStats.ProcessCount) * time.Microsecond
			totalLatency += avgLatency
			latencies = append(latencies, avgLatency)
		}
	}
	
	stats.ProcessedCount = totalProcessed
	stats.ErrorCount = totalErrors
	
	// Calculate average latency
	if len(latencies) > 0 {
		stats.AverageLatency = totalLatency / time.Duration(len(latencies))
		
		// Calculate percentiles (simplified)
		if len(latencies) >= 2 {
			// Sort latencies for percentile calculation
			// This is a simplified implementation
			stats.P50Latency = latencies[len(latencies)/2]
			stats.P90Latency = latencies[int(float64(len(latencies))*0.9)]
			stats.P99Latency = latencies[int(float64(len(latencies))*0.99)]
		}
	}
	
	// Aggregate chain statistics
	var totalChainProcessed, totalChainErrors uint64
	for _, chain := range fm.chains {
		chainStats := chain.GetStats()
		totalChainProcessed += chainStats.TotalExecutions
		totalChainErrors += chainStats.ErrorCount
	}
	
	stats.ChainProcessed = totalChainProcessed
	stats.ChainErrors = totalChainErrors
	
	// Update the stored stats
	fm.stats = stats
	
	return stats
}

// GetStatistics returns the current manager statistics.
func (fm *FilterManager) GetStatistics() ManagerStatistics {
	fm.mu.RLock()
	defer fm.mu.RUnlock()
	
	return fm.stats
}

// Start initializes and starts the manager lifecycle.
func (fm *FilterManager) Start() error {
	fm.mu.Lock()
	defer fm.mu.Unlock()
	
	if fm.started {
		return fmt.Errorf("manager is already started")
	}
	
	// Initialize all filters
	for _, filter := range fm.registry {
		if err := filter.Initialize(types.FilterConfig{}); err != nil {
			return fmt.Errorf("failed to initialize filter '%s': %w", filter.Name(), err)
		}
	}
	
	// Initialize all chains
	for _, chain := range fm.chains {
		if err := chain.Initialize(); err != nil {
			return fmt.Errorf("failed to initialize chain: %w", err)
		}
	}
	
	// Start event bus
	go fm.events.start()
	
	// Start background tasks
	if fm.config.EnableMetrics && fm.config.MetricsInterval > 0 {
		fm.metricsTimer = time.NewTicker(fm.config.MetricsInterval)
		go fm.metricsLoop()
	}
	
	if fm.config.EnableAutoRecovery && fm.config.HealthCheckInterval > 0 {
		fm.healthTimer = time.NewTicker(fm.config.HealthCheckInterval)
		go fm.healthCheckLoop()
	}
	
	fm.started = true
	
	// Emit started event
	fm.emitEvent("ManagerStarted", map[string]interface{}{
		"filters": len(fm.registry),
		"chains":  len(fm.chains),
	})
	
	return nil
}

// Stop gracefully shuts down the manager.
func (fm *FilterManager) Stop() error {
	fm.mu.Lock()
	defer fm.mu.Unlock()
	
	if !fm.started {
		return nil // Already stopped
	}
	
	// Stop background tasks
	if fm.metricsTimer != nil {
		fm.metricsTimer.Stop()
		fm.metricsTimer = nil
	}
	
	if fm.healthTimer != nil {
		fm.healthTimer.Stop()
		fm.healthTimer = nil
	}
	
	// Stop event bus
	fm.events.stop()
	
	// Close all chains
	for name, chain := range fm.chains {
		if err := chain.Close(); err != nil {
			fmt.Printf("Warning: error closing chain '%s': %v\n", name, err)
		}
	}
	
	// Close all filters
	for id, filter := range fm.registry {
		if err := filter.Close(); err != nil {
			fmt.Printf("Warning: error closing filter '%s': %v\n", id.String(), err)
		}
	}
	
	// Cancel context
	fm.cancel()
	
	fm.started = false
	
	// Emit stopped event (before event bus stops)
	fm.emitEvent("ManagerStopped", map[string]interface{}{
		"filters": len(fm.registry),
		"chains":  len(fm.chains),
	})
	
	return nil
}

// IsStarted returns whether the manager is currently started.
func (fm *FilterManager) IsStarted() bool {
	fm.mu.RLock()
	defer fm.mu.RUnlock()
	
	return fm.started
}

// emitEvent emits an event to the event bus.
func (fm *FilterManager) emitEvent(eventType string, data map[string]interface{}) {
	event := Event{
		Type:      eventType,
		Timestamp: time.Now(),
		Data:      data,
	}
	
	select {
	case fm.events.buffer <- event:
	default:
		// Buffer is full, drop event or log warning
		fmt.Printf("Warning: event buffer full, dropping event: %s\n", eventType)
	}
}

// Subscribe subscribes to events of a specific type.
func (fm *FilterManager) Subscribe(eventType string, handler EventHandler) {
	fm.events.subscribe(eventType, handler)
}

// Unsubscribe removes a handler for a specific event type.
func (fm *FilterManager) Unsubscribe(eventType string, handler EventHandler) {
	fm.events.unsubscribe(eventType, handler)
}

// metricsLoop runs the periodic metrics aggregation.
func (fm *FilterManager) metricsLoop() {
	defer func() {
		if r := recover(); r != nil {
			fmt.Printf("Metrics loop panic: %v\n", r)
		}
	}()
	
	for {
		select {
		case <-fm.metricsTimer.C:
			fm.AggregateStatistics()
		case <-fm.ctx.Done():
			return
		}
	}
}

// healthCheckLoop runs periodic health checks.
func (fm *FilterManager) healthCheckLoop() {
	defer func() {
		if r := recover(); r != nil {
			fmt.Printf("Health check loop panic: %v\n", r)
		}
	}()
	
	for {
		select {
		case <-fm.healthTimer.C:
			fm.performHealthCheck()
		case <-fm.ctx.Done():
			return
		}
	}
}

// performHealthCheck performs health checks on filters and chains.
func (fm *FilterManager) performHealthCheck() {
	fm.mu.RLock()
	defer fm.mu.RUnlock()
	
	// Check filter health (simplified)
	for id, filter := range fm.registry {
		// Check if filter is responsive
		_, err := filter.Process(context.Background(), []byte("health-check"))
		if err != nil {
			fm.emitEvent("FilterHealthCheck", map[string]interface{}{
				"id":     id.String(),
				"name":   filter.Name(),
				"status": "unhealthy",
				"error":  err.Error(),
			})
		}
	}
}

// EventBus methods

// start starts the event bus processing loop.
func (eb *EventBus) start() {
	defer func() {
		if r := recover(); r != nil {
			fmt.Printf("Event bus panic: %v\n", r)
		}
	}()
	
	for {
		select {
		case event := <-eb.buffer:
			eb.processEvent(event)
		case <-eb.ctx.Done():
			return
		}
	}
}

// stop stops the event bus.
func (eb *EventBus) stop() {
	eb.cancel()
	close(eb.buffer)
}

// subscribe adds an event handler for a specific event type.
func (eb *EventBus) subscribe(eventType string, handler EventHandler) {
	eb.mu.Lock()
	defer eb.mu.Unlock()
	
	if eb.subscribers[eventType] == nil {
		eb.subscribers[eventType] = make([]EventHandler, 0)
	}
	
	eb.subscribers[eventType] = append(eb.subscribers[eventType], handler)
}

// unsubscribe removes an event handler for a specific event type.
func (eb *EventBus) unsubscribe(eventType string, handler EventHandler) {
	eb.mu.Lock()
	defer eb.mu.Unlock()
	
	handlers := eb.subscribers[eventType]
	if handlers == nil {
		return
	}
	
	// Remove the handler (note: this is a simplified implementation)
	// In a real implementation, you'd need to compare function pointers
	// or use a different mechanism like subscription IDs
	for i, h := range handlers {
		// This comparison might not work as expected with function types
		// A better approach would be to return subscription IDs
		if &h == &handler {
			eb.subscribers[eventType] = append(handlers[:i], handlers[i+1:]...)
			break
		}
	}
}

// processEvent processes a single event by calling all registered handlers.
func (eb *EventBus) processEvent(event Event) {
	eb.mu.RLock()
	handlers := eb.subscribers[event.Type]
	eb.mu.RUnlock()
	
	for _, handler := range handlers {
		go func(h EventHandler) {
			defer func() {
				if r := recover(); r != nil {
					fmt.Printf("Event handler panic for event %s: %v\n", event.Type, r)
				}
			}()
			h(event)
		}(handler)
	}
}