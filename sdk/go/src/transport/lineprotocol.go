// Package transport provides communication transports for the MCP Filter SDK.
package transport

import (
	"bufio"
	"bytes"
	"encoding/binary"
	"fmt"
	"io"
	"sync"
)

// LineProtocol implements line-based message framing with support for
// embedded newlines through escaping or length prefixing.
//
// The protocol supports two modes:
//   - Escaped mode: Newlines in messages are escaped with backslash
//   - Length-prefixed mode: Messages are prefixed with their length
//
// Example usage:
//
//	protocol := NewLineProtocol(LineProtocolConfig{
//	    Mode: EscapedMode,
//	    Delimiter: '\n',
//	})
//	
//	// Frame a message
//	framed := protocol.Frame([]byte("Hello\nWorld"))
//	
//	// Parse incoming data
//	messages, remaining := protocol.Parse(data)
type LineProtocol struct {
	config LineProtocolConfig
	
	// Parser state
	buffer    bytes.Buffer
	inEscape  bool
	msgLength int
	
	// Synchronization
	mu sync.Mutex
}

// LineProtocolMode defines the framing mode.
type LineProtocolMode int

const (
	// EscapedMode escapes delimiter characters in messages
	EscapedMode LineProtocolMode = iota
	
	// LengthPrefixedMode prefixes messages with their length
	LengthPrefixedMode
	
	// DelimitedMode uses simple delimiter without escaping (no embedded delimiters allowed)
	DelimitedMode
)

// LineProtocolConfig configures the line protocol behavior.
type LineProtocolConfig struct {
	// Mode determines how embedded delimiters are handled
	Mode LineProtocolMode
	
	// Delimiter character (default: '\n')
	Delimiter byte
	
	// MaxMessageSize limits message size (default: 1MB)
	MaxMessageSize int
	
	// LengthFieldSize for length-prefixed mode (2, 4, or 8 bytes)
	LengthFieldSize int
	
	// EscapeChar for escaped mode (default: '\\')
	EscapeChar byte
}

// DefaultLineProtocolConfig returns default configuration.
func DefaultLineProtocolConfig() LineProtocolConfig {
	return LineProtocolConfig{
		Mode:            EscapedMode,
		Delimiter:       '\n',
		MaxMessageSize:  1024 * 1024, // 1MB
		LengthFieldSize: 4,           // 32-bit length field
		EscapeChar:      '\\',
	}
}

// NewLineProtocol creates a new line protocol handler.
func NewLineProtocol(config LineProtocolConfig) *LineProtocol {
	// Apply defaults
	if config.Delimiter == 0 {
		config.Delimiter = '\n'
	}
	if config.MaxMessageSize == 0 {
		config.MaxMessageSize = 1024 * 1024
	}
	if config.LengthFieldSize == 0 {
		config.LengthFieldSize = 4
	}
	if config.EscapeChar == 0 {
		config.EscapeChar = '\\'
	}
	
	return &LineProtocol{
		config: config,
	}
}

// Frame adds framing to a message based on the protocol mode.
func (lp *LineProtocol) Frame(message []byte) ([]byte, error) {
	switch lp.config.Mode {
	case EscapedMode:
		return lp.frameEscaped(message), nil
		
	case LengthPrefixedMode:
		return lp.frameLengthPrefixed(message)
		
	case DelimitedMode:
		return lp.frameDelimited(message)
		
	default:
		return nil, fmt.Errorf("unknown protocol mode: %v", lp.config.Mode)
	}
}

// frameEscaped escapes delimiter and escape characters in the message.
func (lp *LineProtocol) frameEscaped(message []byte) []byte {
	// Count characters that need escaping
	escapeCount := 0
	for _, b := range message {
		if b == lp.config.Delimiter || b == lp.config.EscapeChar {
			escapeCount++
		}
	}
	
	// Allocate result buffer
	result := make([]byte, 0, len(message)+escapeCount+1)
	
	// Escape special characters
	for _, b := range message {
		if b == lp.config.Delimiter || b == lp.config.EscapeChar {
			result = append(result, lp.config.EscapeChar)
		}
		result = append(result, b)
	}
	
	// Add delimiter
	result = append(result, lp.config.Delimiter)
	
	return result
}

// frameLengthPrefixed adds a length prefix to the message.
func (lp *LineProtocol) frameLengthPrefixed(message []byte) ([]byte, error) {
	msgLen := len(message)
	
	// Check message size
	if msgLen > lp.config.MaxMessageSize {
		return nil, fmt.Errorf("message size %d exceeds maximum %d", msgLen, lp.config.MaxMessageSize)
	}
	
	// Create length prefix
	var lengthBuf []byte
	switch lp.config.LengthFieldSize {
	case 2:
		if msgLen > 65535 {
			return nil, fmt.Errorf("message size %d exceeds 16-bit limit", msgLen)
		}
		lengthBuf = make([]byte, 2)
		binary.BigEndian.PutUint16(lengthBuf, uint16(msgLen))
		
	case 4:
		lengthBuf = make([]byte, 4)
		binary.BigEndian.PutUint32(lengthBuf, uint32(msgLen))
		
	case 8:
		lengthBuf = make([]byte, 8)
		binary.BigEndian.PutUint64(lengthBuf, uint64(msgLen))
		
	default:
		return nil, fmt.Errorf("invalid length field size: %d", lp.config.LengthFieldSize)
	}
	
	// Combine length prefix, message, and delimiter
	result := make([]byte, 0, len(lengthBuf)+msgLen+1)
	result = append(result, lengthBuf...)
	result = append(result, message...)
	result = append(result, lp.config.Delimiter)
	
	return result, nil
}

