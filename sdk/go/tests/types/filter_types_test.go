package types_test

import (
	"fmt"
	"testing"
	"time"

	"github.com/GopherSecurity/gopher-mcp/src/types"
)

// Test 1: FilterStatus String
func TestFilterStatus_String(t *testing.T) {
	tests := []struct {
		status   types.FilterStatus
		expected string
	}{
		{types.Continue, "Continue"},
		{types.StopIteration, "StopIteration"},
		{types.Error, "Error"},
		{types.NeedMoreData, "NeedMoreData"},
		{types.Buffered, "Buffered"},
		{types.FilterStatus(99), "FilterStatus(99)"},
	}

	for _, tt := range tests {
		t.Run(tt.expected, func(t *testing.T) {
			result := tt.status.String()
			if result != tt.expected {
				t.Errorf("FilterStatus.String() = %s, want %s", result, tt.expected)
			}
		})
	}
}

// Test 2: FilterStatus IsTerminal
func TestFilterStatus_IsTerminal(t *testing.T) {
	tests := []struct {
		status   types.FilterStatus
		terminal bool
	}{
		{types.Continue, false},
		{types.StopIteration, true},
		{types.Error, true},
		{types.NeedMoreData, false},
		{types.Buffered, false},
	}

	for _, tt := range tests {
		t.Run(tt.status.String(), func(t *testing.T) {
			result := tt.status.IsTerminal()
			if result != tt.terminal {
				t.Errorf("%v.IsTerminal() = %v, want %v", tt.status, result, tt.terminal)
			}
		})
	}
}

// Test 3: FilterStatus IsSuccess
func TestFilterStatus_IsSuccess(t *testing.T) {
	tests := []struct {
		status  types.FilterStatus
		success bool
	}{
		{types.Continue, true},
		{types.StopIteration, true},
		{types.Error, false},
		{types.NeedMoreData, false},
		{types.Buffered, true},
	}

	for _, tt := range tests {
		t.Run(tt.status.String(), func(t *testing.T) {
			result := tt.status.IsSuccess()
			if result != tt.success {
				t.Errorf("%v.IsSuccess() = %v, want %v", tt.status, result, tt.success)
			}
		})
	}
}

// Test 4: FilterPosition String
func TestFilterPosition_String(t *testing.T) {
	tests := []struct {
		position types.FilterPosition
		expected string
	}{
		{types.First, "First"},
		{types.Last, "Last"},
		{types.Before, "Before"},
		{types.After, "After"},
		{types.FilterPosition(99), "FilterPosition(99)"},
	}

	for _, tt := range tests {
		t.Run(tt.expected, func(t *testing.T) {
			result := tt.position.String()
			if result != tt.expected {
				t.Errorf("FilterPosition.String() = %s, want %s", result, tt.expected)
			}
		})
	}
}

// Test 5: FilterPosition IsValid
func TestFilterPosition_IsValid(t *testing.T) {
	tests := []struct {
		position types.FilterPosition
		valid    bool
	}{
		{types.First, true},
		{types.Last, true},
		{types.Before, true},
		{types.After, true},
		{types.FilterPosition(99), false},
	}

	for _, tt := range tests {
		t.Run(tt.position.String(), func(t *testing.T) {
			result := tt.position.IsValid()
			if result != tt.valid {
				t.Errorf("%v.IsValid() = %v, want %v", tt.position, result, tt.valid)
			}
		})
	}
}

// Test 6: FilterError Error method
func TestFilterError_Error(t *testing.T) {
	tests := []struct {
		err      types.FilterError
		expected string
	}{
		{types.InvalidConfiguration, "invalid filter configuration"},
		{types.FilterNotFound, "filter not found"},
		{types.FilterAlreadyExists, "filter already exists"},
		{types.InitializationFailed, "filter initialization failed"},
		{types.ProcessingFailed, "filter processing failed"},
		{types.ChainProcessingError, "filter chain error"},
		{types.BufferOverflow, "buffer overflow"},
		{types.Timeout, "operation timeout"},
		{types.ResourceExhausted, "resource exhausted"},
		{types.TooManyRequests, "too many requests"},
		{types.AuthenticationFailed, "authentication failed"},
		{types.ServiceUnavailable, "service unavailable"},
		{types.FilterError(9999), "filter error: 9999"},
	}

	for _, tt := range tests {
		t.Run(tt.expected, func(t *testing.T) {
			result := tt.err.Error()
			if result != tt.expected {
				t.Errorf("FilterError.Error() = %s, want %s", result, tt.expected)
			}
		})
	}
}

