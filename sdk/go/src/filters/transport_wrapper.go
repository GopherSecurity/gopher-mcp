package filters

import (
	"bytes"
	"encoding/json"
	"fmt"
	"io"
	"sync"
	"time"

	"github.com/GopherSecurity/gopher-mcp/src/integration"
)

// FilteredTransport wraps an MCP transport with filter chain capabilities.
type FilteredTransport struct {
	underlying     io.ReadWriteCloser
	inboundChain   *integration.FilterChain
	outboundChain  *integration.FilterChain
	mu             sync.RWMutex
	closed         bool
	stats          TransportStats
}

// TransportStats tracks transport statistics.
type TransportStats struct {
	MessagesIn     int64
	MessagesOut    int64
	BytesIn        int64
	BytesOut       int64
	Errors         int64
}

// NewFilteredTransport creates a new filtered transport.
func NewFilteredTransport(underlying io.ReadWriteCloser) *FilteredTransport {
	return &FilteredTransport{
		underlying:     underlying,
		inboundChain:   integration.NewFilterChain(),
		outboundChain:  integration.NewFilterChain(),
	}
}

// Read reads filtered data from the transport.
func (ft *FilteredTransport) Read(p []byte) (n int, err error) {
	ft.mu.RLock()
	if ft.closed {
		ft.mu.RUnlock()
		return 0, fmt.Errorf("transport is closed")
	}
	ft.mu.RUnlock()

	// Read from underlying transport
	n, err = ft.underlying.Read(p)
	if err != nil {
		ft.mu.Lock()
		ft.stats.Errors++
		ft.mu.Unlock()
		return n, err
	}

	// Apply inbound filters
	if n > 0 && ft.inboundChain.GetFilterCount() > 0 {
		data := make([]byte, n)
		copy(data, p[:n])
		
		filtered, err := ft.inboundChain.Process(data)
		if err != nil {
			ft.mu.Lock()
			ft.stats.Errors++
			ft.mu.Unlock()
			return 0, fmt.Errorf("inbound filter error: %w", err)
		}
		
		copy(p, filtered)
		n = len(filtered)
	}

	ft.mu.Lock()
	ft.stats.MessagesIn++
	ft.stats.BytesIn += int64(n)
	ft.mu.Unlock()

	return n, nil
}

// Write writes filtered data to the transport.
func (ft *FilteredTransport) Write(p []byte) (n int, err error) {
	ft.mu.RLock()
	if ft.closed {
		ft.mu.RUnlock()
		return 0, fmt.Errorf("transport is closed")
	}
	ft.mu.RUnlock()

	data := p
	
	// Apply outbound filters
	if ft.outboundChain.GetFilterCount() > 0 {
		filtered, err := ft.outboundChain.Process(data)
		if err != nil {
			ft.mu.Lock()
			ft.stats.Errors++
			ft.mu.Unlock()
			return 0, fmt.Errorf("outbound filter error: %w", err)
		}
		data = filtered
	}

	// Write to underlying transport
	n, err = ft.underlying.Write(data)
	if err != nil {
		ft.mu.Lock()
		ft.stats.Errors++
		ft.mu.Unlock()
		return n, err
	}

	ft.mu.Lock()
	ft.stats.MessagesOut++
	ft.stats.BytesOut += int64(n)
	ft.mu.Unlock()

	return len(p), nil // Return original length
}

// Close closes the transport.
func (ft *FilteredTransport) Close() error {
	ft.mu.Lock()
	defer ft.mu.Unlock()
	
	if ft.closed {
		return nil
	}
	
	ft.closed = true
	return ft.underlying.Close()
}

// AddInboundFilter adds a filter to the inbound chain.
func (ft *FilteredTransport) AddInboundFilter(filter integration.Filter) error {
	return ft.inboundChain.Add(filter)
}

// AddOutboundFilter adds a filter to the outbound chain.
func (ft *FilteredTransport) AddOutboundFilter(filter integration.Filter) error {
	return ft.outboundChain.Add(filter)
}

// GetStats returns transport statistics.
func (ft *FilteredTransport) GetStats() TransportStats {
	ft.mu.RLock()
	defer ft.mu.RUnlock()
	return ft.stats
}

// ResetStats resets transport statistics.
func (ft *FilteredTransport) ResetStats() {
	ft.mu.Lock()
	defer ft.mu.Unlock()
	ft.stats = TransportStats{}
}

// SetInboundChain sets the entire inbound filter chain.
func (ft *FilteredTransport) SetInboundChain(chain *integration.FilterChain) {
	ft.mu.Lock()
	defer ft.mu.Unlock()
	ft.inboundChain = chain
}

// SetOutboundChain sets the entire outbound filter chain.
func (ft *FilteredTransport) SetOutboundChain(chain *integration.FilterChain) {
	ft.mu.Lock()
	defer ft.mu.Unlock()
	ft.outboundChain = chain
}

// JSONRPCTransport wraps FilteredTransport for JSON-RPC message handling.
type JSONRPCTransport struct {
	*FilteredTransport
	decoder *json.Decoder
	encoder *json.Encoder
	readBuf bytes.Buffer
	writeBuf bytes.Buffer
}

// NewJSONRPCTransport creates a new JSON-RPC transport with filters.
func NewJSONRPCTransport(underlying io.ReadWriteCloser) *JSONRPCTransport {
	ft := NewFilteredTransport(underlying)
	return &JSONRPCTransport{
		FilteredTransport: ft,
		decoder:          json.NewDecoder(ft),
		encoder:          json.NewEncoder(ft),
	}
}