// frameDelimited adds a delimiter without escaping (validates no embedded delimiters).
func (lp *LineProtocol) frameDelimited(message []byte) ([]byte, error) {
	// Check for embedded delimiters
	if bytes.IndexByte(message, lp.config.Delimiter) >= 0 {
		return nil, fmt.Errorf("message contains embedded delimiter")
	}
	
	// Add delimiter
	result := make([]byte, len(message)+1)
	copy(result, message)
	result[len(message)] = lp.config.Delimiter
	
	return result, nil
}

// Parse extracts messages from incoming data stream.
// Returns parsed messages and any remaining unparsed data.
func (lp *LineProtocol) Parse(data []byte) ([][]byte, []byte, error) {
	lp.mu.Lock()
	defer lp.mu.Unlock()
	
	// Add new data to buffer
	lp.buffer.Write(data)
	
	var messages [][]byte
	
	switch lp.config.Mode {
	case EscapedMode:
		messages = lp.parseEscaped()
		
	case LengthPrefixedMode:
		var err error
		messages, err = lp.parseLengthPrefixed()
		if err != nil {
			return nil, lp.buffer.Bytes(), err
		}
		
	case DelimitedMode:
		messages = lp.parseDelimited()
		
	default:
		return nil, lp.buffer.Bytes(), fmt.Errorf("unknown protocol mode: %v", lp.config.Mode)
	}
	
	// Return messages and remaining data
	return messages, lp.buffer.Bytes(), nil
}

// parseEscaped extracts escaped messages from the buffer.
func (lp *LineProtocol) parseEscaped() [][]byte {
	var messages [][]byte
	var currentMsg bytes.Buffer
	
	data := lp.buffer.Bytes()
	i := 0
	
	for i < len(data) {
		b := data[i]
		
		if lp.inEscape {
			// Add escaped character
			currentMsg.WriteByte(b)
			lp.inEscape = false
			i++
		} else if b == lp.config.EscapeChar {
			// Start escape sequence
			lp.inEscape = true
			i++
		} else if b == lp.config.Delimiter {
			// End of message
			if currentMsg.Len() > 0 || i > 0 {
				msg := make([]byte, currentMsg.Len())
				copy(msg, currentMsg.Bytes())
				messages = append(messages, msg)
				currentMsg.Reset()
			}
			i++
		} else {
			// Regular character
			currentMsg.WriteByte(b)
			i++
		}
	}
	
	// Update buffer with remaining data
	if currentMsg.Len() > 0 || lp.inEscape {
		// Incomplete message, keep in buffer
		remaining := make([]byte, 0, currentMsg.Len()+1)
		if lp.inEscape {
			remaining = append(remaining, lp.config.EscapeChar)
		}
		remaining = append(remaining, currentMsg.Bytes()...)
		lp.buffer.Reset()
		lp.buffer.Write(remaining)
	} else {
		// All data processed
		lp.buffer.Reset()
	}
	
	return messages
}

// parseLengthPrefixed extracts length-prefixed messages from the buffer.
func (lp *LineProtocol) parseLengthPrefixed() ([][]byte, error) {
	var messages [][]byte
	data := lp.buffer.Bytes()
	offset := 0
	
	for offset < len(data) {
		// Need length field + delimiter at minimum
		if len(data)-offset < lp.config.LengthFieldSize+1 {
			break
		}
		
		// Read length field
		var msgLen int
		switch lp.config.LengthFieldSize {
		case 2:
			msgLen = int(binary.BigEndian.Uint16(data[offset:]))
		case 4:
			msgLen = int(binary.BigEndian.Uint32(data[offset:]))
		case 8:
			msgLen = int(binary.BigEndian.Uint64(data[offset:]))
		}
		
		// Validate length
		if msgLen < 0 || msgLen > lp.config.MaxMessageSize {
			return nil, fmt.Errorf("invalid message length: %d", msgLen)
		}
		
		// Check if we have the complete message
		totalLen := lp.config.LengthFieldSize + msgLen + 1 // +1 for delimiter
		if len(data)-offset < totalLen {
			break
		}
		
		// Extract message
		msgStart := offset + lp.config.LengthFieldSize
		msgEnd := msgStart + msgLen
		
		// Verify delimiter
		if data[msgEnd] != lp.config.Delimiter {
			return nil, fmt.Errorf("expected delimiter at position %d, got %v", msgEnd, data[msgEnd])
		}
		
		// Copy message
		msg := make([]byte, msgLen)
		copy(msg, data[msgStart:msgEnd])
		messages = append(messages, msg)
		
		// Move to next message
		offset = msgEnd + 1
	}
	
	// Update buffer with remaining data
	if offset < len(data) {
		remaining := data[offset:]
		lp.buffer.Reset()
		lp.buffer.Write(remaining)
	} else {
		lp.buffer.Reset()
	}
	
	return messages, nil
}

