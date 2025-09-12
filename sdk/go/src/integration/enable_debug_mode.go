// Package integration provides MCP SDK integration.
package integration

import (
	"fmt"
	"log"
	"os"
	"runtime/debug"
	"sync"
	"time"
)

// DebugMode configuration for debugging.
type DebugMode struct {
	Enabled            bool
	LogLevel           string
	LogFilters         bool
	LogRequests        bool
	LogResponses       bool
	LogNotifications   bool
	LogMetrics         bool
	LogErrors          bool
	TraceExecution     bool
	DumpOnError        bool
	OutputFile         *os.File
	Logger             *log.Logger
	mu                 sync.RWMutex
}

// DebugEvent represents a debug event.
type DebugEvent struct {
	Timestamp   time.Time
	EventType   string
	Component   string
	Message     string
	Data        interface{}
	StackTrace  string
}

// EnableDebugMode enables debug mode with specified options.
func (fc *FilteredMCPClient) EnableDebugMode(options ...DebugOption) {
	fc.mu.Lock()
	defer fc.mu.Unlock()
	
	// Initialize debug mode if not exists
	if fc.debugMode == nil {
		fc.debugMode = &DebugMode{
			Enabled:    true,
			LogLevel:   "INFO",
			Logger:     log.New(os.Stderr, "[MCP-DEBUG] ", log.LstdFlags|log.Lmicroseconds),
		}
	}
	
	// Apply options
	for _, opt := range options {
		opt(fc.debugMode)
	}
	
	// Enable debug mode
	fc.debugMode.Enabled = true
	
	// Log initialization
	fc.logDebug("DEBUG", "System", "Debug mode enabled", map[string]interface{}{
		"log_level":         fc.debugMode.LogLevel,
		"log_filters":       fc.debugMode.LogFilters,
		"log_requests":      fc.debugMode.LogRequests,
		"log_responses":     fc.debugMode.LogResponses,
		"log_notifications": fc.debugMode.LogNotifications,
		"log_metrics":       fc.debugMode.LogMetrics,
		"trace_execution":   fc.debugMode.TraceExecution,
	})
	
	// Install debug hooks
	fc.installDebugHooks()
}

// DisableDebugMode disables debug mode.
func (fc *FilteredMCPClient) DisableDebugMode() {
	fc.mu.Lock()
	defer fc.mu.Unlock()
	
	if fc.debugMode != nil {
		fc.debugMode.Enabled = false
		fc.logDebug("DEBUG", "System", "Debug mode disabled", nil)
		
		// Close output file if exists
		if fc.debugMode.OutputFile != nil {
			fc.debugMode.OutputFile.Close()
			fc.debugMode.OutputFile = nil
		}
	}
	
	// Remove debug hooks
	fc.removeDebugHooks()
}

// installDebugHooks installs debug hooks into filter chains.
func (fc *FilteredMCPClient) installDebugHooks() {
	// Install request hook
	if fc.requestChain != nil && fc.debugMode.LogRequests {
		fc.requestChain.AddHook(func(data []byte, stage string) {
			fc.logDebug("REQUEST", stage, "Processing request", map[string]interface{}{
				"size": len(data),
				"data": truncateData(data, 200),
			})
		})
	}
	
	// Install response hook
	if fc.responseChain != nil && fc.debugMode.LogResponses {
		fc.responseChain.AddHook(func(data []byte, stage string) {
			fc.logDebug("RESPONSE", stage, "Processing response", map[string]interface{}{
				"size": len(data),
				"data": truncateData(data, 200),
			})
		})
	}
	
	// Install notification hook
	if fc.notificationChain != nil && fc.debugMode.LogNotifications {
		fc.notificationChain.AddHook(func(data []byte, stage string) {
			fc.logDebug("NOTIFICATION", stage, "Processing notification", map[string]interface{}{
				"size": len(data),
				"data": truncateData(data, 200),
			})
		})
	}
}

// removeDebugHooks removes debug hooks from filter chains.
func (fc *FilteredMCPClient) removeDebugHooks() {
	// Implementation would remove previously installed hooks
}

// logDebug logs a debug message.
func (fc *FilteredMCPClient) logDebug(eventType, component, message string, data interface{}) {
	if fc.debugMode == nil || !fc.debugMode.Enabled {
		return
	}
	
	fc.debugMode.mu.RLock()
	defer fc.debugMode.mu.RUnlock()
	
	// Check log level
	if !shouldLog(fc.debugMode.LogLevel, eventType) {
		return
	}
	
	// Create debug event
	event := &DebugEvent{
		Timestamp: time.Now(),
		EventType: eventType,
		Component: component,
		Message:   message,
		Data:      data,
	}
	
	// Add stack trace if tracing enabled
	if fc.debugMode.TraceExecution {
		event.StackTrace = string(debug.Stack())
	}
	
	// Format and log
	logMessage := formatDebugEvent(event)
	fc.debugMode.Logger.Println(logMessage)
	
	// Also write to file if configured
	if fc.debugMode.OutputFile != nil {
		fc.debugMode.OutputFile.WriteString(logMessage + "\n")
	}
}

