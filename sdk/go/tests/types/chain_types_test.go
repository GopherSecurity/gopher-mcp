package types_test

import (
	"testing"
	"time"

	"github.com/GopherSecurity/gopher-mcp/src/types"
)

func TestExecutionMode(t *testing.T) {
	tests := []struct {
		mode     types.ExecutionMode
		expected string
	}{
		{types.Sequential, "Sequential"},
		{types.Parallel, "Parallel"},
		{types.Pipeline, "Pipeline"},
		{types.Adaptive, "Adaptive"},
		{types.ExecutionMode(99), "ExecutionMode(99)"},
	}

	for _, tt := range tests {
		t.Run(tt.expected, func(t *testing.T) {
			result := tt.mode.String()
			if result != tt.expected {
				t.Errorf("ExecutionMode.String() = %s, want %s", result, tt.expected)
			}
		})
	}
}

func TestChainState(t *testing.T) {
	tests := []struct {
		state    types.ChainState
		expected string
	}{
		{types.Uninitialized, "Uninitialized"},
		{types.Ready, "Ready"},
		{types.Running, "Running"},
		{types.Stopped, "Stopped"},
		{types.ChainState(99), "ChainState(99)"},
	}

	for _, tt := range tests {
		t.Run(tt.expected, func(t *testing.T) {
			result := tt.state.String()
			if result != tt.expected {
				t.Errorf("ChainState.String() = %s, want %s", result, tt.expected)
			}
		})
	}
}

func TestChainState_Transitions(t *testing.T) {
	tests := []struct {
		name     string
		from     types.ChainState
		to       types.ChainState
		expected bool
	}{
		{"Uninitialized to Ready", types.Uninitialized, types.Ready, true},
		{"Uninitialized to Stopped", types.Uninitialized, types.Stopped, true},
		{"Uninitialized to Running", types.Uninitialized, types.Running, false},
		{"Ready to Running", types.Ready, types.Running, true},
		{"Ready to Stopped", types.Ready, types.Stopped, true},
		{"Ready to Uninitialized", types.Ready, types.Uninitialized, false},
		{"Running to Ready", types.Running, types.Ready, true},
		{"Running to Stopped", types.Running, types.Stopped, true},
		{"Running to Uninitialized", types.Running, types.Uninitialized, false},
		{"Stopped to Uninitialized", types.Stopped, types.Uninitialized, true},
		{"Stopped to Ready", types.Stopped, types.Ready, false},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			result := tt.from.CanTransitionTo(tt.to)
			if result != tt.expected {
				t.Errorf("CanTransitionTo(%v, %v) = %v, want %v", tt.from, tt.to, result, tt.expected)
			}
		})
	}
}

func TestChainState_Properties(t *testing.T) {
	t.Run("IsActive", func(t *testing.T) {
		tests := []struct {
			state    types.ChainState
			expected bool
		}{
			{types.Uninitialized, false},
			{types.Ready, true},
			{types.Running, true},
			{types.Stopped, false},
		}

		for _, tt := range tests {
			if tt.state.IsActive() != tt.expected {
				t.Errorf("%v.IsActive() = %v, want %v", tt.state, tt.state.IsActive(), tt.expected)
			}
		}
	})

	t.Run("IsTerminal", func(t *testing.T) {
		tests := []struct {
			state    types.ChainState
			expected bool
		}{
			{types.Uninitialized, false},
			{types.Ready, false},
			{types.Running, false},
			{types.Stopped, true},
		}

		for _, tt := range tests {
			if tt.state.IsTerminal() != tt.expected {
				t.Errorf("%v.IsTerminal() = %v, want %v", tt.state, tt.state.IsTerminal(), tt.expected)
			}
		}
	})
}

func TestChainEventType(t *testing.T) {
	tests := []struct {
		event    types.ChainEventType
		expected string
	}{
		{types.ChainStarted, "ChainStarted"},
		{types.ChainCompleted, "ChainCompleted"},
		{types.ChainError, "ChainError"},
		{types.FilterAdded, "FilterAdded"},
		{types.FilterRemoved, "FilterRemoved"},
		{types.StateChanged, "StateChanged"},
		{types.ChainEventType(99), "ChainEventType(99)"},
	}

	for _, tt := range tests {
		t.Run(tt.expected, func(t *testing.T) {
			result := tt.event.String()
			if result != tt.expected {
				t.Errorf("ChainEventType.String() = %s, want %s", result, tt.expected)
			}
		})
	}
}

