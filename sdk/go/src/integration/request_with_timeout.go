// Package integration provides MCP SDK integration.
package integration

import (
	"context"
	"fmt"
	"time"
)

// TimeoutFilter adds timeout enforcement to requests.
type TimeoutFilter struct {
	Timeout time.Duration
	id      string
	name    string
}

// GetID returns the filter ID.
func (tf *TimeoutFilter) GetID() string {
	if tf.id == "" {
		return "timeout_filter"
	}
	return tf.id
}

// GetName returns the filter name.
func (tf *TimeoutFilter) GetName() string {
	if tf.name == "" {
		return "TimeoutFilter"
	}
	return tf.name
}

// GetType returns the filter type.
func (tf *TimeoutFilter) GetType() string {
	return "timeout"
}

// GetVersion returns the filter version.
func (tf *TimeoutFilter) GetVersion() string {
	return "1.0.0"
}

// GetDescription returns the filter description.
func (tf *TimeoutFilter) GetDescription() string {
	return "Enforces timeout on requests"
}

// ValidateConfig validates the filter configuration.
func (tf *TimeoutFilter) ValidateConfig() error {
	if tf.Timeout <= 0 {
		return fmt.Errorf("timeout must be positive")
	}
	return nil
}

// GetConfiguration returns the filter configuration.
func (tf *TimeoutFilter) GetConfiguration() map[string]interface{} {
	return map[string]interface{}{
		"timeout": tf.Timeout,
	}
}

// UpdateConfig updates the filter configuration.
func (tf *TimeoutFilter) UpdateConfig(config map[string]interface{}) {
	if timeout, ok := config["timeout"].(time.Duration); ok {
		tf.Timeout = timeout
	}
}

// GetCapabilities returns the filter capabilities.
func (tf *TimeoutFilter) GetCapabilities() []string {
	return []string{"timeout", "deadline"}
}

// GetDependencies returns the filter dependencies.
func (tf *TimeoutFilter) GetDependencies() []FilterDependency {
	return nil
}

// GetResourceRequirements returns resource requirements.
func (tf *TimeoutFilter) GetResourceRequirements() ResourceRequirements {
	return ResourceRequirements{}
}

// GetTypeInfo returns type information.
func (tf *TimeoutFilter) GetTypeInfo() TypeInfo {
	return TypeInfo{
		InputTypes:  []string{"any"},
		OutputTypes: []string{"any"},
	}
}

// EstimateLatency estimates the filter latency.
func (tf *TimeoutFilter) EstimateLatency() time.Duration {
	return 0
}

// HasBlockingOperations returns if filter has blocking operations.
func (tf *TimeoutFilter) HasBlockingOperations() bool {
	return false
}

// UsesDeprecatedFeatures returns if filter uses deprecated features.
func (tf *TimeoutFilter) UsesDeprecatedFeatures() bool {
	return false
}

// HasKnownVulnerabilities returns if filter has known vulnerabilities.
func (tf *TimeoutFilter) HasKnownVulnerabilities() bool {
	return false
}

// IsStateless returns if filter is stateless.
func (tf *TimeoutFilter) IsStateless() bool {
	return true
}

// Clone creates a copy of the filter.
func (tf *TimeoutFilter) Clone() Filter {
	return &TimeoutFilter{
		Timeout: tf.Timeout,
		id:      tf.id + "_clone",
		name:    tf.name + "_clone",
	}
}

// SetID sets the filter ID.
func (tf *TimeoutFilter) SetID(id string) {
	tf.id = id
}

