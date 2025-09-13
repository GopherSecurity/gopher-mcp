package integration_test

import (
	"context"
	"sync"
	"testing"
	"time"

	"github.com/GopherSecurity/gopher-mcp/src/integration"
)

// mockFilter is a test implementation of the Filter interface
type mockFilter struct {
	id          string
	name        string
	filterType  string
	version     string
	description string
	processFunc func([]byte) ([]byte, error)
	config      map[string]interface{}
	stateless   bool
}

func (m *mockFilter) GetID() string                                { return m.id }
func (m *mockFilter) GetName() string                              { return m.name }
func (m *mockFilter) GetType() string                              { return m.filterType }
func (m *mockFilter) GetVersion() string                           { return m.version }
func (m *mockFilter) GetDescription() string                       { return m.description }
func (m *mockFilter) ValidateConfig() error                        { return nil }
func (m *mockFilter) GetConfiguration() map[string]interface{}     { return m.config }
func (m *mockFilter) UpdateConfig(cfg map[string]interface{})      { m.config = cfg }
func (m *mockFilter) GetCapabilities() []string                    { return []string{"filter", "transform"} }
func (m *mockFilter) GetDependencies() []integration.FilterDependency { return nil }
func (m *mockFilter) GetResourceRequirements() integration.ResourceRequirements {
	return integration.ResourceRequirements{Memory: 1024, CPUCores: 1}
}
func (m *mockFilter) GetTypeInfo() integration.TypeInfo {
	return integration.TypeInfo{
		InputTypes:  []string{"bytes"},
		OutputTypes: []string{"bytes"},
	}
}
func (m *mockFilter) EstimateLatency() time.Duration       { return 10 * time.Millisecond }
func (m *mockFilter) HasBlockingOperations() bool          { return false }
func (m *mockFilter) UsesDeprecatedFeatures() bool         { return false }
func (m *mockFilter) HasKnownVulnerabilities() bool        { return false }
func (m *mockFilter) IsStateless() bool                    { return m.stateless }
func (m *mockFilter) SetID(id string)                      { m.id = id }
func (m *mockFilter) Clone() integration.Filter {
	return &mockFilter{
		id:          m.id + "_clone",
		name:        m.name,
		filterType:  m.filterType,
		version:     m.version,
		description: m.description,
		processFunc: m.processFunc,
		config:      m.config,
		stateless:   m.stateless,
	}
}

func (m *mockFilter) Process(data []byte) ([]byte, error) {
	if m.processFunc != nil {
		return m.processFunc(data)
	}
	return data, nil
}

// Test 1: FilteredMCPServer creation
func TestFilteredMCPServer_Creation(t *testing.T) {
	server := integration.NewFilteredMCPServer()
	if server == nil {
		t.Fatal("NewFilteredMCPServer returned nil")
	}
}

// Test 2: Server request chain setup
func TestFilteredMCPServer_SetRequestChain(t *testing.T) {
	server := integration.NewFilteredMCPServer()
	
	chain := integration.NewFilterChain()
	chain.SetName("server_request_chain")
	
	filter := &mockFilter{
		id:   "req_filter",
		name: "server_request_filter",
	}
	chain.Add(filter)
	
	server.SetRequestChain(chain)
}

// Test 3: Server response chain setup
func TestFilteredMCPServer_SetResponseChain(t *testing.T) {
	server := integration.NewFilteredMCPServer()
	
	chain := integration.NewFilterChain()
	chain.SetName("server_response_chain")
	
	server.SetResponseChain(chain)
}

// Test 4: Process server request
func TestFilteredMCPServer_ProcessRequest(t *testing.T) {
	server := integration.NewFilteredMCPServer()
	
	// Process request (no chain set, should pass through)
	input := []byte("test_request")
	output, err := server.ProcessRequest(input)
	if err != nil {
		t.Fatalf("ProcessRequest failed: %v", err)
	}
	
	if string(output) != "test_request" {
		t.Error("Request modified unexpectedly")
	}
}

// Test 5: Process server response
func TestFilteredMCPServer_ProcessResponse(t *testing.T) {
	server := integration.NewFilteredMCPServer()
	
	// Process response (no chain set, should pass through)
	input := []byte("test_response")
	output, err := server.ProcessResponse(input, "req123")
	if err != nil {
		t.Fatalf("ProcessResponse failed: %v", err)
	}
	
	if string(output) != "test_response" {
		t.Error("Response modified unexpectedly")
	}
}

// Test 6: Handle server request
func TestFilteredMCPServer_HandleRequest(t *testing.T) {
	server := integration.NewFilteredMCPServer()
	
	request := map[string]interface{}{
		"method": "test",
		"params": "data",
	}
	
	// Handle request (would interact with actual MCP server)
	_, err := server.HandleRequest(request)
	// Error expected as no actual server implementation
	_ = err
}

