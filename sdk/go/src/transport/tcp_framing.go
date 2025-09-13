// Package transport provides communication transports for the MCP Filter SDK.
package transport

import (
	"encoding/binary"
	"fmt"
	"io"
)

// TcpFraming implements message framing for TCP transport.
type TcpFraming struct {
	mode      FramingMode
	delimiter byte
	maxSize   int
}

// FramingMode defines TCP message framing strategy.
type FramingMode int

const (
	LengthPrefixFraming FramingMode = iota
	DelimiterFraming
)

// NewTcpFraming creates TCP framing handler.
func NewTcpFraming(mode FramingMode, delimiter byte, maxSize int) *TcpFraming {
	return &TcpFraming{
		mode:      mode,
		delimiter: delimiter,
		maxSize:   maxSize,
	}
}

// WriteMessage writes framed message to connection.
func (tf *TcpFraming) WriteMessage(w io.Writer, data []byte) error {
	if tf.mode == LengthPrefixFraming {
		// Write 4-byte length prefix
		length := uint32(len(data))
		if err := binary.Write(w, binary.BigEndian, length); err != nil {
			return err
		}
	}

	// Write data
	n, err := w.Write(data)
	if err != nil {
		return err
	}
	if n != len(data) {
		return io.ErrShortWrite
	}

	if tf.mode == DelimiterFraming {
		// Write delimiter
		if _, err := w.Write([]byte{tf.delimiter}); err != nil {
			return err
		}
	}

	return nil
}

// ReadMessage reads framed message from connection.
func (tf *TcpFraming) ReadMessage(r io.Reader) ([]byte, error) {
	if tf.mode == LengthPrefixFraming {
		// Read length prefix
		var length uint32
		if err := binary.Read(r, binary.BigEndian, &length); err != nil {
			return nil, err
		}

		if int(length) > tf.maxSize {
			return nil, fmt.Errorf("message size %d exceeds max %d", length, tf.maxSize)
		}

		// Read message
		data := make([]byte, length)
		if _, err := io.ReadFull(r, data); err != nil {
			return nil, err
		}

		return data, nil
	}

	// Delimiter-based framing
	var result []byte
	buffer := make([]byte, 1)

	for len(result) < tf.maxSize {
		if _, err := io.ReadFull(r, buffer); err != nil {
			return nil, err
		}

		if buffer[0] == tf.delimiter {
			return result, nil
		}

		result = append(result, buffer[0])
	}

	return nil, fmt.Errorf("message exceeds max size %d", tf.maxSize)
}