// Test 7: FilterError IsRetryable
func TestFilterError_IsRetryable(t *testing.T) {
	tests := []struct {
		err       types.FilterError
		retryable bool
	}{
		{types.Timeout, true},
		{types.ResourceExhausted, true},
		{types.TooManyRequests, true},
		{types.ServiceUnavailable, true},
		{types.InvalidConfiguration, false},
		{types.FilterNotFound, false},
		{types.FilterAlreadyExists, false},
		{types.InitializationFailed, false},
		{types.BufferOverflow, false},
		{types.AuthenticationFailed, false},
	}

	for _, tt := range tests {
		t.Run(tt.err.Error(), func(t *testing.T) {
			result := tt.err.IsRetryable()
			if result != tt.retryable {
				t.Errorf("%v.IsRetryable() = %v, want %v", tt.err, result, tt.retryable)
			}
		})
	}
}

// Test 8: FilterError Code
func TestFilterError_Code(t *testing.T) {
	tests := []struct {
		err  types.FilterError
		code int
	}{
		{types.InvalidConfiguration, 1001},
		{types.FilterNotFound, 1002},
		{types.FilterAlreadyExists, 1003},
		{types.InitializationFailed, 1004},
		{types.ProcessingFailed, 1005},
		{types.ChainProcessingError, 1006},
		{types.BufferOverflow, 1007},
		{types.Timeout, 1010},
		{types.ResourceExhausted, 1011},
		{types.TooManyRequests, 1018},
		{types.AuthenticationFailed, 1019},
		{types.ServiceUnavailable, 1021},
	}

	for _, tt := range tests {
		t.Run(tt.err.Error(), func(t *testing.T) {
			result := tt.err.Code()
			if result != tt.code {
				t.Errorf("%v.Code() = %d, want %d", tt.err, result, tt.code)
			}
		})
	}
}

// Test 9: FilterLayer String
func TestFilterLayer_String(t *testing.T) {
	tests := []struct {
		layer    types.FilterLayer
		expected string
	}{
		{types.Transport, "Transport (L4)"},
		{types.Session, "Session (L5)"},
		{types.Presentation, "Presentation (L6)"},
		{types.Application, "Application (L7)"},
		{types.Custom, "Custom"},
		{types.FilterLayer(50), "FilterLayer(50)"},
	}

	for _, tt := range tests {
		t.Run(tt.expected, func(t *testing.T) {
			result := tt.layer.String()
			if result != tt.expected {
				t.Errorf("FilterLayer.String() = %s, want %s", result, tt.expected)
			}
		})
	}
}

// Test 10: FilterLayer IsValid
func TestFilterLayer_IsValid(t *testing.T) {
	tests := []struct {
		layer types.FilterLayer
		valid bool
	}{
		{types.Transport, true},
		{types.Session, true},
		{types.Presentation, true},
		{types.Application, true},
		{types.Custom, true},
		{types.FilterLayer(50), false},
	}

	for _, tt := range tests {
		t.Run(tt.layer.String(), func(t *testing.T) {
			result := tt.layer.IsValid()
			if result != tt.valid {
				t.Errorf("%v.IsValid() = %v, want %v", tt.layer, result, tt.valid)
			}
		})
	}
}

// Batch 1 is complete above (10 tests)
// Now starting batch 2

// Test 11: FilterLayer OSILayer
func TestFilterLayer_OSILayer(t *testing.T) {
	tests := []struct {
		layer    types.FilterLayer
		expected int
	}{
		{types.Transport, 4},
		{types.Session, 5},
		{types.Presentation, 6},
		{types.Application, 7},
		{types.Custom, 0},
	}

	for _, tt := range tests {
		t.Run(tt.layer.String(), func(t *testing.T) {
			result := tt.layer.OSILayer()
			if result != tt.expected {
				t.Errorf("%v.OSILayer() = %d, want %d", tt.layer, result, tt.expected)
			}
		})
	}
}