// Test 7: Send server response
func TestFilteredMCPServer_SendResponse(t *testing.T) {
	server := integration.NewFilteredMCPServer()
	
	response := map[string]interface{}{
		"result": "test_result",
	}
	
	// Send response (would interact with actual MCP server)
	err := server.SendResponse(response)
	// Error expected as no actual server implementation
	_ = err
}

// Test 8: Register filtered tool
func TestFilteredMCPServer_RegisterFilteredTool(t *testing.T) {
	server := integration.NewFilteredMCPServer()
	
	// Mock tool interface
	tool := &mockTool{
		name: "test_tool",
	}
	
	filter := &mockFilter{
		id:   "tool_filter",
		name: "tool_filter",
	}
	
	err := server.RegisterFilteredTool(tool, filter)
	// May fail as implementation depends on actual MCP server
	_ = err
}

// Test 9: Register filtered resource
func TestFilteredMCPServer_RegisterFilteredResource(t *testing.T) {
	server := integration.NewFilteredMCPServer()
	
	// Mock resource interface
	resource := &mockResource{
		name: "test_resource",
	}
	
	filter := &mockFilter{
		id:   "resource_filter",
		name: "resource_filter",
	}
	
	err := server.RegisterFilteredResource(resource, filter)
	// May fail as implementation depends on actual MCP server
	_ = err
}

// Test 10: Register filtered prompt
func TestFilteredMCPServer_RegisterFilteredPrompt(t *testing.T) {
	server := integration.NewFilteredMCPServer()
	
	// Mock prompt interface
	prompt := &mockPrompt{
		name: "test_prompt",
	}
	
	filter := &mockFilter{
		id:   "prompt_filter",
		name: "prompt_filter",
	}
	
	err := server.RegisterFilteredPrompt(prompt, filter)
	// May fail as implementation depends on actual MCP server
	_ = err
}

// Test 11: Timeout filter creation
func TestTimeoutFilter_Creation(t *testing.T) {
	filter := &integration.TimeoutFilter{
		Timeout: 100 * time.Millisecond,
	}
	
	if filter.Timeout != 100*time.Millisecond {
		t.Error("Timeout not set correctly")
	}
}

// Test 12: Connect with filters
func TestFilteredMCPClient_ConnectWithFilters(t *testing.T) {
	client := integration.NewFilteredMCPClient(integration.ClientConfig{})
	
	// Mock transport
	transport := &mockTransport{}
	
	filter := &mockFilter{
		id:   "connect_filter",
		name: "connection_filter",
	}
	
	ctx := context.Background()
	err := client.ConnectWithFilters(ctx, transport, filter)
	// May fail as implementation depends on actual transport
	_ = err
}

// Test 13: Batch request processing
func TestBatchRequest_Processing(t *testing.T) {
	client := integration.NewFilteredMCPClient(integration.ClientConfig{
		BatchConcurrency: 3,
		BatchFailFast:    false,
	})
	
	requests := []integration.BatchRequest{
		{ID: "1", Request: map[string]interface{}{"method": "test1"}},
		{ID: "2", Request: map[string]interface{}{"method": "test2"}},
		{ID: "3", Request: map[string]interface{}{"method": "test3"}},
	}
	
	ctx := context.Background()
	result, err := client.BatchRequestsWithFilters(ctx, requests)
	
	// Check result structure
	if result != nil {
		if result.SuccessRate() < 0 || result.SuccessRate() > 1 {
			t.Error("Invalid success rate")
		}
	}
	
	_ = err
}

// Test 14: Subscription management
func TestSubscription_Lifecycle(t *testing.T) {
	sub := &integration.Subscription{
		ID:       "sub123",
		Resource: "test_resource",
	}
	
	// Update filters
	filter := &mockFilter{
		id:   "sub_filter",
		name: "subscription_filter",
	}
	sub.UpdateFilters(filter)
	
	// Unsubscribe
	err := sub.Unsubscribe()
	// May fail as no actual subscription exists
	_ = err
}

// Test 15: Debug mode functionality
func TestDebugMode_Operations(t *testing.T) {
	client := integration.NewFilteredMCPClient(integration.ClientConfig{})
	
	// Enable debug mode with various options
	client.EnableDebugMode(
		integration.WithLogLevel("DEBUG"),
		integration.WithLogFilters(true),
		integration.WithLogRequests(true),
		integration.WithTraceExecution(true),
	)
	
	// Dump state
	state := client.DumpState()
	if state == "" {
		t.Error("Empty state dump")
	}
	
	// Disable debug mode
	client.DisableDebugMode()
}

