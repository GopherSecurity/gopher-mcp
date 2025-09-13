// Package integration provides MCP SDK integration tests.
package integration

import (
	"context"
	"errors"
	"testing"
	"time"
)

// ErrInvalidData represents an invalid data error
var ErrInvalidData = errors.New("invalid data")

// TestFilteredMCPClient tests the FilteredMCPClient.
func TestFilteredMCPClient(t *testing.T) {
	t.Run("ClientCreation", testClientCreation)
	t.Run("FilterChains", testFilterChains)
	t.Run("RequestFiltering", testRequestFiltering)
	t.Run("ResponseFiltering", testResponseFiltering)
	t.Run("NotificationFiltering", testNotificationFiltering)
	t.Run("PerCallFilters", testPerCallFilters)
	t.Run("Subscriptions", testSubscriptions)
	t.Run("BatchRequests", testBatchRequests)
	t.Run("Timeouts", testTimeouts)
	t.Run("Metrics", testMetrics)
	t.Run("Validation", testValidation)
	t.Run("ChainCloning", testChainCloning)
	t.Run("DebugMode", testDebugMode)
}

func testClientCreation(t *testing.T) {
	// Test client creation
	client := NewFilteredMCPClient(ClientConfig{
		EnableFiltering: true,
		MaxChains:       10,
	})

	if client == nil {
		t.Fatal("Failed to create client")
	}

	// Verify initial state
	if client.config.EnableFiltering != true {
		t.Error("Filtering not enabled")
	}
}

func testFilterChains(t *testing.T) {
	client := NewFilteredMCPClient(ClientConfig{})

	// Create and set filter chains
	requestChain := NewFilterChain()
	responseChain := NewFilterChain()

	// Add test filters
	testFilter := &TestFilter{
		name: "test_filter",
		id:   "filter_1",
	}

	requestChain.Add(testFilter)
	responseChain.Add(testFilter)

	// Set chains
	client.SetClientRequestChain(requestChain)
	client.SetClientResponseChain(responseChain)

	// Verify chains are set
	if client.requestChain == nil {
		t.Error("Request chain not set")
	}
	if client.responseChain == nil {
		t.Error("Response chain not set")
	}
}

func testRequestFiltering(t *testing.T) {
	client := NewFilteredMCPClient(ClientConfig{})

	// Create request filter
	requestFilter := &TestFilter{
		name: "request_filter",
		processFunc: func(data []byte) ([]byte, error) {
			// Modify request
			return append(data, []byte("_filtered")...), nil
		},
	}

	// Set up chain
	chain := NewFilterChain()
	chain.Add(requestFilter)
	client.SetClientRequestChain(chain)

	// Test request filtering
	request := map[string]interface{}{
		"method": "test",
		"params": "data",
	}

	filtered, err := client.SendRequest(request)
	if err != nil {
		t.Errorf("Request failed: %v", err)
	}

	_ = filtered
}

func testResponseFiltering(t *testing.T) {
	client := NewFilteredMCPClient(ClientConfig{})

	// Create response filter
	responseFilter := &TestFilter{
		name: "response_filter",
		processFunc: func(data []byte) ([]byte, error) {
			// Validate response
			if len(data) == 0 {
				return nil, ErrInvalidData
			}
			return data, nil
		},
	}

	// Set up chain
	chain := NewFilterChain()
	chain.Add(responseFilter)
	client.SetClientResponseChain(chain)

	// Test response filtering
	response := map[string]interface{}{
		"result": "test_result",
	}

	filtered, err := client.ReceiveResponse(response)
	if err != nil {
		t.Errorf("Response filtering failed: %v", err)
	}

	_ = filtered
}

func testNotificationFiltering(t *testing.T) {
	client := NewFilteredMCPClient(ClientConfig{})

	// Create notification filter
	notifFilter := &TestFilter{
		name: "notification_filter",
		processFunc: func(data []byte) ([]byte, error) {
			// Filter notifications
			return data, nil
		},
	}

	// Set up chain
	chain := NewFilterChain()
	chain.Add(notifFilter)
	// Note: SetClientNotificationChain not implemented yet, using request chain for now
	client.SetClientRequestChain(chain)

	// Register handler
	handlerCalled := false
	handler := func(notif interface{}) error {
		handlerCalled = true
		return nil
	}

	_, err := client.HandleNotificationWithFilters("test_notif", handler)
	if err != nil {
		t.Errorf("Handler registration failed: %v", err)
	}

	// Trigger notification
	client.ProcessNotification("test_notif", map[string]interface{}{
		"data": "notification",
	})

	if !handlerCalled {
		t.Error("Handler not called")
	}
}

