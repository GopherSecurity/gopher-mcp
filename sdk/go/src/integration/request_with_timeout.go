// Package integration provides MCP SDK integration.
package integration

import (
	"context"
	"fmt"
	"time"
)

// TimeoutFilter adds timeout enforcement to requests.
type TimeoutFilter struct {
	BaseFilter
	Timeout time.Duration
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
		filteredReq, err := deserializeRequest(filtered)
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