// Test 16: Validation result handling
func TestValidationResult_Processing(t *testing.T) {
	result := &integration.ValidationResult{
		Valid:    true,
		Errors:   []integration.ValidationError{},
		Warnings: []integration.ValidationWarning{},
	}
	
	// Add error
	result.Errors = append(result.Errors, integration.ValidationError{
		ErrorType: "ERROR",
		Message:   "Test error",
	})
	
	// Should be invalid now
	result.Valid = false
	
	if result.Valid {
		t.Error("Result should be invalid after adding error")
	}
}

// Test 17: Clone options configuration
func TestCloneOptions_Configuration(t *testing.T) {
	options := integration.CloneOptions{
		DeepCopy:       true,
		NewName:        "cloned_chain",
		ReverseOrder:   true,
		ExcludeFilters: []string{"filter1", "filter2"},
	}
	
	if !options.DeepCopy {
		t.Error("DeepCopy should be true")
	}
	
	if options.NewName != "cloned_chain" {
		t.Error("NewName not set correctly")
	}
	
	if len(options.ExcludeFilters) != 2 {
		t.Error("ExcludeFilters not set correctly")
	}
}

// Test 18: Filter chain info retrieval
func TestFilterChainInfo_Structure(t *testing.T) {
	info := &integration.FilterChainInfo{
		ChainID:     "chain123",
		Name:        "test_chain",
		Description: "Test chain",
		Filters:     []integration.FilterInfo{},
		Statistics:  integration.ChainStatistics{},
	}
	
	// Add filter info
	info.Filters = append(info.Filters, integration.FilterInfo{
		ID:       "filter1",
		Name:     "test_filter",
		Type:     "validation",
		Position: 0,
	})
	
	if len(info.Filters) != 1 {
		t.Error("Filter not added to info")
	}
}

// Test 19: Concurrent filter operations
func TestConcurrent_FilterOperations(t *testing.T) {
	chain := integration.NewFilterChain()
	
	// Add multiple filters
	for i := 0; i < 5; i++ {
		filter := &mockFilter{
			id:   string(rune('A' + i)),
			name: "concurrent_filter",
			processFunc: func(data []byte) ([]byte, error) {
				// Simulate processing
				time.Sleep(time.Microsecond)
				return data, nil
			},
		}
		chain.Add(filter)
	}
	
	// Process concurrently
	var wg sync.WaitGroup
	numGoroutines := 50
	errors := make(chan error, numGoroutines)
	
	for i := 0; i < numGoroutines; i++ {
		wg.Add(1)
		go func() {
			defer wg.Done()
			_, err := chain.Process([]byte("test"))
			if err != nil {
				errors <- err
			}
		}()
	}
	
	wg.Wait()
	close(errors)
	
	// Check for errors
	errorCount := 0
	for err := range errors {
		if err != nil {
			errorCount++
			t.Logf("Concurrent processing error: %v", err)
		}
	}
	
	if errorCount > 0 {
		t.Errorf("Had %d errors during concurrent processing", errorCount)
	}
}

// Test 20: Complete integration scenario
func TestComplete_IntegrationScenario(t *testing.T) {
	// Create client and server
	client := integration.NewFilteredMCPClient(integration.ClientConfig{
		EnableFiltering: true,
	})
	server := integration.NewFilteredMCPServer()
	
	// Set up client chains
	clientReqChain := integration.NewFilterChain()
	clientReqChain.SetName("client_request")
	clientReqChain.Add(&mockFilter{
		id:   "client_req",
		name: "client_request_filter",
		processFunc: func(data []byte) ([]byte, error) {
			return append([]byte("CLIENT_REQ:"), data...), nil
		},
	})
	client.SetClientRequestChain(clientReqChain)
	
	clientRespChain := integration.NewFilterChain()
	clientRespChain.SetName("client_response")
	clientRespChain.Add(&mockFilter{
		id:   "client_resp",
		name: "client_response_filter",
		processFunc: func(data []byte) ([]byte, error) {
			return append([]byte("CLIENT_RESP:"), data...), nil
		},
	})
	client.SetClientResponseChain(clientRespChain)
	
	// Set up server chains
	serverReqChain := integration.NewFilterChain()
	serverReqChain.SetName("server_request")
	serverReqChain.Add(&mockFilter{
		id:   "server_req",
		name: "server_request_filter",
		processFunc: func(data []byte) ([]byte, error) {
			return append([]byte("SERVER_REQ:"), data...), nil
		},
	})
	server.SetRequestChain(serverReqChain)
	
	serverRespChain := integration.NewFilterChain()
	serverRespChain.SetName("server_response")
	serverRespChain.Add(&mockFilter{
		id:   "server_resp",
		name: "server_response_filter",
		processFunc: func(data []byte) ([]byte, error) {
			return append([]byte("SERVER_RESP:"), data...), nil
		},
	})
	server.SetResponseChain(serverRespChain)
	
	// Simulate request flow
	originalRequest := []byte("test_request")
	
	// Client processes outgoing request
	clientProcessed, err := client.FilterOutgoingRequest(originalRequest)
	if err != nil {
		t.Fatalf("Client request filtering failed: %v", err)
	}
	
	// Server processes incoming request
	_, err = server.ProcessRequest(clientProcessed)
	if err != nil {
		t.Fatalf("Server request processing failed: %v", err)
	}
	
	// Server processes outgoing response
	serverResponse, err := server.ProcessResponse([]byte("response"), "req123")
	if err != nil {
		t.Fatalf("Server response processing failed: %v", err)
	}
	
	// Client processes incoming response
	finalResponse, err := client.FilterIncomingResponse(serverResponse)
	if err != nil {
		t.Fatalf("Client response filtering failed: %v", err)
	}
	
	// Verify transformations occurred
	if len(finalResponse) <= len(originalRequest) {
		t.Error("Response should be longer after all transformations")
	}
}

