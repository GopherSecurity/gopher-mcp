// Package types provides core type definitions for the MCP Filter SDK.
package types

import "fmt"

// FilterStatus represents the result status of a filter's processing operation.
// It determines how the filter chain should proceed after processing.
type FilterStatus int

const (
	// Continue indicates the filter processed successfully and the chain should continue.
	// The next filter in the chain will receive the processed data.
	Continue FilterStatus = iota

	// StopIteration indicates the filter processed successfully but the chain should stop.
	// No further filters will be executed, and the current result will be returned.
	StopIteration

	// Error indicates the filter encountered an error during processing.
	// The chain will stop and return the error unless configured to bypass errors.
	Error

	// NeedMoreData indicates the filter needs more data to complete processing.
	// Used for filters that work with streaming or chunked data.
	NeedMoreData

	// Buffered indicates the filter has buffered the data for later processing.
	// The chain may continue with empty data or wait based on configuration.
	Buffered
)

// String returns a human-readable string representation of the FilterStatus.
func (s FilterStatus) String() string {
	switch s {
	case Continue:
		return "Continue"
	case StopIteration:
		return "StopIteration"
	case Error:
		return "Error"
	case NeedMoreData:
		return "NeedMoreData"
	case Buffered:
		return "Buffered"
	default:
		return fmt.Sprintf("FilterStatus(%d)", s)
	}
}

// IsTerminal returns true if the status indicates chain termination.
func (s FilterStatus) IsTerminal() bool {
	return s == StopIteration || s == Error
}

// IsSuccess returns true if the status indicates successful processing.
func (s FilterStatus) IsSuccess() bool {
	return s == Continue || s == StopIteration || s == Buffered
}