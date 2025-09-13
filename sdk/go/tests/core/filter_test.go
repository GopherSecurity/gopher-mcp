package core_test

import (
	"context"
	"errors"
	"io"
	"strings"
	"sync"
	"testing"
	"time"

	"github.com/GopherSecurity/gopher-mcp/src/core"
	"github.com/GopherSecurity/gopher-mcp/src/types"
)

// Mock implementation of Filter interface
type mockFilterImpl struct {
	name       string
	filterType string
	stats      types.FilterStatistics
	initialized bool
	closed      bool
	processFunc func(context.Context, []byte) (*types.FilterResult, error)
	mu          sync.Mutex
}

func (m *mockFilterImpl) Process(ctx context.Context, data []byte) (*types.FilterResult, error) {
	if m.processFunc != nil {
		return m.processFunc(ctx, data)
	}
	return types.ContinueWith(data), nil
}

func (m *mockFilterImpl) Initialize(config types.FilterConfig) error {
	m.mu.Lock()
	defer m.mu.Unlock()
	
	if m.initialized {
		return errors.New("already initialized")
	}
	m.initialized = true
	return nil
}

func (m *mockFilterImpl) Close() error {
	m.mu.Lock()
	defer m.mu.Unlock()
	
	if m.closed {
		return errors.New("already closed")
	}
	m.closed = true
	return nil
}

func (m *mockFilterImpl) Name() string {
	return m.name
}

func (m *mockFilterImpl) Type() string {
	return m.filterType
}

func (m *mockFilterImpl) GetStats() types.FilterStatistics {
	return m.stats
}

// Test 1: Basic Filter interface implementation
func TestFilter_BasicImplementation(t *testing.T) {
	filter := &mockFilterImpl{
		name:       "test-filter",
		filterType: "mock",
	}
	
	// Verify interface is satisfied
	var _ core.Filter = filter
	
	// Test Name
	if filter.Name() != "test-filter" {
		t.Errorf("Name() = %s, want test-filter", filter.Name())
	}
	
	// Test Type
	if filter.Type() != "mock" {
		t.Errorf("Type() = %s, want mock", filter.Type())
	}
	
	// Test Initialize
	config := types.FilterConfig{Name: "test"}
	err := filter.Initialize(config)
	if err != nil {
		t.Fatalf("Initialize failed: %v", err)
	}
	
	// Test Process
	data := []byte("test data")
	result, err := filter.Process(context.Background(), data)
	if err != nil {
		t.Fatalf("Process failed: %v", err)
	}
	if string(result.Data) != string(data) {
		t.Errorf("Process result = %s, want %s", result.Data, data)
	}
	
	// Test Close
	err = filter.Close()
	if err != nil {
		t.Fatalf("Close failed: %v", err)
	}
}

// Test 2: Filter with custom process function
func TestFilter_CustomProcess(t *testing.T) {
	transformCalled := false
	filter := &mockFilterImpl{
		name: "transform-filter",
		processFunc: func(ctx context.Context, data []byte) (*types.FilterResult, error) {
			transformCalled = true
			transformed := append([]byte("prefix-"), data...)
			return types.ContinueWith(transformed), nil
		},
	}
	
	result, err := filter.Process(context.Background(), []byte("data"))
	if err != nil {
		t.Fatalf("Process failed: %v", err)
	}
	
	if !transformCalled {
		t.Error("Custom process function not called")
	}
	
	expected := "prefix-data"
	if string(result.Data) != expected {
		t.Errorf("Result = %s, want %s", result.Data, expected)
	}
}

// Test 3: Filter error handling
func TestFilter_ErrorHandling(t *testing.T) {
	testErr := errors.New("process error")
	filter := &mockFilterImpl{
		name: "error-filter",
		processFunc: func(ctx context.Context, data []byte) (*types.FilterResult, error) {
			return nil, testErr
		},
	}
	
	_, err := filter.Process(context.Background(), []byte("data"))
	if err != testErr {
		t.Errorf("Process error = %v, want %v", err, testErr)
	}
	
	// Test double initialization
	filter2 := &mockFilterImpl{initialized: true}
	err = filter2.Initialize(types.FilterConfig{})
	if err == nil {
		t.Error("Double initialization should return error")
	}
	
	// Test double close
	filter3 := &mockFilterImpl{closed: true}
	err = filter3.Close()
	if err == nil {
		t.Error("Double close should return error")
	}
}