// ReadMessage reads a JSON-RPC message from the transport.
func (jt *JSONRPCTransport) ReadMessage() (json.RawMessage, error) {
	var msg json.RawMessage
	if err := jt.decoder.Decode(&msg); err != nil {
		return nil, err
	}
	return msg, nil
}

// WriteMessage writes a JSON-RPC message to the transport.
func (jt *JSONRPCTransport) WriteMessage(msg interface{}) error {
	return jt.encoder.Encode(msg)
}

// FilterAdapter adapts built-in filters to the Filter interface.
type FilterAdapter struct {
	filter interface{}
	id     string
	name   string
	typ    string
}

// NewFilterAdapter creates a new filter adapter.
func NewFilterAdapter(filter interface{}, name, typ string) *FilterAdapter {
	return &FilterAdapter{
		filter: filter,
		id:     fmt.Sprintf("%s-%p", typ, filter),
		name:   name,
		typ:    typ,
	}
}

// GetID returns the filter ID.
func (fa *FilterAdapter) GetID() string {
	return fa.id
}

// GetName returns the filter name.
func (fa *FilterAdapter) GetName() string {
	return fa.name
}

// GetType returns the filter type.
func (fa *FilterAdapter) GetType() string {
	return fa.typ
}

// GetVersion returns the filter version.
func (fa *FilterAdapter) GetVersion() string {
	return "1.0.0"
}

// GetDescription returns the filter description.
func (fa *FilterAdapter) GetDescription() string {
	switch f := fa.filter.(type) {
	case *CompressionFilter:
		return f.GetDescription()
	case *LoggingFilter:
		return f.GetDescription()
	case *ValidationFilter:
		return f.GetDescription()
	case *MetricsFilter:
		return "Metrics collection filter"
	default:
		return "Unknown filter"
	}
}

// Process processes data through the filter.
func (fa *FilterAdapter) Process(data []byte) ([]byte, error) {
	switch f := fa.filter.(type) {
	case *CompressionFilter:
		return f.Process(data)
	case *LoggingFilter:
		return f.Process(data)
	case *ValidationFilter:
		return f.Process(data)
	default:
		return nil, fmt.Errorf("unknown filter type")
	}
}

// ValidateConfig validates the filter configuration.
func (fa *FilterAdapter) ValidateConfig() error {
	return nil
}

// GetConfiguration returns the filter configuration.
func (fa *FilterAdapter) GetConfiguration() map[string]interface{} {
	return make(map[string]interface{})
}

// UpdateConfig updates the filter configuration.
func (fa *FilterAdapter) UpdateConfig(config map[string]interface{}) {
	// No-op for now
}

// GetCapabilities returns filter capabilities.
func (fa *FilterAdapter) GetCapabilities() []string {
	return []string{}
}

// GetDependencies returns filter dependencies.
func (fa *FilterAdapter) GetDependencies() []integration.FilterDependency {
	return []integration.FilterDependency{}
}

// GetResourceRequirements returns resource requirements.
func (fa *FilterAdapter) GetResourceRequirements() integration.ResourceRequirements {
	return integration.ResourceRequirements{}
}

// GetTypeInfo returns type information.
func (fa *FilterAdapter) GetTypeInfo() integration.TypeInfo {
	return integration.TypeInfo{}
}

// EstimateLatency estimates processing latency.
func (fa *FilterAdapter) EstimateLatency() time.Duration {
	switch f := fa.filter.(type) {
	case *CompressionFilter:
		return f.EstimateLatency()
	case *LoggingFilter:
		return f.EstimateLatency()
	case *ValidationFilter:
		return f.EstimateLatency()
	default:
		return 0
	}
}

// HasBlockingOperations returns whether the filter has blocking operations.
func (fa *FilterAdapter) HasBlockingOperations() bool {
	return false
}

// UsesDeprecatedFeatures returns whether the filter uses deprecated features.
func (fa *FilterAdapter) UsesDeprecatedFeatures() bool {
	switch f := fa.filter.(type) {
	case *CompressionFilter:
		return f.UsesDeprecatedFeatures()
	case *LoggingFilter:
		return f.UsesDeprecatedFeatures()
	case *ValidationFilter:
		return f.UsesDeprecatedFeatures()
	default:
		return false
	}
}

// HasKnownVulnerabilities returns whether the filter has known vulnerabilities.
func (fa *FilterAdapter) HasKnownVulnerabilities() bool {
	switch f := fa.filter.(type) {
	case *CompressionFilter:
		return f.HasKnownVulnerabilities()
	case *LoggingFilter:
		return f.HasKnownVulnerabilities()
	case *ValidationFilter:
		return f.HasKnownVulnerabilities()
	default:
		return false
	}
}

// IsStateless returns whether the filter is stateless.
func (fa *FilterAdapter) IsStateless() bool {
	switch f := fa.filter.(type) {
	case *CompressionFilter:
		return f.IsStateless()
	case *LoggingFilter:
		return f.IsStateless()
	case *ValidationFilter:
		return f.IsStateless()
	default:
		return true
	}
}

// Clone creates a copy of the filter.
func (fa *FilterAdapter) Clone() integration.Filter {
	return &FilterAdapter{
		filter: fa.filter,
		id:     fa.id,
		name:   fa.name,
		typ:    fa.typ,
	}
}

// SetID sets the filter ID.
func (fa *FilterAdapter) SetID(id string) {
	fa.id = id
}