// Test 12: FilterConfig Basic
func TestFilterConfig_Basic(t *testing.T) {
	config := types.FilterConfig{
		Name:     "test-filter",
		Type:     "http",
		Layer:    types.Application,
		Enabled:  true,
		Priority: 10,
		Settings: map[string]interface{}{
			"key": "value",
		},
	}

	if config.Name != "test-filter" {
		t.Errorf("Name = %s, want test-filter", config.Name)
	}
	if config.Type != "http" {
		t.Errorf("Type = %s, want http", config.Type)
	}
	if !config.Enabled {
		t.Error("Filter should be enabled")
	}
	if config.Priority != 10 {
		t.Errorf("Priority = %d, want 10", config.Priority)
	}
	if config.Settings["key"] != "value" {
		t.Errorf("Settings[key] = %v, want value", config.Settings["key"])
	}
}

// Test 13: FilterConfig Validate Valid
func TestFilterConfig_ValidateValid(t *testing.T) {
	config := types.FilterConfig{
		Name:          "valid-filter",
		Type:          "auth",
		Enabled:       true,
		Priority:      100,
		MaxBufferSize: 2048,
		TimeoutMs:     5000,
	}

	errors := config.Validate()
	if len(errors) != 0 {
		t.Errorf("Valid config returned errors: %v", errors)
	}
}

// Test 14: FilterConfig Validate Empty Name
func TestFilterConfig_ValidateEmptyName(t *testing.T) {
	config := types.FilterConfig{
		Name: "",
		Type: "test",
	}

	errors := config.Validate()
	if len(errors) == 0 {
		t.Error("Expected error for empty name")
	}

	found := false
	for _, err := range errors {
		if err.Error() == "filter name cannot be empty" {
			found = true
			break
		}
	}
	if !found {
		t.Error("Expected 'filter name cannot be empty' error")
	}
}

// Test 15: FilterConfig Validate Empty Type
func TestFilterConfig_ValidateEmptyType(t *testing.T) {
	config := types.FilterConfig{
		Name: "test-filter",
		Type: "",
	}

	errors := config.Validate()
	if len(errors) == 0 {
		t.Error("Expected error for empty type")
	}

	found := false
	for _, err := range errors {
		if err.Error() == "filter type cannot be empty" {
			found = true
			break
		}
	}
	if !found {
		t.Error("Expected 'filter type cannot be empty' error")
	}
}

// Test 16: FilterConfig Validate Invalid Priority
func TestFilterConfig_ValidateInvalidPriority(t *testing.T) {
	config := types.FilterConfig{
		Name:     "test-filter",
		Type:     "test",
		Priority: 1001,
	}

	errors := config.Validate()
	if len(errors) == 0 {
		t.Error("Expected error for invalid priority")
	}
}

// Test 17: FilterConfig Validate Negative Timeout
func TestFilterConfig_ValidateNegativeTimeout(t *testing.T) {
	config := types.FilterConfig{
		Name:      "test-filter",
		Type:      "test",
		TimeoutMs: -100,
	}

	errors := config.Validate()
	if len(errors) == 0 {
		t.Error("Expected error for negative timeout")
	}
}

// Test 18: FilterConfig Validate Negative Buffer
func TestFilterConfig_ValidateNegativeBuffer(t *testing.T) {
	config := types.FilterConfig{
		Name:          "test-filter",
		Type:          "test",
		MaxBufferSize: -100,
	}

	errors := config.Validate()
	if len(errors) == 0 {
		t.Error("Expected error for negative buffer size")
	}
}

// Test 19: FilterStatistics Basic
func TestFilterStatistics_Basic(t *testing.T) {
	stats := types.FilterStatistics{
		BytesProcessed:          1024 * 1024,
		PacketsProcessed:        1000,
		ProcessCount:            500,
		ErrorCount:              5,
		ProcessingTimeUs:        1000000,
		AverageProcessingTimeUs: 2000,
		MaxProcessingTimeUs:     10000,
		MinProcessingTimeUs:     100,
		CurrentBufferUsage:      4096,
		PeakBufferUsage:         8192,
		ThroughputBps:           1024 * 100,
		ErrorRate:               1.0,
	}

	if stats.BytesProcessed != 1024*1024 {
		t.Errorf("BytesProcessed = %d, want %d", stats.BytesProcessed, 1024*1024)
	}
	if stats.PacketsProcessed != 1000 {
		t.Errorf("PacketsProcessed = %d, want 1000", stats.PacketsProcessed)
	}
	if stats.ErrorCount != 5 {
		t.Errorf("ErrorCount = %d, want 5", stats.ErrorCount)
	}
	if stats.ErrorRate != 1.0 {
		t.Errorf("ErrorRate = %f, want 1.0", stats.ErrorRate)
	}
}

