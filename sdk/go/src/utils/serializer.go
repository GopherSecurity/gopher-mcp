// Package utils provides utility functions for the MCP Filter SDK.
package utils

import (
	"bytes"
	"encoding/json"
	"fmt"
	"io"
	"reflect"
	"sync"
)

// MarshalFunc is a custom marshaling function for a specific type.
type MarshalFunc func(v interface{}) ([]byte, error)

// UnmarshalFunc is a custom unmarshaling function for a specific type.
type UnmarshalFunc func(data []byte, v interface{}) error

// Schema represents a JSON schema for validation.
type Schema interface {
	Validate(data []byte) error
}

// JsonSerializer provides configurable JSON serialization with custom marshalers.
type JsonSerializer struct {
	// indent enables pretty printing with indentation
	indent bool

	// escapeHTML escapes HTML characters in strings
	escapeHTML bool

	// omitEmpty omits empty fields from output
	omitEmpty bool

	// customMarshalers maps types to custom marshal functions
	customMarshalers map[reflect.Type]MarshalFunc

	// customUnmarshalers maps types to custom unmarshal functions
	customUnmarshalers map[reflect.Type]UnmarshalFunc

	// schemaCache caches compiled schemas
	schemaCache map[string]Schema

	// encoderPool pools json.Encoder instances
	encoderPool sync.Pool

	// decoderPool pools json.Decoder instances
	decoderPool sync.Pool

	// bufferPool pools bytes.Buffer instances
	bufferPool sync.Pool

	// mu protects concurrent access
	mu sync.RWMutex
}

// NewJsonSerializer creates a new JSON serializer with default settings.
func NewJsonSerializer() *JsonSerializer {
	js := &JsonSerializer{
		escapeHTML:         true,
		customMarshalers:   make(map[reflect.Type]MarshalFunc),
		customUnmarshalers: make(map[reflect.Type]UnmarshalFunc),
		schemaCache:        make(map[string]Schema),
	}

	// Initialize pools
	js.encoderPool.New = func() interface{} {
		return json.NewEncoder(nil)
	}
	js.decoderPool.New = func() interface{} {
		return json.NewDecoder(nil)
	}
	js.bufferPool.New = func() interface{} {
		return new(bytes.Buffer)
	}

	return js
}

// SetIndent enables or disables pretty printing.
func (js *JsonSerializer) SetIndent(indent bool) {
	js.indent = indent
}

// SetEscapeHTML enables or disables HTML escaping.
func (js *JsonSerializer) SetEscapeHTML(escape bool) {
	js.escapeHTML = escape
}

// SetOmitEmpty enables or disables omitting empty fields.
func (js *JsonSerializer) SetOmitEmpty(omit bool) {
	js.omitEmpty = omit
}

// Marshal serializes a value to JSON using configured options.
func (js *JsonSerializer) Marshal(v interface{}) ([]byte, error) {
	// Check for custom marshaler
	js.mu.RLock()
	if marshaler, ok := js.customMarshalers[reflect.TypeOf(v)]; ok {
		js.mu.RUnlock()
		return marshaler(v)
	}
	js.mu.RUnlock()

	// Get buffer from pool
	buffer := js.bufferPool.Get().(*bytes.Buffer)
	buffer.Reset()
	defer js.bufferPool.Put(buffer)

	// Get encoder from pool
	encoder := js.encoderPool.Get().(*json.Encoder)
	encoder.SetEscapeHTML(js.escapeHTML)
	
	if js.indent {
		encoder.SetIndent("", "  ")
	}

	// Reset encoder with new buffer
	*encoder = *json.NewEncoder(buffer)
	encoder.SetEscapeHTML(js.escapeHTML)
	if js.indent {
		encoder.SetIndent("", "  ")
	}

	if err := encoder.Encode(v); err != nil {
		return nil, err
	}

	// Remove trailing newline added by Encode
	data := buffer.Bytes()
	result := make([]byte, len(data))
	copy(result, data)
	
	if len(result) > 0 && result[len(result)-1] == '\n' {
		result = result[:len(result)-1]
	}

	js.encoderPool.Put(encoder)
	return result, nil
}