// Mock implementation of LifecycleFilter
type mockLifecycleFilter struct {
	mockFilterImpl
	attached bool
	started  bool
	chain    *core.FilterChain
}

func (m *mockLifecycleFilter) OnAttach(chain *core.FilterChain) error {
	m.attached = true
	m.chain = chain
	return nil
}

func (m *mockLifecycleFilter) OnDetach() error {
	m.attached = false
	m.chain = nil
	return nil
}

func (m *mockLifecycleFilter) OnStart(ctx context.Context) error {
	m.started = true
	return nil
}

func (m *mockLifecycleFilter) OnStop(ctx context.Context) error {
	m.started = false
	return nil
}

// Test 4: LifecycleFilter implementation
func TestLifecycleFilter(t *testing.T) {
	filter := &mockLifecycleFilter{
		mockFilterImpl: mockFilterImpl{name: "lifecycle-filter"},
	}
	
	// Verify interface is satisfied
	var _ core.LifecycleFilter = filter
	
	// Test OnAttach
	chain := core.NewFilterChain(types.ChainConfig{Name: "test-chain"})
	err := filter.OnAttach(chain)
	if err != nil {
		t.Fatalf("OnAttach failed: %v", err)
	}
	if !filter.attached {
		t.Error("Filter not marked as attached")
	}
	if filter.chain != chain {
		t.Error("Chain reference not stored")
	}
	
	// Test OnStart
	err = filter.OnStart(context.Background())
	if err != nil {
		t.Fatalf("OnStart failed: %v", err)
	}
	if !filter.started {
		t.Error("Filter not marked as started")
	}
	
	// Test OnStop
	err = filter.OnStop(context.Background())
	if err != nil {
		t.Fatalf("OnStop failed: %v", err)
	}
	if filter.started {
		t.Error("Filter not marked as stopped")
	}
	
	// Test OnDetach
	err = filter.OnDetach()
	if err != nil {
		t.Fatalf("OnDetach failed: %v", err)
	}
	if filter.attached {
		t.Error("Filter not marked as detached")
	}
	if filter.chain != nil {
		t.Error("Chain reference not cleared")
	}
}

// Mock implementation of StatefulFilter
type mockStatefulFilter struct {
	mockFilterImpl
	state map[string]interface{}
}

func (m *mockStatefulFilter) SaveState(w io.Writer) error {
	// Simple implementation: write state keys
	for k := range m.state {
		w.Write([]byte(k + "\n"))
	}
	return nil
}

func (m *mockStatefulFilter) LoadState(r io.Reader) error {
	// Simple implementation: read state keys
	buf := make([]byte, 1024)
	n, _ := r.Read(buf)
	if n > 0 {
		m.state["loaded"] = string(buf[:n])
	}
	return nil
}

func (m *mockStatefulFilter) GetState() interface{} {
	return m.state
}

func (m *mockStatefulFilter) ResetState() error {
	m.state = make(map[string]interface{})
	return nil
}

// Test 5: StatefulFilter implementation
func TestStatefulFilter(t *testing.T) {
	filter := &mockStatefulFilter{
		mockFilterImpl: mockFilterImpl{name: "stateful-filter"},
		state:          make(map[string]interface{}),
	}
	
	// Verify interface is satisfied
	var _ core.StatefulFilter = filter
	
	// Set some state
	filter.state["key1"] = "value1"
	filter.state["key2"] = 42
	
	// Test GetState
	state := filter.GetState()
	stateMap, ok := state.(map[string]interface{})
	if !ok {
		t.Fatal("GetState did not return expected type")
	}
	if stateMap["key1"] != "value1" {
		t.Error("State key1 not preserved")
	}
	
	// Test SaveState
	var buf strings.Builder
	err := filter.SaveState(&buf)
	if err != nil {
		t.Fatalf("SaveState failed: %v", err)
	}
	saved := buf.String()
	if !strings.Contains(saved, "key1") || !strings.Contains(saved, "key2") {
		t.Error("State not properly saved")
	}
	
	// Test LoadState
	reader := strings.NewReader("test-data")
	err = filter.LoadState(reader)
	if err != nil {
		t.Fatalf("LoadState failed: %v", err)
	}
	if filter.state["loaded"] != "test-data" {
		t.Error("State not properly loaded")
	}
	
	// Test ResetState
	err = filter.ResetState()
	if err != nil {
		t.Fatalf("ResetState failed: %v", err)
	}
	if len(filter.state) != 0 {
		t.Error("State not reset")
	}
}