// parseDelimited extracts delimited messages from the buffer.
func (lp *LineProtocol) parseDelimited() [][]byte {
	var messages [][]byte
	scanner := bufio.NewScanner(bytes.NewReader(lp.buffer.Bytes()))
	
	// Set custom split function for delimiter
	scanner.Split(func(data []byte, atEOF bool) (advance int, token []byte, err error) {
		if atEOF && len(data) == 0 {
			return 0, nil, nil
		}
		
		// Look for delimiter
		if i := bytes.IndexByte(data, lp.config.Delimiter); i >= 0 {
			// Found delimiter
			return i + 1, data[0:i], nil
		}
		
		// If at EOF, return remaining data
		if atEOF {
			return 0, nil, nil
		}
		
		// Request more data
		return 0, nil, nil
	})
	
	// Extract messages
	lastPos := 0
	for scanner.Scan() {
		msg := scanner.Bytes()
		msgCopy := make([]byte, len(msg))
		copy(msgCopy, msg)
		messages = append(messages, msgCopy)
		lastPos += len(msg) + 1 // +1 for delimiter
	}
	
	// Update buffer with remaining data
	if lastPos < lp.buffer.Len() {
		remaining := lp.buffer.Bytes()[lastPos:]
		lp.buffer.Reset()
		lp.buffer.Write(remaining)
	} else {
		lp.buffer.Reset()
	}
	
	return messages
}

// Reset clears the parser state.
func (lp *LineProtocol) Reset() {
	lp.mu.Lock()
	defer lp.mu.Unlock()
	
	lp.buffer.Reset()
	lp.inEscape = false
	lp.msgLength = 0
}

// Writer returns an io.Writer that frames written data.
func (lp *LineProtocol) Writer(w io.Writer) io.Writer {
	return &lineProtocolWriter{
		protocol: lp,
		writer:   w,
	}
}

// lineProtocolWriter wraps an io.Writer with line protocol framing.
type lineProtocolWriter struct {
	protocol *LineProtocol
	writer   io.Writer
}

// Write frames data and writes it to the underlying writer.
func (lpw *lineProtocolWriter) Write(p []byte) (n int, err error) {
	framed, err := lpw.protocol.Frame(p)
	if err != nil {
		return 0, err
	}
	
	written, err := lpw.writer.Write(framed)
	if err != nil {
		return 0, err
	}
	
	// Return original data length (not framed length)
	if written >= len(framed) {
		return len(p), nil
	}
	
	// Partial write
	return 0, io.ErrShortWrite
}

// Reader returns an io.Reader that parses framed data.
func (lp *LineProtocol) Reader(r io.Reader) io.Reader {
	return &lineProtocolReader{
		protocol: lp,
		reader:   r,
		buffer:   make([]byte, 4096),
	}
}

// lineProtocolReader wraps an io.Reader with line protocol parsing.
type lineProtocolReader struct {
	protocol  *LineProtocol
	reader    io.Reader
	buffer    []byte
	messages  [][]byte
	current   []byte
	offset    int
}

// Read parses framed data and returns unframed messages.
func (lpr *lineProtocolReader) Read(p []byte) (n int, err error) {
	// If we have data in current message, return it
	if len(lpr.current) > 0 {
		n = copy(p, lpr.current[lpr.offset:])
		lpr.offset += n
		if lpr.offset >= len(lpr.current) {
			lpr.current = nil
			lpr.offset = 0
		}
		return n, nil
	}
	
	// If we have queued messages, return the next one
	if len(lpr.messages) > 0 {
		lpr.current = lpr.messages[0]
		lpr.messages = lpr.messages[1:]
		lpr.offset = 0
		return lpr.Read(p)
	}
	
	// Read more data from underlying reader
	n, err = lpr.reader.Read(lpr.buffer)
	if err != nil {
		return 0, err
	}
	
	// Parse the data
	messages, remaining, parseErr := lpr.protocol.Parse(lpr.buffer[:n])
	if parseErr != nil {
		return 0, parseErr
	}
	
	// Queue parsed messages
	lpr.messages = messages
	
	// If we have messages, return data
	if len(lpr.messages) > 0 {
		return lpr.Read(p)
	}
	
	// No complete messages yet
	if len(remaining) > 0 {
		// More data needed
		return 0, nil
	}
	
	return 0, io.EOF
}