// Unmarshal deserializes JSON data into a value with validation.
func (js *JsonSerializer) Unmarshal(data []byte, v interface{}) error {
	// Check for custom unmarshaler
	js.mu.RLock()
	if unmarshaler, ok := js.customUnmarshalers[reflect.TypeOf(v)]; ok {
		js.mu.RUnlock()
		return unmarshaler(data, v)
	}
	js.mu.RUnlock()

	// Use decoder for better error messages
	decoder := json.NewDecoder(bytes.NewReader(data))
	decoder.DisallowUnknownFields() // Strict validation

	return decoder.Decode(v)
}

// MarshalToWriter serializes a value directly to a writer.
func (js *JsonSerializer) MarshalToWriter(v interface{}, w io.Writer) error {
	// Check for custom marshaler
	js.mu.RLock()
	if marshaler, ok := js.customMarshalers[reflect.TypeOf(v)]; ok {
		js.mu.RUnlock()
		data, err := marshaler(v)
		if err != nil {
			return err
		}
		_, err = w.Write(data)
		return err
	}
	js.mu.RUnlock()

	// Stream directly to writer
	encoder := json.NewEncoder(w)
	encoder.SetEscapeHTML(js.escapeHTML)
	
	if js.indent {
		encoder.SetIndent("", "  ")
	}

	return encoder.Encode(v)
}

// UnmarshalFromReader deserializes JSON directly from a reader.
func (js *JsonSerializer) UnmarshalFromReader(r io.Reader, v interface{}) error {
	// Check for custom unmarshaler
	js.mu.RLock()
	if unmarshaler, ok := js.customUnmarshalers[reflect.TypeOf(v)]; ok {
		js.mu.RUnlock()
		data, err := io.ReadAll(r)
		if err != nil {
			return err
		}
		return unmarshaler(data, v)
	}
	js.mu.RUnlock()

	// Stream directly from reader
	decoder := json.NewDecoder(r)
	decoder.DisallowUnknownFields()

	return decoder.Decode(v)
}

// RegisterMarshaler registers a custom marshaler for a type.
func (js *JsonSerializer) RegisterMarshaler(t reflect.Type, f MarshalFunc) {
	js.mu.Lock()
	defer js.mu.Unlock()
	js.customMarshalers[t] = f
}

// RegisterUnmarshaler registers a custom unmarshaler for a type.
func (js *JsonSerializer) RegisterUnmarshaler(t reflect.Type, f UnmarshalFunc) {
	js.mu.Lock()
	defer js.mu.Unlock()
	js.customUnmarshalers[t] = f
}

// ValidateJSON validates JSON data against a schema.
func (js *JsonSerializer) ValidateJSON(data []byte, schema Schema) error {
	if schema == nil {
		return fmt.Errorf("schema is nil")
	}

	// Validate JSON is well-formed
	var temp interface{}
	if err := json.Unmarshal(data, &temp); err != nil {
		return fmt.Errorf("invalid JSON: %w", err)
	}

	// Validate against schema
	return schema.Validate(data)
}

// PrettyPrint formats JSON with indentation.
func (js *JsonSerializer) PrettyPrint(data []byte) ([]byte, error) {
	var temp interface{}
	if err := json.Unmarshal(data, &temp); err != nil {
		return nil, err
	}

	buffer := &bytes.Buffer{}
	encoder := json.NewEncoder(buffer)
	encoder.SetIndent("", "  ")
	encoder.SetEscapeHTML(js.escapeHTML)

	if err := encoder.Encode(temp); err != nil {
		return nil, err
	}

	// Remove trailing newline
	result := buffer.Bytes()
	if len(result) > 0 && result[len(result)-1] == '\n' {
		result = result[:len(result)-1]
	}

	return result, nil
}

// Compact minimizes JSON by removing whitespace.
func (js *JsonSerializer) Compact(data []byte) ([]byte, error) {
	buffer := &bytes.Buffer{}
	if err := json.Compact(buffer, data); err != nil {
		return nil, err
	}
	return buffer.Bytes(), nil
}