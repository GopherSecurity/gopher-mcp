#ifndef MCP_RESULT_H
#define MCP_RESULT_H

#include "mcp/core/compat.h"
#include "mcp/io_result.h"
#include "mcp/types.h"

namespace mcp {

// Result is already defined in type_helpers.h as:
// template <typename T>
// using Result = variant<T, Error>;

// For cleaner API, we define a VoidResult type
using VoidResult = Result<std::nullptr_t>;

// Helper to create successful void results
inline VoidResult makeVoidSuccess() { return VoidResult(nullptr); }

// Helper to create error void results
inline VoidResult makeVoidError(const Error& error) {
  return VoidResult(error);
}

// Convenience factory functions for Result<T>
template <typename T>
Result<T> makeSuccess(T&& value) {
  return Result<T>(std::forward<T>(value));
}

template <typename T>
Result<T> makeError(const Error& error) {
  return Result<T>(error);
}

template <typename T>
Result<T> makeError(int code, const std::string& message) {
  Error err;
  err.code = code;
  err.message = message;
  return Result<T>(err);
}

// Helper to convert IoResult to Result for protocol-level errors
template <typename T>
Result<T> ioResultToResult(const IoResult<T>& io_result) {
  if (io_result.ok()) {
    return Result<T>(*io_result);
  } else {
    Error err;
    err.code = io_result.error_code();
    err.message =
        io_result.error_info ? io_result.error_info->message : "Unknown error";
    return Result<T>(err);
  }
}

// Transport-specific result for higher-level operations
struct TransportIoResult {
  enum PostIoAction {
    CONTINUE,        // Continue processing
    CLOSE,           // Close the connection
    WRITE_AND_CLOSE  // Write remaining data then close
  };

  PostIoAction action_;
  uint64_t bytes_processed_;
  bool end_stream_read_;
  optional<Error> error_;  // Use MCP Error type for protocol errors

  static TransportIoResult success(uint64_t bytes,
                                   PostIoAction action = CONTINUE) {
    return TransportIoResult{action, bytes, false, nullopt};
  }

  static TransportIoResult error(const Error& err) {
    return TransportIoResult{CLOSE, 0, false, err};
  }

  static TransportIoResult endStream(uint64_t bytes) {
    return TransportIoResult{CONTINUE, bytes, true, nullopt};
  }

  static TransportIoResult stop() {
    return TransportIoResult{CONTINUE, 0, false, nullopt};
  }

  static TransportIoResult close() {
    return TransportIoResult{CLOSE, 0, false, nullopt};
  }

  bool ok() const { return !error_.has_value(); }
};

}  // namespace mcp

#endif  // MCP_RESULT_H