// Test 20: FilterStatistics String
func TestFilterStatistics_String(t *testing.T) {
	stats := types.FilterStatistics{
		ProcessCount: 100,
		ErrorCount:   5,
	}

	str := stats.String()
	if str == "" {
		t.Error("String() should return non-empty string")
	}
}

// Batch 2 is complete above (10 tests)
// Now starting batch 3

// Test 21: FilterStatistics CustomMetrics
func TestFilterStatistics_CustomMetrics(t *testing.T) {
	stats := types.FilterStatistics{
		CustomMetrics: map[string]interface{}{
			"custom_counter": 42,
			"custom_gauge":   3.14,
		},
	}

	if stats.CustomMetrics["custom_counter"] != 42 {
		t.Errorf("CustomMetrics[custom_counter] = %v, want 42", stats.CustomMetrics["custom_counter"])
	}
	if stats.CustomMetrics["custom_gauge"] != 3.14 {
		t.Errorf("CustomMetrics[custom_gauge] = %v, want 3.14", stats.CustomMetrics["custom_gauge"])
	}
}

// Test 22: FilterResult Success
func TestFilterResult_Success(t *testing.T) {
	result := types.FilterResult{
		Status:   types.Continue,
		Data:     []byte("processed data"),
		Error:    nil,
		Metadata: map[string]interface{}{"key": "value"},
	}

	if result.Status != types.Continue {
		t.Errorf("Status = %v, want Continue", result.Status)
	}
	if string(result.Data) != "processed data" {
		t.Errorf("Data = %s, want 'processed data'", result.Data)
	}
	if result.Error != nil {
		t.Errorf("Error should be nil, got %v", result.Error)
	}
	if result.Metadata["key"] != "value" {
		t.Errorf("Metadata[key] = %v, want value", result.Metadata["key"])
	}
}

// Test 23: FilterResult Error
func TestFilterResult_Error(t *testing.T) {
	errMsg := "processing failed"
	result := types.FilterResult{
		Status: types.Error,
		Error:  fmt.Errorf(errMsg),
	}

	if result.Status != types.Error {
		t.Errorf("Status = %v, want Error", result.Status)
	}
	if result.Error == nil {
		t.Error("Error should not be nil")
	}
	if result.Error.Error() != errMsg {
		t.Errorf("Error message = %s, want %s", result.Error.Error(), errMsg)
	}
}

// Test 24: FilterResult IsSuccess
func TestFilterResult_IsSuccess(t *testing.T) {
	successResult := types.FilterResult{
		Status: types.Continue,
	}
	if !successResult.IsSuccess() {
		t.Error("Continue status should be success")
	}

	errorResult := types.FilterResult{
		Status: types.Error,
	}
	if errorResult.IsSuccess() {
		t.Error("Error status should not be success")
	}
}

// Test 25: FilterResult IsError
func TestFilterResult_IsError(t *testing.T) {
	errorResult := types.FilterResult{
		Status: types.Error,
	}
	if !errorResult.IsError() {
		t.Error("Error status should be error")
	}

	successResult := types.FilterResult{
		Status: types.Continue,
	}
	if successResult.IsError() {
		t.Error("Continue status should not be error")
	}
}

// Test 26: FilterResult Duration
func TestFilterResult_Duration(t *testing.T) {
	start := time.Now()
	end := start.Add(100 * time.Millisecond)

	result := types.FilterResult{
		StartTime: start,
		EndTime:   end,
	}

	duration := result.Duration()
	expected := 100 * time.Millisecond
	if duration != expected {
		t.Errorf("Duration() = %v, want %v", duration, expected)
	}

	// Test with zero times
	emptyResult := types.FilterResult{}
	if emptyResult.Duration() != 0 {
		t.Error("Duration() with zero times should return 0")
	}
}

// Test 27: FilterResult Validate
func TestFilterResult_Validate(t *testing.T) {
	t.Run("Valid Result", func(t *testing.T) {
		result := types.FilterResult{
			Status: types.Continue,
		}
		if err := result.Validate(); err != nil {
			t.Errorf("Valid result validation failed: %v", err)
		}
	})

	t.Run("Error Status Without Error", func(t *testing.T) {
		result := types.FilterResult{
			Status: types.Error,
			Error:  nil,
		}
		if err := result.Validate(); err == nil {
			t.Error("Expected validation error for error status without error field")
		}
	})

	t.Run("Invalid Status", func(t *testing.T) {
		result := types.FilterResult{
			Status: types.FilterStatus(100),
		}
		if err := result.Validate(); err == nil {
			t.Error("Expected validation error for invalid status")
		}
	})
}