// RequestWithTimeout sends request with timeout.
func (fc *FilteredMCPClient) RequestWithTimeout(
	ctx context.Context,
	request interface{},
	timeout time.Duration,
) (interface{}, error) {
	// Create timeout context
	timeoutCtx, cancel := context.WithTimeout(ctx, timeout)
	defer cancel()
	
	// Create timeout filter
	timeoutFilter := &TimeoutFilter{
		Timeout: timeout,
	}
	
	// Create temporary chain with timeout filter
	tempChain := NewFilterChain()
	tempChain.Add(timeoutFilter)
	
	// Combine with existing request chain
	combinedChain := fc.combineChains(fc.requestChain, tempChain)
	
	// Channel for result
	type result struct {
		response interface{}
		err      error
	}
	resultChan := make(chan result, 1)
	
	// Execute request in goroutine
	go func() {
		// Apply filters
		reqData, err := serializeRequest(request)
		if err != nil {
			resultChan <- result{nil, fmt.Errorf("serialize error: %w", err)}
			return
		}
		
		filtered, err := combinedChain.Process(reqData)
		if err != nil {
			resultChan <- result{nil, fmt.Errorf("filter error: %w", err)}
			return
		}
		
		// Deserialize filtered request
		_, err = deserializeRequest(filtered)
		if err != nil {
			resultChan <- result{nil, fmt.Errorf("deserialize error: %w", err)}
			return
		}
		
		// Send request through MCP client
		// response, err := fc.MCPClient.SendRequest(filteredReq)
		// Simulate request
		response := map[string]interface{}{
			"result": "timeout_test",
			"status": "success",
		}
		
		// Apply response filters
		respData, err := serializeResponse(response)
		if err != nil {
			resultChan <- result{nil, fmt.Errorf("response serialize error: %w", err)}
			return
		}
		
		filteredResp, err := fc.responseChain.Process(respData)
		if err != nil {
			resultChan <- result{nil, fmt.Errorf("response filter error: %w", err)}
			return
		}
		
		// Deserialize response
		finalResp, err := deserializeResponse(filteredResp)
		if err != nil {
			resultChan <- result{nil, fmt.Errorf("response deserialize error: %w", err)}
			return
		}
		
		resultChan <- result{finalResp, nil}
	}()
	
	// Wait for result or timeout
	select {
	case <-timeoutCtx.Done():
		// Timeout occurred
		return nil, fmt.Errorf("request timeout after %v", timeout)
		
	case res := <-resultChan:
		return res.response, res.err
	}
}

// Process implements timeout filtering.
func (tf *TimeoutFilter) Process(data []byte) ([]byte, error) {
	// Add timeout metadata to request
	// In real implementation, would modify request headers or metadata
	return data, nil
}

// RequestWithRetry sends request with retry logic.
func (fc *FilteredMCPClient) RequestWithRetry(
	ctx context.Context,
	request interface{},
	maxRetries int,
	backoff time.Duration,
) (interface{}, error) {
	var lastErr error
	
	for attempt := 0; attempt <= maxRetries; attempt++ {
		// Add retry metadata
		reqWithRetry := addRetryMetadata(request, attempt)
		
		// Try request with timeout
		response, err := fc.RequestWithTimeout(ctx, reqWithRetry, 30*time.Second)
		if err == nil {
			return response, nil
		}
		
		lastErr = err
		
		// Check if retryable
		if !isRetryableError(err) {
			return nil, err
		}
		
		// Don't sleep on last attempt
		if attempt < maxRetries {
			// Calculate backoff with jitter
			sleepTime := calculateBackoff(backoff, attempt)
			
			select {
			case <-ctx.Done():
				return nil, ctx.Err()
			case <-time.After(sleepTime):
				// Continue to next retry
			}
		}
	}
	
	return nil, fmt.Errorf("max retries exceeded: %w", lastErr)
}

// addRetryMetadata adds retry information to request.
func addRetryMetadata(request interface{}, attempt int) interface{} {
	// In real implementation, would add retry headers or metadata
	if reqMap, ok := request.(map[string]interface{}); ok {
		reqMap["retry_attempt"] = attempt
		return reqMap
	}
	return request
}

// isRetryableError checks if error is retryable.
func isRetryableError(err error) bool {
	// Check for network errors, timeouts, 5xx errors
	errStr := err.Error()
	return errStr == "timeout" || 
	       errStr == "connection refused" ||
	       errStr == "temporary failure"
}

// calculateBackoff calculates exponential backoff with jitter.
func calculateBackoff(base time.Duration, attempt int) time.Duration {
	// Exponential backoff: base * 2^attempt
	backoff := base * time.Duration(1<<uint(attempt))
	
	// Add jitter (±25%)
	jitter := time.Duration(float64(backoff) * 0.25)
	
	return backoff + jitter
}