// Package transport provides communication transports for the MCP Filter SDK.
package transport

import (
	"bufio"
	"bytes"
	"context"
	"fmt"
	"io"
	"os"
	"runtime"
	"sync"
)

// StdioTransport implements Transport using standard input/output streams.
// It provides line-based message framing suitable for CLI tools and pipes.
//
// Features:
//   - Line-based protocol with configurable delimiter
//   - Buffered I/O for efficiency
//   - Platform-specific handling (Windows vs Unix)
//   - Graceful handling of pipe closure
//
// Example usage:
//
//	transport := NewStdioTransport(StdioConfig{
//	    Delimiter: '\n',
//	    BufferSize: 4096,
//	})
//	
//	if err := transport.Connect(context.Background()); err != nil {
//	    log.Fatal(err)
//	}
//	defer transport.Disconnect()
//	
//	// Send a message
//	transport.Send([]byte("Hello, World!"))
//	
//	// Receive a message
//	data, err := transport.Receive()
type StdioTransport struct {
	TransportBase
	
	// I/O components
	reader  *bufio.Reader
	writer  *bufio.Writer
	scanner *bufio.Scanner
	
	// Configuration
	delimiter byte
	config    StdioConfig
	
	// Synchronization
	readMu  sync.Mutex
	writeMu sync.Mutex
}

// StdioConfig provides configuration specific to stdio transport.
type StdioConfig struct {
	// Delimiter for message framing (default: '\n')
	Delimiter byte
	
	// Buffer size for reader/writer (default: 4096)
	BufferSize int
	
	// Maximum message size (default: 1MB)
	MaxMessageSize int
	
	// Whether to escape delimiter in messages
	EscapeDelimiter bool
	
	// Platform-specific settings
	WindowsMode bool
}

// DefaultStdioConfig returns default configuration for stdio transport.
func DefaultStdioConfig() StdioConfig {
	return StdioConfig{
		Delimiter:       '\n',
		BufferSize:      4096,
		MaxMessageSize:  1024 * 1024, // 1MB
		EscapeDelimiter: false,
		WindowsMode:     runtime.GOOS == "windows",
	}
}

// NewStdioTransport creates a new stdio transport with the given configuration.
func NewStdioTransport(config StdioConfig) *StdioTransport {
	baseConfig := DefaultTransportConfig()
	baseConfig.ReadBufferSize = config.BufferSize
	baseConfig.WriteBufferSize = config.BufferSize
	
	return &StdioTransport{
		TransportBase: NewTransportBase(baseConfig),
		delimiter:     config.Delimiter,
		config:        config,
	}
}

// Connect establishes the stdio connection by setting up buffered I/O.
func (st *StdioTransport) Connect(ctx context.Context) error {
	// Check if already connected
	if !st.SetConnected(true) {
		return ErrAlreadyConnected
	}
	
	// Check context cancellation
	select {
	case <-ctx.Done():
		st.SetConnected(false)
		return ctx.Err()
	default:
	}
	
	// Set up buffered reader for stdin
	st.reader = bufio.NewReaderSize(os.Stdin, st.config.BufferSize)
	
	// Set up buffered writer for stdout
	st.writer = bufio.NewWriterSize(os.Stdout, st.config.BufferSize)
	
	// Configure scanner for line-based protocol
	st.scanner = bufio.NewScanner(st.reader)
	st.scanner.Buffer(make([]byte, 0, st.config.BufferSize), st.config.MaxMessageSize)
	
	// Set custom split function if delimiter is not newline
	if st.delimiter != '\n' {
		st.scanner.Split(st.createSplitFunc())
	}
	
	// Handle platform differences
	if st.config.WindowsMode {
		st.configurePlatformWindows()
	} else {
		st.configurePlatformUnix()
	}
	
	// Update statistics
	st.UpdateConnectTime()
	st.SetCustomMetric("delimiter", string(st.delimiter))
	st.SetCustomMetric("buffer_size", st.config.BufferSize)
	
	return nil
}

// createSplitFunc creates a custom split function for non-newline delimiters.
func (st *StdioTransport) createSplitFunc() bufio.SplitFunc {
	return func(data []byte, atEOF bool) (advance int, token []byte, err error) {
		if atEOF && len(data) == 0 {
			return 0, nil, nil
		}
		
		// Look for delimiter
		if i := bytes.IndexByte(data, st.delimiter); i >= 0 {
			// We have a full message
			return i + 1, data[0:i], nil
		}
		
		// If we're at EOF, we have a final, non-terminated message
		if atEOF {
			return len(data), data, nil
		}
		
		// Request more data
		return 0, nil, nil
	}
}

// configurePlatformWindows applies Windows-specific configuration.
func (st *StdioTransport) configurePlatformWindows() {
	// Windows-specific handling could include:
	// - Setting console mode for proper line handling
	// - Handling CRLF vs LF line endings
	// For now, we'll just track it as a metric
	st.SetCustomMetric("platform", "windows")
}

// configurePlatformUnix applies Unix-specific configuration.
func (st *StdioTransport) configurePlatformUnix() {
	// Unix-specific handling could include:
	// - Setting terminal modes
	// - Handling signals
	// For now, we'll just track it as a metric
	st.SetCustomMetric("platform", "unix")
}