func testPerCallFilters(t *testing.T) {
	client := NewFilteredMCPClient(ClientConfig{})

	// Create per-call filter
	callFilter := &TestFilter{
		name: "per_call_filter",
		processFunc: func(data []byte) ([]byte, error) {
			return append(data, []byte("_per_call")...), nil
		},
	}

	// Call with filters
	result, err := client.CallToolWithFilters(
		"test_tool",
		map[string]interface{}{"param": "value"},
		callFilter,
	)

	if err != nil {
		t.Errorf("Call with filters failed: %v", err)
	}

	_ = result
}

func testSubscriptions(t *testing.T) {
	client := NewFilteredMCPClient(ClientConfig{})

	// Create subscription filter
	subFilter := &TestFilter{
		name: "subscription_filter",
	}

	// Subscribe with filters
	sub, err := client.SubscribeWithFilters("test_resource", subFilter)
	if err != nil {
		t.Errorf("Subscription failed: %v", err)
	}

	if sub == nil {
		t.Fatal("No subscription returned")
	}

	// Update filters
	newFilter := &TestFilter{
		name: "updated_filter",
	}
	sub.UpdateFilters(newFilter)

	// Unsubscribe
	err = sub.Unsubscribe()
	if err != nil {
		t.Errorf("Unsubscribe failed: %v", err)
	}
}

func testBatchRequests(t *testing.T) {
	client := NewFilteredMCPClient(ClientConfig{
		BatchConcurrency: 5,
	})

	// Create batch requests
	requests := []BatchRequest{
		{
			ID:      "req1",
			Request: map[string]interface{}{"method": "test1"},
		},
		{
			ID:      "req2",
			Request: map[string]interface{}{"method": "test2"},
		},
		{
			ID:      "req3",
			Request: map[string]interface{}{"method": "test3"},
		},
	}

	// Execute batch
	ctx := context.Background()
	result, err := client.BatchRequestsWithFilters(ctx, requests)
	if err != nil {
		t.Errorf("Batch execution failed: %v", err)
	}

	// Check results
	if len(result.Responses) != 3 {
		t.Errorf("Expected 3 responses, got %d", len(result.Responses))
	}

	// Check success rate
	if result.SuccessRate() != 1.0 {
		t.Errorf("Expected 100%% success rate, got %.2f", result.SuccessRate())
	}
}

func testTimeouts(t *testing.T) {
	client := NewFilteredMCPClient(ClientConfig{})

	ctx := context.Background()
	request := map[string]interface{}{
		"method": "slow_operation",
	}

	// Test with timeout
	_, err := client.RequestWithTimeout(ctx, request, 100*time.Millisecond)
	// Timeout might occur depending on implementation
	_ = err

	// Test with retry
	_, err = client.RequestWithRetry(ctx, request, 3, 100*time.Millisecond)
	_ = err
}

func testMetrics(t *testing.T) {
	client := NewFilteredMCPClient(ClientConfig{})

	// Initialize metrics
	client.metricsCollector = &MetricsCollector{
		filterMetrics: make(map[string]*FilterMetrics),
		chainMetrics:  make(map[string]*ChainMetrics),
		systemMetrics: &SystemMetrics{
			StartTime: time.Now(),
		},
	}

	// Record some metrics
	client.RecordFilterExecution("filter1", 10*time.Millisecond, true)
	client.RecordFilterExecution("filter1", 20*time.Millisecond, true)
	client.RecordFilterExecution("filter1", 15*time.Millisecond, false)

	// Get metrics
	metrics := client.GetFilterMetrics()
	if metrics == nil {
		t.Fatal("No metrics returned")
	}

	// Export metrics
	jsonData, err := client.ExportMetrics("json")
	if err != nil {
		t.Errorf("Failed to export metrics: %v", err)
	}
	if len(jsonData) == 0 {
		t.Error("Empty metrics export")
	}

	// Reset metrics
	client.ResetMetrics()
}

func testValidation(t *testing.T) {
	client := NewFilteredMCPClient(ClientConfig{})

	// Create test chain
	chain := NewFilterChain()

	// Add incompatible filters (for testing)
	authFilter := &TestFilter{
		name:       "auth_filter",
		filterType: "authentication",
	}
	authzFilter := &TestFilter{
		name:       "authz_filter",
		filterType: "authorization",
	}

	// Add in wrong order
	chain.Add(authzFilter)
	chain.Add(authFilter)

	// Validate chain
	result, err := client.ValidateFilterChain(chain)
	if err != nil {
		t.Errorf("Validation failed: %v", err)
	}

	// Should have errors
	if len(result.Errors) == 0 {
		t.Error("Expected validation errors")
	}

	if result.Valid {
		t.Error("Chain should be invalid")
	}
}

