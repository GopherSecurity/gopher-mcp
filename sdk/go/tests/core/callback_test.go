package core_test

import (
	"errors"
	"sync"
	"sync/atomic"
	"testing"
	"time"

	"github.com/GopherSecurity/gopher-mcp/src/core"
)

// Test 1: SimpleEvent creation and methods
func TestSimpleEvent(t *testing.T) {
	eventName := "test-event"
	eventData := map[string]string{"key": "value"}
	
	event := core.NewEvent(eventName, eventData)
	
	if event.Name() != eventName {
		t.Errorf("Event name = %s, want %s", event.Name(), eventName)
	}
	
	data, ok := event.Data().(map[string]string)
	if !ok {
		t.Fatal("Event data type assertion failed")
	}
	if data["key"] != "value" {
		t.Errorf("Event data[key] = %s, want value", data["key"])
	}
}

// Test 2: NewCallbackManager sync mode
func TestNewCallbackManager_Sync(t *testing.T) {
	cm := core.NewCallbackManager(false)
	
	if cm == nil {
		t.Fatal("NewCallbackManager returned nil")
	}
	
	// Register a simple callback
	called := false
	id, err := cm.Register("test", func(event core.Event) error {
		called = true
		return nil
	})
	
	if err != nil {
		t.Fatalf("Register failed: %v", err)
	}
	if id == 0 {
		t.Error("Register returned invalid ID")
	}
	
	// Trigger the event
	err = cm.Trigger("test", nil)
	if err != nil {
		t.Fatalf("Trigger failed: %v", err)
	}
	
	if !called {
		t.Error("Callback was not called")
	}
}

// Test 3: NewCallbackManager async mode
func TestNewCallbackManager_Async(t *testing.T) {
	cm := core.NewCallbackManager(true)
	cm.SetTimeout(1 * time.Second)
	
	if cm == nil {
		t.Fatal("NewCallbackManager returned nil")
	}
	
	// Register an async callback
	done := make(chan bool, 1)
	_, err := cm.Register("async-test", func(event core.Event) error {
		done <- true
		return nil
	})
	
	if err != nil {
		t.Fatalf("Register failed: %v", err)
	}
	
	// Trigger the event
	err = cm.Trigger("async-test", nil)
	if err != nil {
		t.Fatalf("Trigger failed: %v", err)
	}
	
	// Wait for callback
	select {
	case <-done:
		// Success
	case <-time.After(2 * time.Second):
		t.Error("Async callback did not execute within timeout")
	}
}

// Test 4: Register with invalid parameters
func TestCallbackManager_Register_Invalid(t *testing.T) {
	cm := core.NewCallbackManager(false)
	
	// Empty event name
	_, err := cm.Register("", func(event core.Event) error { return nil })
	if err == nil {
		t.Error("Register with empty event name should fail")
	}
	
	// Nil handler
	_, err = cm.Register("test", nil)
	if err == nil {
		t.Error("Register with nil handler should fail")
	}
}

// Test 5: Unregister callback
func TestCallbackManager_Unregister(t *testing.T) {
	cm := core.NewCallbackManager(false)
	
	// Register callback
	callCount := 0
	id, err := cm.Register("test", func(event core.Event) error {
		callCount++
		return nil
	})
	if err != nil {
		t.Fatalf("Register failed: %v", err)
	}
	
	// Trigger once
	cm.Trigger("test", nil)
	if callCount != 1 {
		t.Errorf("Call count = %d, want 1", callCount)
	}
	
	// Unregister
	err = cm.Unregister("test", id)
	if err != nil {
		t.Fatalf("Unregister failed: %v", err)
	}
	
	// Trigger again - should not call
	cm.Trigger("test", nil)
	if callCount != 1 {
		t.Errorf("Call count after unregister = %d, want 1", callCount)
	}
	
	// Unregister non-existent should return error
	err = cm.Unregister("test", id)
	if err == nil {
		t.Error("Unregister non-existent callback should return error")
	}
}

// Test 6: Multiple callbacks for same event
func TestCallbackManager_MultipleCallbacks(t *testing.T) {
	cm := core.NewCallbackManager(false)
	
	var callOrder []int
	var mu sync.Mutex
	
	// Register multiple callbacks
	for i := 1; i <= 3; i++ {
		num := i // Capture loop variable
		_, err := cm.Register("multi", func(event core.Event) error {
			mu.Lock()
			callOrder = append(callOrder, num)
			mu.Unlock()
			return nil
		})
		if err != nil {
			t.Fatalf("Register callback %d failed: %v", i, err)
		}
	}
	
	// Trigger event
	err := cm.Trigger("multi", "test data")
	if err != nil {
		t.Fatalf("Trigger failed: %v", err)
	}
	
	// Verify all callbacks were called
	if len(callOrder) != 3 {
		t.Errorf("Number of callbacks called = %d, want 3", len(callOrder))
	}
}

