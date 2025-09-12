// Package integration provides MCP SDK integration.
package integration

import (
	"context"
	"fmt"
	"sync"
	"time"
)

// BatchRequest represents a single request in a batch.
type BatchRequest struct {
	ID      string
	Request interface{}
	Filters []Filter
}

// BatchResponse represents a single response in a batch.
type BatchResponse struct {
	ID       string
	Response interface{}
	Error    error
}

// BatchResult contains all batch responses.
type BatchResult struct {
	Responses map[string]*BatchResponse
	Duration  time.Duration
	mu        sync.RWMutex
}

// BatchRequestsWithFilters executes multiple requests in batch.
func (fc *FilteredMCPClient) BatchRequestsWithFilters(
	ctx context.Context,
	requests []BatchRequest,
	batchFilters ...Filter,
) (*BatchResult, error) {
	startTime := time.Now()
	
	// Create batch-level filter chain
	batchChain := NewFilterChain()
	for _, filter := range batchFilters {
		batchChain.Add(filter)
	}
	
	// Result container
	result := &BatchResult{
		Responses: make(map[string]*BatchResponse),
	}
	
	// Process requests concurrently
	var wg sync.WaitGroup
	semaphore := make(chan struct{}, fc.getBatchConcurrency())
	
	for _, req := range requests {
		wg.Add(1)
		
		// Acquire semaphore
		semaphore <- struct{}{}
		
		go func(br BatchRequest) {
			defer wg.Done()
			defer func() { <-semaphore }()
			
			// Create combined filter chain
			reqChain := fc.combineChains(batchChain, fc.requestChain)
			
			// Add request-specific filters
			if len(br.Filters) > 0 {
				tempChain := NewFilterChain()
				for _, filter := range br.Filters {
					tempChain.Add(filter)
				}
				reqChain = fc.combineChains(reqChain, tempChain)
			}
			
			// Process request
			response, err := fc.processBatchRequest(ctx, br, reqChain)
			
			// Store result
			result.mu.Lock()
			result.Responses[br.ID] = &BatchResponse{
				ID:       br.ID,
				Response: response,
				Error:    err,
			}
			result.mu.Unlock()
		}(req)
	}
	
	// Wait for all requests
	wg.Wait()
	
	// Set duration
	result.Duration = time.Since(startTime)
	
	// Check for any errors
	var hasErrors bool
	for _, resp := range result.Responses {
		if resp.Error != nil {
			hasErrors = true
			break
		}
	}
	
	if hasErrors && fc.shouldFailFast() {
		return result, fmt.Errorf("batch execution had errors")
	}
	
	return result, nil
}

// processBatchRequest processes a single batch request.
func (fc *FilteredMCPClient) processBatchRequest(
	ctx context.Context,
	req BatchRequest,
	chain *FilterChain,
) (interface{}, error) {
	// Check context
	select {
	case <-ctx.Done():
		return nil, ctx.Err()
	default:
	}
	
	// Serialize request
	reqData, err := serializeRequest(req.Request)
	if err != nil {
		return nil, fmt.Errorf("serialize error: %w", err)
	}
	
	// Apply filters
	filtered, err := chain.Process(reqData)
	if err != nil {
		return nil, fmt.Errorf("filter error: %w", err)
	}
	
	// Deserialize filtered request
	filteredReq, err := deserializeRequest(filtered)
	if err != nil {
		return nil, fmt.Errorf("deserialize error: %w", err)
	}
	
	// Send request
	// response, err := fc.MCPClient.SendRequest(filteredReq)
	// Simulate response
	response := map[string]interface{}{
		"batch_id": req.ID,
		"result":   "batch_result",
	}
	
	// Apply response filters
	respData, err := serializeResponse(response)
	if err != nil {
		return nil, fmt.Errorf("response serialize error: %w", err)
	}
	
	filteredResp, err := fc.responseChain.Process(respData)
	if err != nil {
		return nil, fmt.Errorf("response filter error: %w", err)
	}
	
	// Deserialize response
	return deserializeResponse(filteredResp)
}

// getBatchConcurrency returns max concurrent batch requests.
func (fc *FilteredMCPClient) getBatchConcurrency() int {
	// Default to 10 concurrent requests
	if fc.config.BatchConcurrency > 0 {
		return fc.config.BatchConcurrency
	}
	return 10
}

// shouldFailFast checks if batch should fail on first error.
func (fc *FilteredMCPClient) shouldFailFast() bool {
	return fc.config.BatchFailFast
}

// Get retrieves a response by ID.
func (br *BatchResult) Get(id string) (*BatchResponse, bool) {
	br.mu.RLock()
	defer br.mu.RUnlock()
	
	resp, exists := br.Responses[id]
	return resp, exists
}

// Successful returns all successful responses.
func (br *BatchResult) Successful() []*BatchResponse {
	br.mu.RLock()
	defer br.mu.RUnlock()
	
	var successful []*BatchResponse
	for _, resp := range br.Responses {
		if resp.Error == nil {
			successful = append(successful, resp)
		}
	}
	return successful
}

// Failed returns all failed responses.
func (br *BatchResult) Failed() []*BatchResponse {
	br.mu.RLock()
	defer br.mu.RUnlock()
	
	var failed []*BatchResponse
	for _, resp := range br.Responses {
		if resp.Error != nil {
			failed = append(failed, resp)
		}
	}
	return failed
}

// SuccessRate returns the success rate of the batch.
func (br *BatchResult) SuccessRate() float64 {
	br.mu.RLock()
	defer br.mu.RUnlock()
	
	if len(br.Responses) == 0 {
		return 0
	}
	
	successCount := 0
	for _, resp := range br.Responses {
		if resp.Error == nil {
			successCount++
		}
	}
	
	return float64(successCount) / float64(len(br.Responses))
}