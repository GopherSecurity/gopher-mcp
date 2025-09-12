// Package core provides the core interfaces and types for the MCP Filter SDK.
// It defines the fundamental contracts that all filters must implement.
package core

import (
	"context"

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
}