// Test 28: FilterResult Release
func TestFilterResult_Release(t *testing.T) {
	result := &types.FilterResult{
		Status:   types.Error,
		Data:     []byte("test"),
		Error:    fmt.Errorf("test error"),
		Metadata: map[string]interface{}{"key": "value"},
	}

	result.Release()
	// After release, result should be reset
	if result.Status != types.Continue {
		t.Error("Status should be reset to Continue after Release")
	}
	if result.Data != nil {
		t.Error("Data should be nil after Release")
	}
	if result.Error != nil {
		t.Error("Error should be nil after Release")
	}
}

// Test 29: Success Helper Function
func TestSuccess_Helper(t *testing.T) {
	data := []byte("success data")
	result := types.Success(data)

	if result.Status != types.Continue {
		t.Errorf("Status = %v, want Continue", result.Status)
	}
	if string(result.Data) != "success data" {
		t.Errorf("Data = %s, want 'success data'", result.Data)
	}
}

// Test 30: ErrorResult Helper Function
func TestErrorResult_Helper(t *testing.T) {
	err := fmt.Errorf("test error")
	result := types.ErrorResult(err, types.ProcessingFailed)

	if result.Status != types.Error {
		t.Errorf("Status = %v, want Error", result.Status)
	}
	if result.Error == nil {
		t.Error("Error should not be nil")
	}
	if code, ok := result.Metadata["error_code"].(int); ok {
		if code != types.ProcessingFailed.Code() {
			t.Errorf("Error code = %d, want %d", code, types.ProcessingFailed.Code())
		}
	} else {
		t.Error("Error code not found in metadata")
	}
}

// Batch 3 is complete above (10 tests)
// Now starting batch 4

// Test 31: ContinueWith Helper
func TestContinueWith_Helper(t *testing.T) {
	data := []byte("continue data")
	result := types.ContinueWith(data)

	if result.Status != types.Continue {
		t.Errorf("Status = %v, want Continue", result.Status)
	}
	if string(result.Data) != "continue data" {
		t.Errorf("Data = %s, want 'continue data'", result.Data)
	}
}

// Test 32: Blocked Helper
func TestBlocked_Helper(t *testing.T) {
	reason := "Security violation"
	result := types.Blocked(reason)

	if result.Status != types.StopIteration {
		t.Errorf("Status = %v, want StopIteration", result.Status)
	}
	if !result.StopChain {
		t.Error("StopChain should be true")
	}
	if blockedReason, ok := result.Metadata["blocked_reason"].(string); ok {
		if blockedReason != reason {
			t.Errorf("Blocked reason = %s, want %s", blockedReason, reason)
		}
	} else {
		t.Error("Blocked reason not found in metadata")
	}
}

// Test 33: StopIterationResult Helper
func TestStopIterationResult_Helper(t *testing.T) {
	result := types.StopIterationResult()

	if result.Status != types.StopIteration {
		t.Errorf("Status = %v, want StopIteration", result.Status)
	}
	if !result.StopChain {
		t.Error("StopChain should be true")
	}
}

// Test 34: GetResult Pool
func TestGetResult_Pool(t *testing.T) {
	result := types.GetResult()

	if result == nil {
		t.Fatal("GetResult() returned nil")
	}
	if result.Status != types.Continue {
		t.Errorf("Status = %v, want Continue", result.Status)
	}
	if result.Metadata == nil {
		t.Error("Metadata should be initialized")
	}
}

// Test 35: FilterEventArgs Basic
func TestFilterEventArgs_Basic(t *testing.T) {
	args := types.FilterEventArgs{
		FilterName: "test-filter",
		FilterType: "http",
		Timestamp:  time.Now(),
		Data: map[string]interface{}{
			"config": "test",
		},
	}

	if args.FilterName != "test-filter" {
		t.Errorf("FilterName = %s, want test-filter", args.FilterName)
	}
	if args.FilterType != "http" {
		t.Errorf("FilterType = %s, want http", args.FilterType)
	}
	if args.Data["config"] != "test" {
		t.Errorf("Data[config] = %v, want test", args.Data["config"])
	}
}

