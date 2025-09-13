package core_test

import (
	"bytes"
	"context"
	"errors"
	"sync"
	"sync/atomic"
	"testing"
	"time"

	"github.com/GopherSecurity/gopher-mcp/src/core"
	"github.com/GopherSecurity/gopher-mcp/src/types"
)

// Test 1: Basic FilterFunc implementation
func TestFilterFunc_Basic(t *testing.T) {
	// Create a simple filter function
	called := false
	filter := core.FilterFunc(func(ctx context.Context, data []byte) (*types.FilterResult, error) {
		called = true
		return types.ContinueWith(data), nil
	})
	
	// Verify it implements Filter interface
	var _ core.Filter = filter
	
	// Test Process
	result, err := filter.Process(context.Background(), []byte("test"))
	if err != nil {
		t.Fatalf("Process failed: %v", err)
	}
	if !called {
		t.Error("Filter function not called")
	}
	if string(result.Data) != "test" {
		t.Errorf("Result = %s, want test", result.Data)
	}
	
	// Test Name (should return generic name)
	if filter.Name() != "filter-func" {
		t.Errorf("Name() = %s, want filter-func", filter.Name())
	}
	
	// Test Type (should return generic type)
	if filter.Type() != "function" {
		t.Errorf("Type() = %s, want function", filter.Type())
	}
}

// Test 2: FilterFunc with data transformation
func TestFilterFunc_Transform(t *testing.T) {
	// Create uppercase filter
	filter := core.FilterFunc(func(ctx context.Context, data []byte) (*types.FilterResult, error) {
		upperData := bytes.ToUpper(data)
		return types.ContinueWith(upperData), nil
	})
	
	// Test transformation
	input := []byte("hello world")
	result, err := filter.Process(context.Background(), input)
	if err != nil {
		t.Fatalf("Process failed: %v", err)
	}
	
	expected := "HELLO WORLD"
	if string(result.Data) != expected {
		t.Errorf("Result = %s, want %s", result.Data, expected)
	}
}

// Test 3: FilterFunc with error handling
func TestFilterFunc_Error(t *testing.T) {
	testErr := errors.New("processing error")
	
	filter := core.FilterFunc(func(ctx context.Context, data []byte) (*types.FilterResult, error) {
		return nil, testErr
	})
	
	_, err := filter.Process(context.Background(), []byte("test"))
	if err != testErr {
		t.Errorf("Process error = %v, want %v", err, testErr)
	}
}

// Test 4: FilterFunc with context cancellation
func TestFilterFunc_ContextCancellation(t *testing.T) {
	filter := core.FilterFunc(func(ctx context.Context, data []byte) (*types.FilterResult, error) {
		// Check context
		select {
		case <-ctx.Done():
			return nil, ctx.Err()
		default:
			return types.ContinueWith(data), nil
		}
	})
	
	// Test with cancelled context
	ctx, cancel := context.WithCancel(context.Background())
	cancel() // Cancel immediately
	
	_, err := filter.Process(ctx, []byte("test"))
	if err == nil {
		t.Error("Process should return error for cancelled context")
	}
}

// Test 5: FilterFunc Initialize and Close (no-op)
func TestFilterFunc_InitializeClose(t *testing.T) {
	filter := core.FilterFunc(func(ctx context.Context, data []byte) (*types.FilterResult, error) {
		return types.ContinueWith(data), nil
	})
	
	// Initialize should not fail (no-op)
	config := types.FilterConfig{Name: "test", Type: "test"}
	err := filter.Initialize(config)
	if err != nil {
		t.Errorf("Initialize returned unexpected error: %v", err)
	}
	
	// Close should not fail (no-op)
	err = filter.Close()
	if err != nil {
		t.Errorf("Close returned unexpected error: %v", err)
	}
	
	// Should still work after Close
	result, err := filter.Process(context.Background(), []byte("test"))
	if err != nil {
		t.Errorf("Process failed after Close: %v", err)
	}
	if string(result.Data) != "test" {
		t.Error("Filter not working after Close")
	}
}

// Test 6: FilterFunc GetStats (always empty)
func TestFilterFunc_GetStats(t *testing.T) {
	filter := core.FilterFunc(func(ctx context.Context, data []byte) (*types.FilterResult, error) {
		return types.ContinueWith(data), nil
	})
	
	// Process some data
	for i := 0; i < 10; i++ {
		filter.Process(context.Background(), []byte("test"))
	}
	
	// Stats should still be empty (FilterFunc doesn't track stats)
	stats := filter.GetStats()
	if stats.BytesProcessed != 0 {
		t.Error("FilterFunc should not track statistics")
	}
	if stats.ProcessCount != 0 {
		t.Error("FilterFunc should not track process count")
	}
}

