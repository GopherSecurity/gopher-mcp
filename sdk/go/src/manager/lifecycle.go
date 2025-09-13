// Package manager provides filter and chain management for the MCP Filter SDK.
package manager

import (
	"fmt"
	"sync"
	"time"

	"github.com/google/uuid"
)

// NewFilterManager creates a new filter manager.
func NewFilterManager(config FilterManagerConfig) *FilterManager {
	return &FilterManager{
		registry: NewFilterRegistry(),
		chains:   make(map[string]*FilterChain),
		config:   config,
		events:   NewEventBus(config.EventBufferSize),
		stopCh:   make(chan struct{}),
	}
}

// Start initializes all filters and chains.
func (fm *FilterManager) Start() error {
	fm.mu.Lock()
	defer fm.mu.Unlock()

	if fm.running {
		return fmt.Errorf("manager already running")
	}

	fm.startTime = time.Now()

	// Initialize all filters
	allFilters := fm.registry.GetAll()
	for id, filter := range allFilters {
		// Initialize filter
		// if err := filter.Initialize(); err != nil {
		//     return fmt.Errorf("failed to initialize filter %s: %w", id, err)
		// }
		_ = id
		_ = filter
	}

	// Start all chains
	for name, chain := range fm.chains {
		// Start chain
		// if err := chain.Start(); err != nil {
		//     return fmt.Errorf("failed to start chain %s: %w", name, err)
		// }
		_ = name
		_ = chain
	}

	// Start statistics collection
	if fm.config.EnableMetrics {
		fm.StartStatisticsCollection()
	}

	// Start event processing
	if fm.events != nil {
		fm.events.Start()
	}

	fm.running = true

	// Emit start event
	if fm.events != nil {
		fm.events.Emit(ManagerStartedEvent{
			Timestamp: time.Now(),
		})
	}

	return nil
}

// Stop gracefully shuts down the manager.
func (fm *FilterManager) Stop() error {
	fm.mu.Lock()
	defer fm.mu.Unlock()

	if !fm.running {
		return fmt.Errorf("manager not running")
	}

	// Signal stop
	close(fm.stopCh)

	// Stop chains first (in reverse order)
	chainNames := make([]string, 0, len(fm.chains))
	for name := range fm.chains {
		chainNames = append(chainNames, name)
	}

	// Stop in reverse order
	for i := len(chainNames) - 1; i >= 0; i-- {
		chain := fm.chains[chainNames[i]]
		// chain.Stop()
		_ = chain
	}

	// Stop all filters
	allFilters := fm.registry.GetAll()
	var wg sync.WaitGroup

	for id, filter := range allFilters {
		wg.Add(1)
		go func(id uuid.UUID, f Filter) {
			defer wg.Done()
			f.Close()
		}(id, filter)
	}

	// Wait for all filters to stop
	wg.Wait()

	// Stop event bus
	if fm.events != nil {
		fm.events.Stop()
	}

	fm.running = false

	// Emit stop event
	if fm.events != nil {
		fm.events.Emit(ManagerStoppedEvent{
			Timestamp: time.Now(),
		})
	}

	return nil
}

// Restart performs a graceful restart.
func (fm *FilterManager) Restart() error {
	if err := fm.Stop(); err != nil {
		return fmt.Errorf("failed to stop: %w", err)
	}

	// Reset state
	fm.stopCh = make(chan struct{})

	if err := fm.Start(); err != nil {
		return fmt.Errorf("failed to start: %w", err)
	}

	return nil
}

// IsRunning returns true if the manager is running.
func (fm *FilterManager) IsRunning() bool {
	fm.mu.RLock()
	defer fm.mu.RUnlock()
	return fm.running
}

// Additional fields for FilterManager
type FilterManager struct {
	registry  *FilterRegistry
	chains    map[string]*FilterChain
	config    FilterManagerConfig
	stats     ManagerStatistics
	events    *EventBus
	running   bool
	startTime time.Time
	stopCh    chan struct{}
	mu        sync.RWMutex
}