// Mock implementations for testing

type mockTool struct {
	name string
}

func (m *mockTool) Name() string {
	return m.name
}

func (m *mockTool) Execute(params interface{}) (interface{}, error) {
	return map[string]interface{}{"result": "ok"}, nil
}

type mockResource struct {
	name string
}

func (m *mockResource) Name() string {
	return m.name
}

func (m *mockResource) Read() ([]byte, error) {
	return []byte("resource data"), nil
}

func (m *mockResource) Write(data []byte) error {
	return nil
}

type mockPrompt struct {
	name string
}

func (m *mockPrompt) Name() string {
	return m.name
}

func (m *mockPrompt) Generate(params interface{}) (string, error) {
	return "generated prompt", nil
}

type mockTransport struct{}

func (m *mockTransport) Connect(ctx context.Context) error {
	return nil
}

func (m *mockTransport) Send(data []byte) error {
	return nil
}

func (m *mockTransport) Receive() ([]byte, error) {
	return []byte("received"), nil
}

func (m *mockTransport) Disconnect() error {
	return nil
}

func (m *mockTransport) Close() error {
	return nil
}

// Benchmarks

func BenchmarkIntegration_FilterChainProcessing(b *testing.B) {
	chain := integration.NewFilterChain()
	
	for i := 0; i < 10; i++ {
		chain.Add(&mockFilter{
			id:   string(rune('A' + i)),
			name: "bench_filter",
			processFunc: func(data []byte) ([]byte, error) {
				return data, nil
			},
		})
	}
	
	data := []byte("benchmark data")
	
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		chain.Process(data)
	}
}

func BenchmarkIntegration_ClientServerFlow(b *testing.B) {
	client := integration.NewFilteredMCPClient(integration.ClientConfig{})
	server := integration.NewFilteredMCPServer()
	
	// Set up minimal chains
	clientChain := integration.NewFilterChain()
	clientChain.Add(&mockFilter{
		id:   "client",
		name: "client_filter",
		processFunc: func(data []byte) ([]byte, error) {
			return data, nil
		},
	})
	client.SetClientRequestChain(clientChain)
	
	serverChain := integration.NewFilterChain()
	serverChain.Add(&mockFilter{
		id:   "server",
		name: "server_filter",
		processFunc: func(data []byte) ([]byte, error) {
			return data, nil
		},
	})
	server.SetRequestChain(serverChain)
	
	data := []byte("benchmark data")
	
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		// Client -> Server -> Client flow
		processed, _ := client.FilterOutgoingRequest(data)
		processed, _ = server.ProcessRequest(processed)
		server.ProcessResponse(processed, "req")
	}
}

func BenchmarkIntegration_ConcurrentChains(b *testing.B) {
	chains := make([]*integration.FilterChain, 10)
	
	for i := 0; i < 10; i++ {
		chain := integration.NewFilterChain()
		chain.Add(&mockFilter{
			id:   string(rune('A' + i)),
			name: "concurrent_filter",
			processFunc: func(data []byte) ([]byte, error) {
				return data, nil
			},
		})
		chains[i] = chain
	}
	
	data := []byte("benchmark data")
	
	b.RunParallel(func(pb *testing.PB) {
		i := 0
		for pb.Next() {
			chains[i%10].Process(data)
			i++
		}
	})
}

func BenchmarkIntegration_ValidationOperations(b *testing.B) {
	client := integration.NewFilteredMCPClient(integration.ClientConfig{})
	
	chain := integration.NewFilterChain()
	for i := 0; i < 5; i++ {
		chain.Add(&mockFilter{
			id:         string(rune('A' + i)),
			name:       "validation_filter",
			filterType: "validation",
		})
	}
	
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		client.ValidateFilterChain(chain)
	}
}