// Test 7: Error handling in callbacks
func TestCallbackManager_ErrorHandling(t *testing.T) {
	cm := core.NewCallbackManager(false)
	
	var errorHandled error
	cm.SetErrorHandler(func(err error) {
		errorHandled = err
	})
	
	testErr := errors.New("test error")
	
	// Register callback that returns error
	_, err := cm.Register("error-test", func(event core.Event) error {
		return testErr
	})
	if err != nil {
		t.Fatalf("Register failed: %v", err)
	}
	
	// Trigger should return error
	err = cm.Trigger("error-test", nil)
	if err == nil {
		t.Error("Trigger should return error from callback")
	}
	
	// Error handler should have been called
	if errorHandled != testErr {
		t.Errorf("Error handler received %v, want %v", errorHandled, testErr)
	}
}

// Test 8: Panic recovery in callbacks
func TestCallbackManager_PanicRecovery(t *testing.T) {
	cm := core.NewCallbackManager(false)
	
	var errorHandled error
	cm.SetErrorHandler(func(err error) {
		errorHandled = err
	})
	
	// Register callback that panics
	_, err := cm.Register("panic-test", func(event core.Event) error {
		panic("test panic")
	})
	if err != nil {
		t.Fatalf("Register failed: %v", err)
	}
	
	// Trigger should recover from panic
	err = cm.Trigger("panic-test", nil)
	if err == nil {
		t.Error("Trigger should return error for panicked callback")
	}
	
	// Error handler should have been called
	if errorHandled == nil {
		t.Error("Error handler should have been called for panic")
	}
	
	// Check statistics
	stats := cm.GetStatistics()
	if stats.PanickedCallbacks != 1 {
		t.Errorf("PanickedCallbacks = %d, want 1", stats.PanickedCallbacks)
	}
}

// Test 9: GetStatistics
func TestCallbackManager_GetStatistics(t *testing.T) {
	cm := core.NewCallbackManager(false)
	
	// Register callbacks with different behaviors
	_, _ = cm.Register("success", func(event core.Event) error {
		return nil
	})
	
	_, _ = cm.Register("error", func(event core.Event) error {
		return errors.New("error")
	})
	
	// Trigger events
	cm.Trigger("success", nil)
	cm.Trigger("success", nil)
	cm.Trigger("error", nil)
	
	// Check statistics
	stats := cm.GetStatistics()
	
	if stats.TotalCallbacks != 3 {
		t.Errorf("TotalCallbacks = %d, want 3", stats.TotalCallbacks)
	}
	if stats.SuccessfulCallbacks != 2 {
		t.Errorf("SuccessfulCallbacks = %d, want 2", stats.SuccessfulCallbacks)
	}
	if stats.FailedCallbacks != 1 {
		t.Errorf("FailedCallbacks = %d, want 1", stats.FailedCallbacks)
	}
}

// Test 10: Concurrent operations
func TestCallbackManager_Concurrent(t *testing.T) {
	cm := core.NewCallbackManager(false)
	
	var callCount int32
	numGoroutines := 10
	eventsPerGoroutine := 10
	
	// Register a callback
	_, err := cm.Register("concurrent", func(event core.Event) error {
		atomic.AddInt32(&callCount, 1)
		return nil
	})
	if err != nil {
		t.Fatalf("Register failed: %v", err)
	}
	
	// Concurrent triggers
	var wg sync.WaitGroup
	for i := 0; i < numGoroutines; i++ {
		wg.Add(1)
		go func(id int) {
			defer wg.Done()
			for j := 0; j < eventsPerGoroutine; j++ {
				cm.Trigger("concurrent", id*100+j)
			}
		}(i)
	}
	
	wg.Wait()
	
	expected := int32(numGoroutines * eventsPerGoroutine)
	if callCount != expected {
		t.Errorf("Call count = %d, want %d", callCount, expected)
	}
	
	// Verify statistics
	stats := cm.GetStatistics()
	if stats.TotalCallbacks != uint64(expected) {
		t.Errorf("TotalCallbacks = %d, want %d", stats.TotalCallbacks, expected)
	}
}

// Benchmarks

func BenchmarkCallbackManager_Trigger_Sync(b *testing.B) {
	cm := core.NewCallbackManager(false)
	
	cm.Register("bench", func(event core.Event) error {
		return nil
	})
	
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		cm.Trigger("bench", i)
	}
}

func BenchmarkCallbackManager_Trigger_Async(b *testing.B) {
	cm := core.NewCallbackManager(true)
	cm.SetTimeout(10 * time.Second)
	
	cm.Register("bench", func(event core.Event) error {
		return nil
	})
	
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		cm.Trigger("bench", i)
	}
}

func BenchmarkCallbackManager_Register(b *testing.B) {
	cm := core.NewCallbackManager(false)
	
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		cm.Register("bench", func(event core.Event) error {
			return nil
		})
	}
}

func BenchmarkCallbackManager_Concurrent(b *testing.B) {
	cm := core.NewCallbackManager(false)
	
	cm.Register("bench", func(event core.Event) error {
		return nil
	})
	
	b.RunParallel(func(pb *testing.PB) {
		i := 0
		for pb.Next() {
			cm.Trigger("bench", i)
			i++
		}
	})
}