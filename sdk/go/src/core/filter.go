// Package core provides the core interfaces and types for the MCP Filter SDK.
// It defines the fundamental contracts that all filters must implement.
package core

import (
	"context"
	"io"
	"time"

	"github.com/GopherSecurity/gopher-mcp/src/types"
)

// Filter is the primary interface that all filters must implement.
// A filter processes data flowing through a filter chain, performing
// transformations, validations, or other operations on the data.
//
// Filters should be designed to be:
//   - Stateless when possible (state can be stored in context if needed)
//   - Reentrant and safe for concurrent use
//   - Efficient in memory usage and processing time
//   - Composable with other filters in a chain
//
// Example implementation:
//
//	type LoggingFilter struct {
//	    logger *log.Logger
//	}
//
//	func (f *LoggingFilter) Process(ctx context.Context, data []byte) (*types.FilterResult, error) {
//	    f.logger.Printf("Processing %d bytes", len(data))
//	    return types.ContinueWith(data), nil
//	}
type Filter interface {
	// Process is the primary method that performs the filter's operation on the input data.
	// It receives a context for cancellation and deadline support, and the data to process.
	//
	// The method should:
	//   - Process the input data according to the filter's logic
	//   - Return a FilterResult indicating the processing outcome
	//   - Return an error if processing fails
	//
	// The context may contain:
	//   - Cancellation signals that should be respected
	//   - Deadlines that should be enforced
	//   - Request-scoped values for maintaining state
	//   - Metadata about the filter chain and execution
	//
	// Parameters:
	//   - ctx: The context for this processing operation
	//   - data: The input data to be processed
	//
	// Returns:
	//   - *types.FilterResult: The result of processing, including status and output data
	//   - error: Any error that occurred during processing
	//
	// Example:
	//
	//	func (f *MyFilter) Process(ctx context.Context, data []byte) (*types.FilterResult, error) {
	//	    // Check for cancellation
	//	    select {
	//	    case <-ctx.Done():
	//	        return nil, ctx.Err()
	//	    default:
	//	    }
	//
	//	    // Process the data
	//	    processed := f.transform(data)
	//
	//	    // Return the result
	//	    return types.ContinueWith(processed), nil
	//	}
	Process(ctx context.Context, data []byte) (*types.FilterResult, error)

	// Initialize sets up the filter with the provided configuration.
	// This method is called once before the filter starts processing data.
	//
	// The method should:
	//   - Validate the configuration parameters
	//   - Allocate any required resources
	//   - Set up internal state based on the configuration
	//   - Return an error if initialization fails
	//
	// Configuration validation should check:
	//   - Required parameters are present
	//   - Values are within acceptable ranges
	//   - Dependencies are available
	//   - Resource limits are respected
	//
	// Parameters:
	//   - config: The configuration to apply to this filter
	//
	// Returns:
	//   - error: Any error that occurred during initialization
	//
	// Example:
	//
	//	func (f *MyFilter) Initialize(config types.FilterConfig) error {
	//	    // Validate configuration
	//	    if errs := config.Validate(); len(errs) > 0 {
	//	        return fmt.Errorf("invalid configuration: %v", errs)
	//	    }
	//
	//	    // Extract filter-specific settings
	//	    if threshold, ok := config.Settings["threshold"].(int); ok {
	//	        f.threshold = threshold
	//	    }
	//
	//	    // Allocate resources
	//	    f.buffer = make([]byte, config.MaxBufferSize)
	//
	//	    return nil
	//	}
	Initialize(config types.FilterConfig) error

	// Close performs cleanup operations when the filter is no longer needed.
	// This method is called when the filter is being removed from a chain or
	// when the chain is shutting down.
	//
	// The method should:
	//   - Release any allocated resources
	//   - Close open connections or file handles
	//   - Flush any buffered data
	//   - Cancel any background operations
	//   - Return an error if cleanup fails
	//
	// Close should be idempotent - calling it multiple times should be safe.
	// After Close is called, the filter should not process any more data.
	//
	// Returns:
	//   - error: Any error that occurred during cleanup
	//
	// Example:
	//
	//	func (f *MyFilter) Close() error {
	//	    // Stop background workers
	//	    if f.done != nil {
	//	        close(f.done)
	//	    }
	//
	//	    // Flush buffered data
	//	    if f.buffer != nil {
	//	        if err := f.flush(); err != nil {
	//	            return fmt.Errorf("failed to flush buffer: %w", err)
	//	        }
	//	    }
	//
	//	    // Close connections
	//	    if f.conn != nil {
	//	        if err := f.conn.Close(); err != nil {
	//	            return fmt.Errorf("failed to close connection: %w", err)
	//	        }
	//	    }
	//
	//	    return nil
	//	}
	Close() error

	// Name returns the unique name of this filter instance within a chain.
	// The name is used for identification, logging, and referencing the filter
	// in configuration and management operations.
	//
	// Names should be:
	//   - Unique within a filter chain
	//   - Descriptive of the filter's purpose
	//   - Valid as identifiers (alphanumeric, hyphens, underscores)
	//   - Consistent across restarts
	//
	// Returns:
	//   - string: The unique name of this filter instance
	//
	// Example:
	//
	//	func (f *MyFilter) Name() string {
	//	    return f.config.Name
	//	}
	Name() string

	// Type returns the category or type of this filter.
	// The type is used for organizing filters, collecting metrics by category,
	// and understanding the filter's role in the processing pipeline.
	//
	// Common filter types include:
	//   - "security": Authentication, authorization, validation filters
	//   - "transformation": Data format conversion, encoding/decoding filters
	//   - "monitoring": Logging, metrics, tracing filters
	//   - "routing": Load balancing, path-based routing filters
	//   - "caching": Response caching, memoization filters
	//   - "compression": Data compression/decompression filters
	//   - "rate-limiting": Request throttling, quota management filters
	//
	// Returns:
	//   - string: The type category of this filter
	//
	// Example:
	//
	//	func (f *AuthenticationFilter) Type() string {
	//	    return "security"
	//	}
	Type() string

	// GetStats returns the current performance statistics for this filter.
	// Statistics are used for monitoring, debugging, and optimization of
	// filter performance within the chain.
	//
	// The returned statistics should include:
	//   - Number of bytes/packets processed
	//   - Processing times (average, min, max)
	//   - Error counts and types
	//   - Resource usage metrics
	//   - Throughput measurements
	//
	// Statistics should be collected efficiently to minimize performance impact.
	// Consider using atomic operations or periodic snapshots for high-throughput filters.
	//
	// Returns:
	//   - types.FilterStatistics: Current performance metrics for this filter
	//
	// Example:
	//
	//	func (f *MyFilter) GetStats() types.FilterStatistics {
	//	    f.statsLock.RLock()
	//	    defer f.statsLock.RUnlock()
	//	    return f.stats
	//	}
	GetStats() types.FilterStatistics
}

