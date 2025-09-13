// Package manager provides filter and chain management for the MCP Filter SDK.
package manager

// MessageProcessor processes messages through filter chains.
type MessageProcessor struct {
	manager      *FilterManager
	router       Router
	aggregator   Aggregator
	errorHandler ErrorHandler
	config       ProcessorConfig
}

// Router routes messages to chains.
type Router interface {
	Route(message []byte) (string, error)
}

// Aggregator aggregates responses.
type Aggregator interface {
	Aggregate(responses [][]byte) ([]byte, error)
}

// ErrorHandler handles processing errors.
type ErrorHandler interface {
	HandleError(err error) error
}

// ProcessorConfig configures message processor.
type ProcessorConfig struct {
	EnableRouting     bool
	EnableAggregation bool
	EnableMonitoring  bool
	BatchSize         int
	AsyncProcessing   bool
}

// NewMessageProcessor creates a new message processor.
func NewMessageProcessor(manager *FilterManager, config ProcessorConfig) *MessageProcessor {
	return &MessageProcessor{
		manager: manager,
		config:  config,
	}
}