// Mock implementation of ConfigurableFilter
type mockConfigurableFilter struct {
	mockFilterImpl
	config        types.FilterConfig
	configVersion string
}

func (m *mockConfigurableFilter) UpdateConfig(config types.FilterConfig) error {
	if config.Name == "" {
		return errors.New("invalid config: name required")
	}
	m.config = config
	m.configVersion = time.Now().Format(time.RFC3339)
	return nil
}

func (m *mockConfigurableFilter) ValidateConfig(config types.FilterConfig) error {
	if config.Name == "" {
		return errors.New("invalid config: name required")
	}
	return nil
}

func (m *mockConfigurableFilter) GetConfigVersion() string {
	return m.configVersion
}

// Test 6: ConfigurableFilter implementation
func TestConfigurableFilter(t *testing.T) {
	filter := &mockConfigurableFilter{
		mockFilterImpl: mockFilterImpl{name: "configurable-filter"},
		configVersion:  "v1",
	}
	
	// Verify interface is satisfied
	var _ core.ConfigurableFilter = filter
	
	// Test ValidateConfig with valid config
	validConfig := types.FilterConfig{Name: "test"}
	err := filter.ValidateConfig(validConfig)
	if err != nil {
		t.Fatalf("ValidateConfig failed for valid config: %v", err)
	}
	
	// Test ValidateConfig with invalid config
	invalidConfig := types.FilterConfig{Name: ""}
	err = filter.ValidateConfig(invalidConfig)
	if err == nil {
		t.Error("ValidateConfig should fail for invalid config")
	}
	
	// Test UpdateConfig
	newConfig := types.FilterConfig{Name: "updated"}
	err = filter.UpdateConfig(newConfig)
	if err != nil {
		t.Fatalf("UpdateConfig failed: %v", err)
	}
	if filter.config.Name != "updated" {
		t.Error("Config not updated")
	}
	
	// Test GetConfigVersion
	version := filter.GetConfigVersion()
	if version == "v1" {
		t.Error("Config version not updated")
	}
}

// Test 7: ObservableFilter implementation
type mockObservableFilter struct {
	mockFilterImpl
	metrics core.FilterMetrics
	health  core.HealthStatus
}

func (m *mockObservableFilter) GetMetrics() core.FilterMetrics {
	return m.metrics
}

func (m *mockObservableFilter) GetHealthStatus() core.HealthStatus {
	return m.health
}

func (m *mockObservableFilter) GetTraceSpan() interface{} {
	return "trace-span-123"
}

func TestObservableFilter(t *testing.T) {
	filter := &mockObservableFilter{
		mockFilterImpl: mockFilterImpl{name: "observable-filter"},
		metrics: core.FilterMetrics{
			RequestsTotal:  100,
			ErrorsTotal:    5,
		},
		health: core.HealthStatus{
			Healthy: true,
			Status:  "healthy",
		},
	}
	
	// Verify interface is satisfied
	var _ core.ObservableFilter = filter
	
	// Test GetMetrics
	metrics := filter.GetMetrics()
	if metrics.RequestsTotal != 100 {
		t.Errorf("RequestsTotal = %d, want 100", metrics.RequestsTotal)
	}
	if metrics.ErrorsTotal != 5 {
		t.Errorf("ErrorsTotal = %d, want 5", metrics.ErrorsTotal)
	}
	
	// Test GetHealthStatus
	health := filter.GetHealthStatus()
	if !health.Healthy {
		t.Error("Health status should be healthy")
	}
	if health.Status != "healthy" {
		t.Errorf("Health status = %s, want healthy", health.Status)
	}
	
	// Test GetTraceSpan
	span := filter.GetTraceSpan()
	if span != "trace-span-123" {
		t.Error("Trace span not returned correctly")
	}
}