// LifecycleFilter extends Filter with lifecycle management capabilities.
// Filters implementing this interface can respond to attachment/detachment
// from chains and start/stop events.
type LifecycleFilter interface {
	Filter

	// OnAttach is called when the filter is attached to a filter chain.
	// This allows the filter to access chain properties and coordinate with other filters.
	//
	// Parameters:
	//   - chain: The filter chain this filter is being attached to
	//
	// Returns:
	//   - error: Any error preventing attachment
	OnAttach(chain *FilterChain) error

	// OnDetach is called when the filter is being removed from a chain.
	// The filter should clean up any chain-specific resources.
	//
	// Returns:
	//   - error: Any error during detachment
	OnDetach() error

	// OnStart is called when the filter chain starts processing.
	// Filters can use this to initialize runtime state or start background tasks.
	//
	// Parameters:
	//   - ctx: Context for the start operation
	//
	// Returns:
	//   - error: Any error preventing the filter from starting
	OnStart(ctx context.Context) error

	// OnStop is called when the filter chain stops processing.
	// Filters should stop background tasks and prepare for shutdown.
	//
	// Parameters:
	//   - ctx: Context for the stop operation
	//
	// Returns:
	//   - error: Any error during stopping
	OnStop(ctx context.Context) error
}

// StatefulFilter interface for filters that maintain state.
// Filters implementing this interface can save and restore their state,
// which is useful for persistence, migration, or debugging.
type StatefulFilter interface {
	Filter

	// SaveState serializes the filter's current state to a writer.
	// The state should be in a format that can be restored later.
	//
	// Parameters:
	//   - w: The writer to save state to
	//
	// Returns:
	//   - error: Any error during state serialization
	SaveState(w io.Writer) error

	// LoadState deserializes and restores filter state from a reader.
	// The filter should validate the loaded state before applying it.
	//
	// Parameters:
	//   - r: The reader to load state from
	//
	// Returns:
	//   - error: Any error during state deserialization
	LoadState(r io.Reader) error

	// GetState returns the filter's current state as an interface.
	// The returned value should be safe for concurrent access.
	//
	// Returns:
	//   - interface{}: The current filter state
	GetState() interface{}

	// ResetState clears the filter's state to its initial condition.
	// This is useful for testing or when the filter needs a fresh start.
	//
	// Returns:
	//   - error: Any error during state reset
	ResetState() error
}

// ConfigurableFilter interface for runtime reconfiguration support.
// Filters implementing this interface can be reconfigured without restart.
type ConfigurableFilter interface {
	Filter

	// UpdateConfig applies a new configuration to the running filter.
	// The filter should validate and apply the config atomically.
	//
	// Parameters:
	//   - config: The new configuration to apply
	//
	// Returns:
	//   - error: Any error during configuration update
	UpdateConfig(config types.FilterConfig) error

	// ValidateConfig checks if a configuration is valid without applying it.
	// This allows pre-validation before attempting updates.
	//
	// Parameters:
	//   - config: The configuration to validate
	//
	// Returns:
	//   - error: Any validation errors found
	ValidateConfig(config types.FilterConfig) error

	// GetConfigVersion returns the current configuration version.
	// Useful for tracking configuration changes and debugging.
	//
	// Returns:
	//   - string: The current configuration version identifier
	GetConfigVersion() string
}