// Test 36: FilterDataEventArgs Basic
func TestFilterDataEventArgs_Basic(t *testing.T) {
	args := types.FilterDataEventArgs{
		FilterEventArgs: types.FilterEventArgs{
			FilterName: "test-filter",
			FilterType: "http",
			Timestamp:  time.Now(),
			Data: map[string]interface{}{
				"source": "client",
			},
		},
		Buffer: []byte("test data"),
		Offset: 0,
		Length: 9,
	}

	if args.FilterName != "test-filter" {
		t.Errorf("FilterName = %s, want test-filter", args.FilterName)
	}
	if args.FilterType != "http" {
		t.Errorf("FilterType = %s, want http", args.FilterType)
	}
	if string(args.Buffer) != "test data" {
		t.Errorf("Buffer = %s, want 'test data'", args.Buffer)
	}
	if args.Data["source"] != "client" {
		t.Errorf("Data[source] = %v, want client", args.Data["source"])
	}
}

// Test 37: FilterConstants Status
func TestFilterConstants_Status(t *testing.T) {
	if types.Continue != 0 {
		t.Error("Continue should be 0")
	}
	if types.StopIteration != 1 {
		t.Error("StopIteration should be 1")
	}
	if types.Error != 2 {
		t.Error("Error should be 2")
	}
	if types.NeedMoreData != 3 {
		t.Error("NeedMoreData should be 3")
	}
	if types.Buffered != 4 {
		t.Error("Buffered should be 4")
	}
}

// Test 38: FilterConstants Position
func TestFilterConstants_Position(t *testing.T) {
	if types.First != 0 {
		t.Error("First should be 0")
	}
	if types.Last != 1 {
		t.Error("Last should be 1")
	}
	if types.Before != 2 {
		t.Error("Before should be 2")
	}
	if types.After != 3 {
		t.Error("After should be 3")
	}
}

// Test 39: FilterConstants Error
func TestFilterConstants_Error(t *testing.T) {
	if types.InvalidConfiguration != 1001 {
		t.Error("InvalidConfiguration should be 1001")
	}
	if types.FilterNotFound != 1002 {
		t.Error("FilterNotFound should be 1002")
	}
	if types.FilterAlreadyExists != 1003 {
		t.Error("FilterAlreadyExists should be 1003")
	}
	if types.InitializationFailed != 1004 {
		t.Error("InitializationFailed should be 1004")
	}
	if types.ProcessingFailed != 1005 {
		t.Error("ProcessingFailed should be 1005")
	}
}

// Test 40: FilterConstants Layer
func TestFilterConstants_Layer(t *testing.T) {
	if types.Transport != 4 {
		t.Error("Transport should be 4")
	}
	if types.Session != 5 {
		t.Error("Session should be 5")
	}
	if types.Presentation != 6 {
		t.Error("Presentation should be 6")
	}
	if types.Application != 7 {
		t.Error("Application should be 7")
	}
	if types.Custom != 99 {
		t.Error("Custom should be 99")
	}
}

// Batch 4 is complete above (10 tests)
// Now benchmarks

func BenchmarkFilterError_Error(b *testing.B) {
	err := types.ProcessingFailed

	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		_ = err.Error()
	}
}

func BenchmarkFilterError_IsRetryable(b *testing.B) {
	err := types.Timeout

	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		_ = err.IsRetryable()
	}
}

func BenchmarkFilterConfig_Validate(b *testing.B) {
	config := types.FilterConfig{
		Name:     "bench-filter",
		Type:     "http",
		Enabled:  true,
		Priority: 100,
		Settings: map[string]interface{}{
			"key1": "value1",
			"key2": 42,
		},
	}

	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		_ = config.Validate()
	}
}

func BenchmarkFilterStatistics_String(b *testing.B) {
	stats := types.FilterStatistics{
		BytesProcessed:   1024 * 1024,
		PacketsProcessed: 1000,
		ProcessCount:     500,
		ErrorCount:       5,
	}

	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		_ = stats.String()
	}
}

func BenchmarkFilterResult_Duration(b *testing.B) {
	start := time.Now()
	result := types.FilterResult{
		StartTime: start,
		EndTime:   start.Add(100 * time.Millisecond),
	}

	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		_ = result.Duration()
	}
}

func BenchmarkGetResult_Pool(b *testing.B) {
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		result := types.GetResult()
		result.Release()
	}
}