// Test 8: HookableFilter implementation
type mockHookableFilter struct {
	mockFilterImpl
	preHooks  map[string]core.FilterHook
	postHooks map[string]core.FilterHook
	hookID    int
}

func (m *mockHookableFilter) AddPreHook(hook core.FilterHook) string {
	if m.preHooks == nil {
		m.preHooks = make(map[string]core.FilterHook)
	}
	m.hookID++
	id := string(rune('A' + m.hookID))
	m.preHooks[id] = hook
	return id
}

func (m *mockHookableFilter) AddPostHook(hook core.FilterHook) string {
	if m.postHooks == nil {
		m.postHooks = make(map[string]core.FilterHook)
	}
	m.hookID++
	id := string(rune('A' + m.hookID))
	m.postHooks[id] = hook
	return id
}

func (m *mockHookableFilter) RemoveHook(id string) error {
	if _, ok := m.preHooks[id]; ok {
		delete(m.preHooks, id)
		return nil
	}
	if _, ok := m.postHooks[id]; ok {
		delete(m.postHooks, id)
		return nil
	}
	return errors.New("hook not found")
}

func TestHookableFilter(t *testing.T) {
	filter := &mockHookableFilter{
		mockFilterImpl: mockFilterImpl{name: "hookable-filter"},
	}
	
	// Verify interface is satisfied
	var _ core.HookableFilter = filter
	
	// Test AddPreHook
	preHook := func(ctx context.Context, data []byte) ([]byte, error) {
		return append([]byte("pre-"), data...), nil
	}
	preID := filter.AddPreHook(preHook)
	if preID == "" {
		t.Error("AddPreHook returned empty ID")
	}
	if len(filter.preHooks) != 1 {
		t.Error("Pre hook not added")
	}
	
	// Test AddPostHook
	postHook := func(ctx context.Context, data []byte) ([]byte, error) {
		return append(data, []byte("-post")...), nil
	}
	postID := filter.AddPostHook(postHook)
	if postID == "" {
		t.Error("AddPostHook returned empty ID")
	}
	if len(filter.postHooks) != 1 {
		t.Error("Post hook not added")
	}
	
	// Test RemoveHook
	err := filter.RemoveHook(preID)
	if err != nil {
		t.Fatalf("RemoveHook failed: %v", err)
	}
	if len(filter.preHooks) != 0 {
		t.Error("Pre hook not removed")
	}
	
	// Test RemoveHook for non-existent hook
	err = filter.RemoveHook("non-existent")
	if err == nil {
		t.Error("RemoveHook should fail for non-existent hook")
	}
}

// Test 9: BatchFilter implementation
type mockBatchFilter struct {
	mockFilterImpl
	batchSize    int
	batchTimeout time.Duration
}

func (m *mockBatchFilter) ProcessBatch(ctx context.Context, batch [][]byte) ([]*types.FilterResult, error) {
	results := make([]*types.FilterResult, len(batch))
	for i, data := range batch {
		results[i] = types.ContinueWith(append([]byte("batch-"), data...))
	}
	return results, nil
}

func (m *mockBatchFilter) SetBatchSize(size int) {
	m.batchSize = size
}

func (m *mockBatchFilter) SetBatchTimeout(timeout time.Duration) {
	m.batchTimeout = timeout
}

func TestBatchFilter(t *testing.T) {
	filter := &mockBatchFilter{
		mockFilterImpl: mockFilterImpl{name: "batch-filter"},
	}
	
	// Verify interface is satisfied
	var _ core.BatchFilter = filter
	
	// Test SetBatchSize
	filter.SetBatchSize(10)
	if filter.batchSize != 10 {
		t.Errorf("Batch size = %d, want 10", filter.batchSize)
	}
	
	// Test SetBatchTimeout
	timeout := 5 * time.Second
	filter.SetBatchTimeout(timeout)
	if filter.batchTimeout != timeout {
		t.Errorf("Batch timeout = %v, want %v", filter.batchTimeout, timeout)
	}
	
	// Test ProcessBatch
	batch := [][]byte{
		[]byte("item1"),
		[]byte("item2"),
		[]byte("item3"),
	}
	
	results, err := filter.ProcessBatch(context.Background(), batch)
	if err != nil {
		t.Fatalf("ProcessBatch failed: %v", err)
	}
	
	if len(results) != 3 {
		t.Fatalf("Results length = %d, want 3", len(results))
	}
	
	for i, result := range results {
		expected := "batch-item" + string(rune('1'+i))
		if string(result.Data) != expected {
			t.Errorf("Result[%d] = %s, want %s", i, result.Data, expected)
		}
	}
}