// Test 7: WrapFilterFunc with custom name and type
func TestWrapFilterFunc(t *testing.T) {
	name := "custom-filter"
	filterType := "transformation"
	
	filter := core.WrapFilterFunc(name, filterType,
		func(ctx context.Context, data []byte) (*types.FilterResult, error) {
			reversed := make([]byte, len(data))
			for i := range data {
				reversed[i] = data[len(data)-1-i]
			}
			return types.ContinueWith(reversed), nil
		})
	
	// Check name and type
	if filter.Name() != name {
		t.Errorf("Name() = %s, want %s", filter.Name(), name)
	}
	if filter.Type() != filterType {
		t.Errorf("Type() = %s, want %s", filter.Type(), filterType)
	}
	
	// Test processing
	result, err := filter.Process(context.Background(), []byte("hello"))
	if err != nil {
		t.Fatalf("Process failed: %v", err)
	}
	if string(result.Data) != "olleh" {
		t.Errorf("Result = %s, want olleh", result.Data)
	}
	
	// Stats should be tracked for wrapped functions
	stats := filter.GetStats()
	if stats.BytesProcessed != 5 {
		t.Errorf("BytesProcessed = %d, want 5", stats.BytesProcessed)
	}
	if stats.ProcessCount != 1 {
		t.Errorf("ProcessCount = %d, want 1", stats.ProcessCount)
	}
}

// Test 8: WrapFilterFunc with error tracking
func TestWrapFilterFunc_ErrorTracking(t *testing.T) {
	errorCount := 0
	filter := core.WrapFilterFunc("error-filter", "test",
		func(ctx context.Context, data []byte) (*types.FilterResult, error) {
			if string(data) == "error" {
				errorCount++
				return nil, errors.New("triggered error")
			}
			return types.ContinueWith(data), nil
		})
	
	// Process without error
	filter.Process(context.Background(), []byte("ok"))
	
	// Process with error
	filter.Process(context.Background(), []byte("error"))
	
	// Process without error again
	filter.Process(context.Background(), []byte("ok"))
	
	// Check stats
	stats := filter.GetStats()
	if stats.ProcessCount != 3 {
		t.Errorf("ProcessCount = %d, want 3", stats.ProcessCount)
	}
	if stats.ErrorCount != 1 {
		t.Errorf("ErrorCount = %d, want 1", stats.ErrorCount)
	}
	if errorCount != 1 {
		t.Errorf("Function called with error %d times, want 1", errorCount)
	}
}

// Test 9: WrapFilterFunc after Close
func TestWrapFilterFunc_AfterClose(t *testing.T) {
	filter := core.WrapFilterFunc("closeable", "test",
		func(ctx context.Context, data []byte) (*types.FilterResult, error) {
			return types.ContinueWith(data), nil
		})
	
	// Process before close
	result, err := filter.Process(context.Background(), []byte("before"))
	if err != nil {
		t.Fatalf("Process failed before close: %v", err)
	}
	if string(result.Data) != "before" {
		t.Error("Incorrect result before close")
	}
	
	// Close the filter
	err = filter.Close()
	if err != nil {
		t.Fatalf("Close failed: %v", err)
	}
	
	// Process after close should fail
	_, err = filter.Process(context.Background(), []byte("after"))
	if err == nil {
		t.Error("Process should fail after Close")
	}
}

// Test 10: Concurrent FilterFunc usage
func TestFilterFunc_Concurrent(t *testing.T) {
	var counter int32
	
	filter := core.FilterFunc(func(ctx context.Context, data []byte) (*types.FilterResult, error) {
		atomic.AddInt32(&counter, 1)
		// Simulate some work
		time.Sleep(time.Microsecond)
		return types.ContinueWith(data), nil
	})
	
	var wg sync.WaitGroup
	numGoroutines := 10
	callsPerGoroutine := 100
	
	for i := 0; i < numGoroutines; i++ {
		wg.Add(1)
		go func(id int) {
			defer wg.Done()
			for j := 0; j < callsPerGoroutine; j++ {
				data := []byte(string(rune('A' + id)))
				filter.Process(context.Background(), data)
			}
		}(i)
	}
	
	wg.Wait()
	
	expectedCalls := int32(numGoroutines * callsPerGoroutine)
	if counter != expectedCalls {
		t.Errorf("Counter = %d, want %d", counter, expectedCalls)
	}
}

// Test wrapped FilterFunc concurrent usage
func TestWrapFilterFunc_Concurrent(t *testing.T) {
	var counter int32
	
	filter := core.WrapFilterFunc("concurrent", "test",
		func(ctx context.Context, data []byte) (*types.FilterResult, error) {
			atomic.AddInt32(&counter, 1)
			return types.ContinueWith(data), nil
		})
	
	var wg sync.WaitGroup
	numGoroutines := 10
	callsPerGoroutine := 50
	
	for i := 0; i < numGoroutines; i++ {
		wg.Add(1)
		go func(id int) {
			defer wg.Done()
			for j := 0; j < callsPerGoroutine; j++ {
				data := []byte{byte(id), byte(j)}
				filter.Process(context.Background(), data)
			}
		}(i)
	}
	
	wg.Wait()
	
	// Check counter
	expectedCalls := int32(numGoroutines * callsPerGoroutine)
	if counter != expectedCalls {
		t.Errorf("Counter = %d, want %d", counter, expectedCalls)
	}
	
	// Check stats
	stats := filter.GetStats()
	if stats.ProcessCount != uint64(expectedCalls) {
		t.Errorf("ProcessCount = %d, want %d", stats.ProcessCount, expectedCalls)
	}
}