// FilterMetrics contains detailed performance and operational metrics.
type FilterMetrics struct {
	// Request metrics
	RequestsTotal    int64
	RequestsPerSec   float64
	RequestLatencyMs float64
	
	// Error metrics
	ErrorsTotal int64
	ErrorRate   float64
	
	// Resource metrics
	MemoryUsageBytes int64
	CPUUsagePercent  float64
	GoroutineCount   int
	
	// Custom metrics
	CustomMetrics map[string]interface{}
}

// HealthStatus represents the health state of a filter.
type HealthStatus struct {
	Healthy bool
	Status  string // "healthy", "degraded", "unhealthy"
	Message string
	Details map[string]interface{}
}

// ObservableFilter interface for monitoring integration.
// Filters implementing this interface provide detailed metrics and health information.
type ObservableFilter interface {
	Filter

	// GetMetrics returns current filter performance metrics.
	// Used for monitoring dashboards and alerting.
	//
	// Returns:
	//   - FilterMetrics: Current performance and operational metrics
	GetMetrics() FilterMetrics

	// GetHealthStatus returns the current health state of the filter.
	// Used for health checks and circuit breaking.
	//
	// Returns:
	//   - HealthStatus: Current health state and details
	GetHealthStatus() HealthStatus

	// GetTraceSpan returns the current trace span for distributed tracing.
	// Used for request tracing and performance analysis.
	//
	// Returns:
	//   - interface{}: Current trace span (implementation-specific)
	GetTraceSpan() interface{}
}

// FilterHook represents a hook function that can modify filter behavior.
type FilterHook func(ctx context.Context, data []byte) ([]byte, error)

// HookableFilter interface for extending filter behavior with hooks.
// Filters implementing this interface allow dynamic behavior modification.
type HookableFilter interface {
	Filter

	// AddPreHook adds a hook to be executed before filter processing.
	// Multiple pre-hooks are executed in the order they were added.
	//
	// Parameters:
	//   - hook: The hook function to add
	//
	// Returns:
	//   - string: Hook ID for later removal
	AddPreHook(hook FilterHook) string

	// AddPostHook adds a hook to be executed after filter processing.
	// Multiple post-hooks are executed in the order they were added.
	//
	// Parameters:
	//   - hook: The hook function to add
	//
	// Returns:
	//   - string: Hook ID for later removal
	AddPostHook(hook FilterHook) string

	// RemoveHook removes a previously added hook by its ID.
	//
	// Parameters:
	//   - id: The hook ID to remove
	//
	// Returns:
	//   - error: Error if hook not found
	RemoveHook(id string) error
}

// BatchFilter interface for batch processing support.
// Filters implementing this interface can process multiple items efficiently.
type BatchFilter interface {
	Filter

	// ProcessBatch processes multiple data items in a single operation.
	// More efficient than processing items individually.
	//
	// Parameters:
	//   - ctx: Context for the batch operation
	//   - batch: Array of data items to process
	//
	// Returns:
	//   - []*FilterResult: Results for each batch item
	//   - error: Any error during batch processing
	ProcessBatch(ctx context.Context, batch [][]byte) ([]*types.FilterResult, error)

	// SetBatchSize configures the preferred batch size.
	// The filter may adjust this based on resource constraints.
	//
	// Parameters:
	//   - size: Preferred number of items per batch
	SetBatchSize(size int)

	// SetBatchTimeout sets the maximum time to wait for a full batch.
	// After timeout, partial batches are processed.
	//
	// Parameters:
	//   - timeout: Maximum wait time for batch accumulation
	SetBatchTimeout(timeout time.Duration)
}

// Cache represents a generic cache interface.
type Cache interface {
	Get(key string) (interface{}, bool)
	Set(key string, value interface{}, ttl time.Duration) error
	Delete(key string) error
	Clear() error
}

// CachingFilter interface for filters with caching capabilities.
// Filters implementing this interface can cache processed results.
type CachingFilter interface {
	Filter

	// GetCache returns the current cache instance.
	// Returns nil if no cache is configured.
	//
	// Returns:
	//   - Cache: The current cache instance
	GetCache() Cache

	// SetCache configures the cache to use.
	// Pass nil to disable caching.
	//
	// Parameters:
	//   - cache: The cache instance to use
	SetCache(cache Cache)

	// InvalidateCache removes a specific cache entry.
	// Used when cached data becomes stale.
	//
	// Parameters:
	//   - key: The cache key to invalidate
	//
	// Returns:
	//   - error: Any error during invalidation
	InvalidateCache(key string) error

	// PreloadCache warms up the cache with frequently used data.
	// Called during initialization or quiet periods.
	//
	// Parameters:
	//   - ctx: Context for the preload operation
	//
	// Returns:
	//   - error: Any error during cache preloading
	PreloadCache(ctx context.Context) error
}