func TestChainConfig_Validate(t *testing.T) {
	t.Run("Valid Config", func(t *testing.T) {
		config := types.ChainConfig{
			Name:           "test-chain",
			ExecutionMode:  types.Sequential,
			MaxConcurrency: 10,
			BufferSize:     1000,
			ErrorHandling:  "fail-fast",
			Timeout:        time.Second * 30,
		}

		errors := config.Validate()
		if len(errors) != 0 {
			t.Errorf("Valid config returned errors: %v", errors)
		}
	})

	t.Run("Empty Name", func(t *testing.T) {
		config := types.ChainConfig{
			Name:          "",
			ExecutionMode: types.Sequential,
		}

		errors := config.Validate()
		if len(errors) == 0 {
			t.Error("Expected error for empty name")
		}

		found := false
		for _, err := range errors {
			if err.Error() == "chain name cannot be empty" {
				found = true
				break
			}
		}
		if !found {
			t.Error("Expected 'chain name cannot be empty' error")
		}
	})

	t.Run("Invalid Parallel Config", func(t *testing.T) {
		config := types.ChainConfig{
			Name:           "parallel-chain",
			ExecutionMode:  types.Parallel,
			MaxConcurrency: 0, // Invalid for parallel mode
		}

		errors := config.Validate()
		if len(errors) == 0 {
			t.Error("Expected error for invalid parallel config")
		}

		found := false
		for _, err := range errors {
			if err.Error() == "max concurrency must be > 0 for parallel mode" {
				found = true
				break
			}
		}
		if !found {
			t.Error("Expected max_concurrency error for parallel mode")
		}
	})

	t.Run("Invalid Pipeline Config", func(t *testing.T) {
		config := types.ChainConfig{
			Name:          "pipeline-chain",
			ExecutionMode: types.Pipeline,
			BufferSize:    0, // Invalid for pipeline mode
		}

		errors := config.Validate()
		if len(errors) == 0 {
			t.Error("Expected error for invalid pipeline config")
		}

		found := false
		for _, err := range errors {
			if err.Error() == "buffer size must be > 0 for pipeline mode" {
				found = true
				break
			}
		}
		if !found {
			t.Error("Expected buffer_size error for pipeline mode")
		}
	})

	t.Run("Invalid Error Handling", func(t *testing.T) {
		config := types.ChainConfig{
			Name:          "test-chain",
			ExecutionMode: types.Sequential,
			ErrorHandling: "invalid-mode",
		}

		errors := config.Validate()
		if len(errors) == 0 {
			t.Error("Expected error for invalid error handling")
		}

		found := false
		for _, err := range errors {
			if err.Error() == "invalid error handling: invalid-mode (must be fail-fast, continue, or isolate)" {
				found = true
				break
			}
		}
		if !found {
			t.Error("Expected error for invalid error handling mode")
		}
	})

	t.Run("Negative Timeout", func(t *testing.T) {
		config := types.ChainConfig{
			Name:          "test-chain",
			ExecutionMode: types.Sequential,
			Timeout:       -1 * time.Second,
		}

		errors := config.Validate()
		if len(errors) == 0 {
			t.Error("Expected error for negative timeout")
		}

		found := false
		for _, err := range errors {
			if err.Error() == "timeout cannot be negative" {
				found = true
				break
			}
		}
		if !found {
			t.Error("Expected error for negative timeout")
		}
	})
}

func TestChainStatistics(t *testing.T) {
	t.Run("Basic Statistics", func(t *testing.T) {
		stats := types.ChainStatistics{
			TotalExecutions: 1000,
			SuccessCount:    950,
			ErrorCount:      50,
			AverageLatency:  100 * time.Millisecond,
			P50Latency:      50 * time.Millisecond,
			P90Latency:      150 * time.Millisecond,
			P99Latency:      300 * time.Millisecond,
			CurrentLoad:     5,
		}

		if stats.TotalExecutions != 1000 {
			t.Errorf("TotalExecutions = %d, want 1000", stats.TotalExecutions)
		}
		if stats.SuccessCount != 950 {
			t.Errorf("SuccessCount = %d, want 950", stats.SuccessCount)
		}
		if stats.ErrorCount != 50 {
			t.Errorf("ErrorCount = %d, want 50", stats.ErrorCount)
		}
		if stats.CurrentLoad != 5 {
			t.Errorf("CurrentLoad = %d, want 5", stats.CurrentLoad)
		}
	})

	t.Run("Latency Percentiles", func(t *testing.T) {
		stats := types.ChainStatistics{
			P50Latency: 50 * time.Millisecond,
			P90Latency: 150 * time.Millisecond,
			P99Latency: 300 * time.Millisecond,
		}

		if stats.P50Latency != 50*time.Millisecond {
			t.Errorf("P50Latency = %v, want 50ms", stats.P50Latency)
		}
		if stats.P90Latency != 150*time.Millisecond {
			t.Errorf("P90Latency = %v, want 150ms", stats.P90Latency)
		}
		if stats.P99Latency != 300*time.Millisecond {
			t.Errorf("P99Latency = %v, want 300ms", stats.P99Latency)
		}
	})
}