// Test 10: Complex filter implementing multiple interfaces
type complexFilter struct {
	mockFilterImpl
	mockLifecycleFilter
	mockStatefulFilter
	mockConfigurableFilter
	mockObservableFilter
}

func TestComplexFilter_MultipleInterfaces(t *testing.T) {
	filter := &complexFilter{
		mockFilterImpl:         mockFilterImpl{name: "complex-filter"},
		mockStatefulFilter:     mockStatefulFilter{state: make(map[string]interface{})},
		mockConfigurableFilter: mockConfigurableFilter{configVersion: "v1"},
		mockObservableFilter: mockObservableFilter{
			metrics: core.FilterMetrics{RequestsTotal: 50},
			health:  core.HealthStatus{Healthy: true},
		},
	}
	
	// Verify all interfaces are satisfied
	var _ core.Filter = filter
	var _ core.LifecycleFilter = filter
	var _ core.StatefulFilter = filter
	var _ core.ConfigurableFilter = filter
	var _ core.ObservableFilter = filter
	
	// Test that all interface methods work
	
	// Basic Filter
	if filter.Name() != "complex-filter" {
		t.Error("Name() not working")
	}
	
	// LifecycleFilter
	err := filter.OnStart(context.Background())
	if err != nil {
		t.Errorf("OnStart failed: %v", err)
	}
	
	// StatefulFilter
	filter.state["test"] = "value"
	state := filter.GetState()
	if state.(map[string]interface{})["test"] != "value" {
		t.Error("StatefulFilter methods not working")
	}
	
	// ConfigurableFilter
	config := types.FilterConfig{Name: "new-config"}
	err = filter.UpdateConfig(config)
	if err != nil {
		t.Errorf("UpdateConfig failed: %v", err)
	}
	
	// ObservableFilter
	metrics := filter.GetMetrics()
	if metrics.RequestsTotal != 50 {
		t.Error("ObservableFilter methods not working")
	}
}

// Benchmarks

func BenchmarkFilter_Process(b *testing.B) {
	filter := &mockFilterImpl{
		name: "bench-filter",
		processFunc: func(ctx context.Context, data []byte) (*types.FilterResult, error) {
			return types.ContinueWith(data), nil
		},
	}
	
	data := []byte("benchmark data")
	ctx := context.Background()
	
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		filter.Process(ctx, data)
	}
}

func BenchmarkFilter_GetStats(b *testing.B) {
	filter := &mockFilterImpl{
		name: "bench-filter",
		stats: types.FilterStatistics{
			BytesProcessed: 1000,
			PacketsProcessed: 100,
		},
	}
	
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		_ = filter.GetStats()
	}
}

func BenchmarkStatefulFilter_SaveState(b *testing.B) {
	filter := &mockStatefulFilter{
		mockFilterImpl: mockFilterImpl{name: "bench-filter"},
		state: map[string]interface{}{
			"key1": "value1",
			"key2": 42,
			"key3": true,
		},
	}
	
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		var buf strings.Builder
		filter.SaveState(&buf)
	}
}

func BenchmarkBatchFilter_ProcessBatch(b *testing.B) {
	filter := &mockBatchFilter{
		mockFilterImpl: mockFilterImpl{name: "bench-filter"},
	}
	
	batch := [][]byte{
		[]byte("item1"),
		[]byte("item2"),
		[]byte("item3"),
		[]byte("item4"),
		[]byte("item5"),
	}
	ctx := context.Background()
	
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		filter.ProcessBatch(ctx, batch)
	}
}