// Disconnect closes the stdio connection.
func (st *StdioTransport) Disconnect() error {
	// Check if connected
	if !st.SetConnected(false) {
		return nil // Already disconnected
	}
	
	// Flush any pending output
	if st.writer != nil {
		if err := st.writer.Flush(); err != nil {
			st.RecordSendError()
			// Continue with disconnection even if flush fails
		}
	}
	
	// Update statistics
	st.UpdateDisconnectTime()
	
	// Note: We don't close stdin/stdout as they're shared resources
	// Just clear our references
	st.reader = nil
	st.writer = nil
	st.scanner = nil
	
	return nil
}

// Send writes data to stdout with the configured delimiter.
func (st *StdioTransport) Send(data []byte) error {
	// Check connection
	if !st.IsConnected() {
		return ErrNotConnected
	}
	
	st.writeMu.Lock()
	defer st.writeMu.Unlock()
	
	// Handle message escaping if configured
	if st.config.EscapeDelimiter && bytes.IndexByte(data, st.delimiter) >= 0 {
		data = st.escapeDelimiter(data)
	}
	
	// Write data
	n, err := st.writer.Write(data)
	if err != nil {
		st.RecordSendError()
		return &TransportError{
			Code:    "STDIO_WRITE_ERROR",
			Message: "failed to write to stdout",
			Cause:   err,
		}
	}
	
	// Write delimiter
	if err := st.writer.WriteByte(st.delimiter); err != nil {
		st.RecordSendError()
		return &TransportError{
			Code:    "STDIO_DELIMITER_ERROR",
			Message: "failed to write delimiter",
			Cause:   err,
		}
	}
	n++ // Account for delimiter
	
	// Flush buffer
	if err := st.writer.Flush(); err != nil {
		st.RecordSendError()
		return &TransportError{
			Code:    "STDIO_FLUSH_ERROR",
			Message: "failed to flush stdout buffer",
			Cause:   err,
		}
	}
	
	// Update statistics
	st.RecordBytesSent(n)
	st.incrementLineCount("sent")
	
	return nil
}

// Receive reads data from stdin until delimiter or EOF.
func (st *StdioTransport) Receive() ([]byte, error) {
	// Check connection
	if !st.IsConnected() {
		return nil, ErrNotConnected
	}
	
	st.readMu.Lock()
	defer st.readMu.Unlock()
	
	// Scan for next message
	if !st.scanner.Scan() {
		// Check for error or EOF
		if err := st.scanner.Err(); err != nil {
			st.RecordReceiveError()
			return nil, &TransportError{
				Code:    "STDIO_READ_ERROR",
				Message: "failed to read from stdin",
				Cause:   err,
			}
		}
		// EOF reached
		return nil, io.EOF
	}
	
	// Get the message
	data := st.scanner.Bytes()
	
	// Make a copy since scanner reuses the buffer
	result := make([]byte, len(data))
	copy(result, data)
	
	// Handle unescaping if configured
	if st.config.EscapeDelimiter {
		result = st.unescapeDelimiter(result)
	}
	
	// Update statistics
	st.RecordBytesReceived(len(result))
	st.incrementLineCount("received")
	
	return result, nil
}

// escapeDelimiter escapes delimiter characters in the data.
func (st *StdioTransport) escapeDelimiter(data []byte) []byte {
	// Simple escaping: replace delimiter with \delimiter
	escaped := bytes.ReplaceAll(data, []byte{st.delimiter}, []byte{'\\', st.delimiter})
	// Also escape backslashes
	escaped = bytes.ReplaceAll(escaped, []byte{'\\'}, []byte{'\\', '\\'})
	return escaped
}

// unescapeDelimiter unescapes delimiter characters in the data.
func (st *StdioTransport) unescapeDelimiter(data []byte) []byte {
	// Reverse the escaping
	unescaped := bytes.ReplaceAll(data, []byte{'\\', '\\'}, []byte{'\\'})
	unescaped = bytes.ReplaceAll(unescaped, []byte{'\\', st.delimiter}, []byte{st.delimiter})
	return unescaped
}

// incrementLineCount tracks lines read/written.
func (st *StdioTransport) incrementLineCount(direction string) {
	key := fmt.Sprintf("lines_%s", direction)
	
	st.mu.Lock()
	defer st.mu.Unlock()
	
	if st.stats.CustomMetrics == nil {
		st.stats.CustomMetrics = make(map[string]interface{})
	}
	
	if count, ok := st.stats.CustomMetrics[key].(int64); ok {
		st.stats.CustomMetrics[key] = count + 1
	} else {
		st.stats.CustomMetrics[key] = int64(1)
	}
}

// GetAverageMessageSize returns the average message size.
func (st *StdioTransport) GetAverageMessageSize() (sendAvg, receiveAvg float64) {
	st.mu.RLock()
	defer st.mu.RUnlock()
	
	if st.stats.MessagesSent > 0 {
		sendAvg = float64(st.stats.BytesSent) / float64(st.stats.MessagesSent)
	}
	
	if st.stats.MessagesReceived > 0 {
		receiveAvg = float64(st.stats.BytesReceived) / float64(st.stats.MessagesReceived)
	}
	
	return sendAvg, receiveAvg
}

// Close closes the transport and releases resources.
func (st *StdioTransport) Close() error {
	return st.Disconnect()
}