// Test chaining multiple FilterFuncs
func TestFilterFunc_Chaining(t *testing.T) {
	// Create a chain of filter functions
	uppercase := core.FilterFunc(func(ctx context.Context, data []byte) (*types.FilterResult, error) {
		return types.ContinueWith(bytes.ToUpper(data)), nil
	})
	
	addPrefix := core.FilterFunc(func(ctx context.Context, data []byte) (*types.FilterResult, error) {
		prefixed := append([]byte("PREFIX-"), data...)
		return types.ContinueWith(prefixed), nil
	})
	
	addSuffix := core.FilterFunc(func(ctx context.Context, data []byte) (*types.FilterResult, error) {
		suffixed := append(data, []byte("-SUFFIX")...)
		return types.ContinueWith(suffixed), nil
	})
	
	// Process through chain manually
	input := []byte("hello")
	
	result1, _ := uppercase.Process(context.Background(), input)
	result2, _ := addPrefix.Process(context.Background(), result1.Data)
	result3, _ := addSuffix.Process(context.Background(), result2.Data)
	
	expected := "PREFIX-HELLO-SUFFIX"
	if string(result3.Data) != expected {
		t.Errorf("Chained result = %s, want %s", result3.Data, expected)
	}
}

// Test FilterFunc with different result statuses
func TestFilterFunc_ResultStatuses(t *testing.T) {
	tests := []struct {
		name   string
		filter core.FilterFunc
		want   types.FilterStatus
	}{
		{
			name: "Continue",
			filter: core.FilterFunc(func(ctx context.Context, data []byte) (*types.FilterResult, error) {
				return types.ContinueWith(data), nil
			}),
			want: types.Continue,
		},
		{
			name: "StopIteration",
			filter: core.FilterFunc(func(ctx context.Context, data []byte) (*types.FilterResult, error) {
				return types.StopIterationResult(), nil
			}),
			want: types.StopIteration,
		},
		{
			name: "Error",
			filter: core.FilterFunc(func(ctx context.Context, data []byte) (*types.FilterResult, error) {
				result := &types.FilterResult{
					Status: types.Error,
					Data:   data,
					Error:  errors.New("test error"),
				}
				return result, nil
			}),
			want: types.Error,
		},
	}
	
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			result, _ := tt.filter.Process(context.Background(), []byte("test"))
			if result.Status != tt.want {
				t.Errorf("Status = %v, want %v", result.Status, tt.want)
			}
		})
	}
}

// Benchmarks

func BenchmarkFilterFunc_Process(b *testing.B) {
	filter := core.FilterFunc(func(ctx context.Context, data []byte) (*types.FilterResult, error) {
		return types.ContinueWith(data), nil
	})
	
	data := []byte("benchmark data")
	ctx := context.Background()
	
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		filter.Process(ctx, data)
	}
}

func BenchmarkWrapFilterFunc_Process(b *testing.B) {
	filter := core.WrapFilterFunc("bench", "test",
		func(ctx context.Context, data []byte) (*types.FilterResult, error) {
			return types.ContinueWith(data), nil
		})
	
	data := []byte("benchmark data")
	ctx := context.Background()
	
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		filter.Process(ctx, data)
	}
}

func BenchmarkFilterFunc_Transform(b *testing.B) {
	filter := core.FilterFunc(func(ctx context.Context, data []byte) (*types.FilterResult, error) {
		upper := bytes.ToUpper(data)
		return types.ContinueWith(upper), nil
	})
	
	data := []byte("transform this text")
	ctx := context.Background()
	
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		filter.Process(ctx, data)
	}
}

func BenchmarkWrapFilterFunc_Concurrent(b *testing.B) {
	filter := core.WrapFilterFunc("bench", "test",
		func(ctx context.Context, data []byte) (*types.FilterResult, error) {
			// Simple pass-through
			return types.ContinueWith(data), nil
		})
	
	data := []byte("benchmark")
	ctx := context.Background()
	
	b.RunParallel(func(pb *testing.PB) {
		for pb.Next() {
			filter.Process(ctx, data)
		}
	})
}

func BenchmarkFilterFunc_Chain(b *testing.B) {
	filter1 := core.FilterFunc(func(ctx context.Context, data []byte) (*types.FilterResult, error) {
		return types.ContinueWith(append([]byte("1-"), data...)), nil
	})
	
	filter2 := core.FilterFunc(func(ctx context.Context, data []byte) (*types.FilterResult, error) {
		return types.ContinueWith(append(data, []byte("-2")...)), nil
	})
	
	data := []byte("data")
	ctx := context.Background()
	
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		result1, _ := filter1.Process(ctx, data)
		filter2.Process(ctx, result1.Data)
	}
}