// LogFilterExecution logs filter execution details.
func (fc *FilteredMCPClient) LogFilterExecution(filter Filter, input []byte, output []byte, duration time.Duration, err error) {
	if fc.debugMode == nil || !fc.debugMode.Enabled || !fc.debugMode.LogFilters {
		return
	}
	
	data := map[string]interface{}{
		"filter_id":   filter.GetID(),
		"filter_name": filter.GetName(),
		"input_size":  len(input),
		"output_size": len(output),
		"duration_ms": duration.Milliseconds(),
	}
	
	if err != nil {
		data["error"] = err.Error()
		if fc.debugMode.DumpOnError {
			data["input"] = truncateData(input, 500)
			data["output"] = truncateData(output, 500)
		}
	}
	
	fc.logDebug("FILTER", filter.GetName(), "Filter execution", data)
}

// DumpState dumps current system state for debugging.
func (fc *FilteredMCPClient) DumpState() string {
	fc.mu.RLock()
	defer fc.mu.RUnlock()
	
	state := fmt.Sprintf("=== MCP Client State Dump ===\n")
	state += fmt.Sprintf("Time: %s\n", time.Now().Format(time.RFC3339))
	state += fmt.Sprintf("Debug Mode: %v\n", fc.debugMode != nil && fc.debugMode.Enabled)
	
	// Dump chains
	if fc.requestChain != nil {
		state += fmt.Sprintf("Request Chain: %d filters\n", len(fc.requestChain.filters))
	}
	if fc.responseChain != nil {
		state += fmt.Sprintf("Response Chain: %d filters\n", len(fc.responseChain.filters))
	}
	if fc.notificationChain != nil {
		state += fmt.Sprintf("Notification Chain: %d filters\n", len(fc.notificationChain.filters))
	}
	
	// Dump subscriptions
	state += fmt.Sprintf("Active Subscriptions: %d\n", len(fc.subscriptions))
	
	// Dump metrics
	if fc.metricsCollector != nil {
		metrics := fc.GetFilterMetrics()
		state += fmt.Sprintf("Total Requests: %d\n", metrics.TotalRequests)
		state += fmt.Sprintf("Total Responses: %d\n", metrics.TotalResponses)
		state += fmt.Sprintf("Total Notifications: %d\n", metrics.TotalNotifications)
	}
	
	state += "=========================\n"
	
	return state
}

// DebugOption configures debug mode.
type DebugOption func(*DebugMode)

// WithLogLevel sets the log level.
func WithLogLevel(level string) DebugOption {
	return func(dm *DebugMode) {
		dm.LogLevel = level
	}
}

// WithLogFilters enables filter logging.
func WithLogFilters(enabled bool) DebugOption {
	return func(dm *DebugMode) {
		dm.LogFilters = enabled
	}
}

// WithLogRequests enables request logging.
func WithLogRequests(enabled bool) DebugOption {
	return func(dm *DebugMode) {
		dm.LogRequests = enabled
	}
}

// WithOutputFile sets the debug output file.
func WithOutputFile(filename string) DebugOption {
	return func(dm *DebugMode) {
		file, err := os.Create(filename)
		if err == nil {
			dm.OutputFile = file
		}
	}
}

// WithTraceExecution enables execution tracing.
func WithTraceExecution(enabled bool) DebugOption {
	return func(dm *DebugMode) {
		dm.TraceExecution = enabled
	}
}

// Helper functions
func shouldLog(logLevel, eventType string) bool {
	// Simple log level comparison
	levels := map[string]int{
		"DEBUG": 0,
		"INFO":  1,
		"WARN":  2,
		"ERROR": 3,
	}
	
	currentLevel, ok1 := levels[logLevel]
	eventLevel, ok2 := levels[eventType]
	
	if !ok1 || !ok2 {
		return true
	}
	
	return eventLevel >= currentLevel
}

func formatDebugEvent(event *DebugEvent) string {
	msg := fmt.Sprintf("[%s] [%s] %s: %s",
		event.Timestamp.Format("15:04:05.000"),
		event.EventType,
		event.Component,
		event.Message,
	)
	
	if event.Data != nil {
		msg += fmt.Sprintf(" | Data: %v", event.Data)
	}
	
	if event.StackTrace != "" {
		msg += fmt.Sprintf("\nStack Trace:\n%s", event.StackTrace)
	}
	
	return msg
}

func truncateData(data []byte, maxLen int) string {
	if len(data) <= maxLen {
		return string(data)
	}
	return string(data[:maxLen]) + "..."
}