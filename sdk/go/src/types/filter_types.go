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

// FilterPosition indicates where a filter should be placed in a chain.
// It determines the relative position when adding filters dynamically.
type FilterPosition int

const (
	// First indicates the filter should be placed at the beginning of the chain.
	First FilterPosition = iota

	// Last indicates the filter should be placed at the end of the chain.
	Last

	// Before indicates the filter should be placed before a specific filter.
	// Requires a reference filter name or ID.
	Before

	// After indicates the filter should be placed after a specific filter.
	// Requires a reference filter name or ID.
	After
)

// String returns a human-readable string representation of the FilterPosition.
func (p FilterPosition) String() string {
	switch p {
	case First:
		return "First"
	case Last:
		return "Last"
	case Before:
		return "Before"
	case After:
		return "After"
	default:
		return fmt.Sprintf("FilterPosition(%d)", p)
	}
}

// IsValid validates that the position is within the valid range.
func (p FilterPosition) IsValid() bool {
	return p >= First && p <= After
}

// RequiresReference returns true if the position requires a reference filter.
func (p FilterPosition) RequiresReference() bool {
	return p == Before || p == After
}