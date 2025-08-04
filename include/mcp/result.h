#ifndef MCP_RESULT_H
#define MCP_RESULT_H

#include "mcp/compat.h"
#include "mcp/types.h"
#include "mcp/optional.h"
#include "mcp/io_result.h"

namespace mcp {

// Result is already defined in type_helpers.h as:
// template <typename T>
// using Result = variant<T, Error>;

// Helper to create void results
inline Result<std::nullptr_t> make_void_result() {
  return Result<std::nullptr_t>(nullptr);
}

// Helper to convert IoResult to Result for protocol-level errors
template <typename T>
Result<T> ioResultToResult(const IoResult<T>& io_result) {
  if (io_result.ok()) {
    return Result<T>(*io_result);
  } else {
    Error err;
    err.code = io_result.error_code();
    err.message = io_result.error_info ? io_result.error_info->message : "Unknown error";
    return Result<T>(err);
  }
}

// Transport-specific result for higher-level operations
struct TransportIoResult {
  enum PostIoAction {
    CONTINUE,      // Continue processing
    CLOSE,         // Close the connection
    WRITE_AND_CLOSE // Write remaining data then close
  };

  PostIoAction action_;
  uint64_t bytes_processed_;
  bool end_stream_read_;
  optional<Error> error_;  // Use MCP Error type for protocol errors

  static TransportIoResult success(uint64_t bytes, PostIoAction action = CONTINUE) {
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

} // namespace mcp

#endif // MCP_RESULT_H