func TestChainEventData(t *testing.T) {
	eventData := types.ChainEventData{
		ChainName:      "TestChain",
		EventType:      types.ChainStarted,
		Timestamp:      time.Now(),
		OldState:       types.Ready,
		NewState:       types.Running,
		FilterName:     "TestFilter",
		FilterPosition: 0,
		Duration:       5 * time.Second,
		ProcessedBytes: 1024,
		Metadata: map[string]interface{}{
			"key": "value",
		},
	}

	if eventData.EventType != types.ChainStarted {
		t.Errorf("EventType = %v, want ChainStarted", eventData.EventType)
	}
	if eventData.ChainName != "TestChain" {
		t.Errorf("ChainName = %s, want TestChain", eventData.ChainName)
	}
	if eventData.OldState != types.Ready {
		t.Errorf("OldState = %v, want Ready", eventData.OldState)
	}
	if eventData.NewState != types.Running {
		t.Errorf("NewState = %v, want Running", eventData.NewState)
	}
	if eventData.FilterName != "TestFilter" {
		t.Errorf("FilterName = %s, want TestFilter", eventData.FilterName)
	}
	if eventData.FilterPosition != 0 {
		t.Errorf("FilterPosition = %d, want 0", eventData.FilterPosition)
	}
	if eventData.Duration != 5*time.Second {
		t.Errorf("Duration = %v, want 5s", eventData.Duration)
	}
	if eventData.ProcessedBytes != 1024 {
		t.Errorf("ProcessedBytes = %d, want 1024", eventData.ProcessedBytes)
	}
	if eventData.Metadata["key"] != "value" {
		t.Errorf("Metadata[key] = %v, want value", eventData.Metadata["key"])
	}
}

func TestChainEventArgs(t *testing.T) {
	args := types.ChainEventArgs{
		ChainName:   "chain-456",
		State:       types.Running,
		ExecutionID: "exec-123",
		Timestamp:   time.Now(),
		Metadata: map[string]interface{}{
			"duration": "5s",
			"status":   "success",
		},
	}

	if args.ChainName != "chain-456" {
		t.Errorf("ChainName = %s, want chain-456", args.ChainName)
	}
	if args.State != types.Running {
		t.Errorf("State = %v, want Running", args.State)
	}
	if args.ExecutionID != "exec-123" {
		t.Errorf("ExecutionID = %s, want exec-123", args.ExecutionID)
	}
	if args.Metadata["duration"] != "5s" {
		t.Errorf("Metadata[duration] = %v, want 5s", args.Metadata["duration"])
	}
	if args.Metadata["status"] != "success" {
		t.Errorf("Metadata[status] = %v, want success", args.Metadata["status"])
	}

	// Test NewChainEventArgs
	newArgs := types.NewChainEventArgs("test-chain", types.Ready, "exec-456")
	if newArgs == nil {
		t.Fatal("NewChainEventArgs returned nil")
	}
	if newArgs.ChainName != "test-chain" {
		t.Errorf("NewChainEventArgs ChainName = %s, want test-chain", newArgs.ChainName)
	}
	if newArgs.State != types.Ready {
		t.Errorf("NewChainEventArgs State = %v, want Ready", newArgs.State)
	}
	if newArgs.ExecutionID != "exec-456" {
		t.Errorf("NewChainEventArgs ExecutionID = %s, want exec-456", newArgs.ExecutionID)
	}
}

func TestChainConstants(t *testing.T) {
	// Test ExecutionMode constants
	if types.Sequential != 0 {
		t.Error("Sequential should be 0")
	}
	if types.Parallel != 1 {
		t.Error("Parallel should be 1")
	}
	if types.Pipeline != 2 {
		t.Error("Pipeline should be 2")
	}
	if types.Adaptive != 3 {
		t.Error("Adaptive should be 3")
	}

	// Test ChainState constants
	if types.Uninitialized != 0 {
		t.Error("Uninitialized should be 0")
	}
	if types.Ready != 1 {
		t.Error("Ready should be 1")
	}
	if types.Running != 2 {
		t.Error("Running should be 2")
	}
	if types.Stopped != 3 {
		t.Error("Stopped should be 3")
	}
}

func BenchmarkChainConfig_Validate(b *testing.B) {
	config := types.ChainConfig{
		Name:           "bench-chain",
		ExecutionMode:  types.Parallel,
		MaxConcurrency: 10,
		BufferSize:     1000,
		ErrorHandling:  "fail-fast",
		Timeout:        30 * time.Second,
	}

	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		_ = config.Validate()
	}
}

func BenchmarkChainState_CanTransitionTo(b *testing.B) {
	state := types.Ready

	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		_ = state.CanTransitionTo(types.Running)
	}
}

func BenchmarkChainState_IsActive(b *testing.B) {
	state := types.Running

	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		_ = state.IsActive()
	}
}