func testChainCloning(t *testing.T) {
	client := NewFilteredMCPClient(ClientConfig{})

	// Create original chain
	original := NewFilterChain()
	original.name = "original_chain"

	filter1 := &TestFilter{name: "filter1", id: "f1"}
	filter2 := &TestFilter{name: "filter2", id: "f2"}
	filter3 := &TestFilter{name: "filter3", id: "f3"}

	original.Add(filter1)
	original.Add(filter2)
	original.Add(filter3)

	// Register chain
	client.mu.Lock()
	if client.customChains == nil {
		client.customChains = make(map[string]*FilterChain)
	}
	client.customChains["original"] = original
	client.mu.Unlock()

	// Clone with modifications
	cloned, err := client.CloneFilterChain("original", CloneOptions{
		DeepCopy:       true,
		NewName:        "cloned_chain",
		ReverseOrder:   true,
		ExcludeFilters: []string{"f2"},
	})

	if err != nil {
		t.Errorf("Cloning failed: %v", err)
	}

	if cloned == nil {
		t.Fatal("No clone returned")
	}

	// Verify modifications
	if len(cloned.Clone.filters) != 2 {
		t.Errorf("Expected 2 filters, got %d", len(cloned.Clone.filters))
	}

	// Test merging chains
	merged, err := client.MergeChains([]string{"original"}, "merged_chain")
	if err != nil {
		t.Errorf("Merge failed: %v", err)
	}

	if merged == nil {
		t.Fatal("No merged chain returned")
	}
}

func testDebugMode(t *testing.T) {
	client := NewFilteredMCPClient(ClientConfig{})

	// Enable debug mode
	client.EnableDebugMode(
		WithLogLevel("DEBUG"),
		WithLogFilters(true),
		WithLogRequests(true),
		WithTraceExecution(true),
	)

	// Check debug mode is enabled
	if client.debugMode == nil || !client.debugMode.Enabled {
		t.Error("Debug mode not enabled")
	}

	// Dump state
	state := client.DumpState()
	if len(state) == 0 {
		t.Error("Empty state dump")
	}

	// Log filter execution
	testFilter := &TestFilter{name: "debug_test"}
	client.LogFilterExecution(
		testFilter,
		[]byte("input"),
		[]byte("output"),
		10*time.Millisecond,
		nil,
	)

	// Disable debug mode
	client.DisableDebugMode()

	if client.debugMode.Enabled {
		t.Error("Debug mode not disabled")
	}
}

// TestFilter is a test implementation of Filter.
type TestFilter struct {
	name        string
	id          string
	filterType  string
	processFunc func([]byte) ([]byte, error)
	version     string
	description string
	config      map[string]interface{}
}

func (tf *TestFilter) GetName() string {
	return tf.name
}

func (tf *TestFilter) GetID() string {
	if tf.id == "" {
		return tf.name
	}
	return tf.id
}

func (tf *TestFilter) GetType() string {
	if tf.filterType == "" {
		return "test"
	}
	return tf.filterType
}

func (tf *TestFilter) Process(data []byte) ([]byte, error) {
	if tf.processFunc != nil {
		return tf.processFunc(data)
	}
	return data, nil
}

func (tf *TestFilter) Clone() Filter {
	return &TestFilter{
		name:        tf.name + "_clone",
		id:          tf.id + "_clone",
		filterType:  tf.filterType,
		processFunc: tf.processFunc,
		version:     tf.version,
		description: tf.description,
		config:      tf.config,
	}
}

func (tf *TestFilter) GetVersion() string {
	if tf.version == "" {
		return "1.0.0"
	}
	return tf.version
}

func (tf *TestFilter) GetDescription() string {
	if tf.description == "" {
		return "Test filter"
	}
	return tf.description
}

func (tf *TestFilter) ValidateConfig() error {
	return nil
}

func (tf *TestFilter) GetConfiguration() map[string]interface{} {
	if tf.config == nil {
		return make(map[string]interface{})
	}
	return tf.config
}

func (tf *TestFilter) UpdateConfig(config map[string]interface{}) {
	tf.config = config
}

func (tf *TestFilter) GetCapabilities() []string {
	return []string{"test"}
}

func (tf *TestFilter) GetDependencies() []FilterDependency {
	return nil
}

func (tf *TestFilter) GetResourceRequirements() ResourceRequirements {
	return ResourceRequirements{}
}

func (tf *TestFilter) GetTypeInfo() TypeInfo {
	return TypeInfo{
		InputTypes:  []string{"bytes"},
		OutputTypes: []string{"bytes"},
	}
}

func (tf *TestFilter) EstimateLatency() time.Duration {
	return 1 * time.Millisecond
}

func (tf *TestFilter) HasBlockingOperations() bool {
	return false
}

func (tf *TestFilter) HasKnownVulnerabilities() bool {
	return false
}

func (tf *TestFilter) IsStateless() bool {
	return true
}

func (tf *TestFilter) SetID(id string) {
	tf.id = id
}

func (tf *TestFilter) UsesDeprecatedFeatures() bool {
	return false
}
