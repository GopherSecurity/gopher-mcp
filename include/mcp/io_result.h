#ifndef MCP_IO_RESULT_H
#define MCP_IO_RESULT_H

#include <cerrno>
#include <cstring>

#ifdef _WIN32
#include <winsock2.h>
#endif

#include "mcp/optional.h"

namespace mcp {

// System error type for I/O operations
struct SystemError {
  int error_code;
  std::string message;
  
  SystemError(int code, const std::string& msg) 
      : error_code(code), message(msg) {}
};

// Unified I/O result type using optional for consistency
template <typename T>
struct IoResult {
  optional<T> value;         // Value on success
  optional<SystemError> error_info; // Error on failure

  bool ok() const { return value.has_value(); }
  explicit operator bool() const { return ok(); }

  T& operator*() { return *value; }
  const T& operator*() const { return *value; }
  T* operator->() { return &(*value); }
  const T* operator->() const { return &(*value); }

  static IoResult success(T val) {
    IoResult result;
    result.value = std::move(val);
    return result;
  }

  static IoResult error(int code, const std::string& msg = "") {
    IoResult result;
    result.error_info = SystemError(code, msg);
    return result;
  }

  static IoResult from_errno(int err) {
    return error(err, std::strerror(err));
  }

  // Check if error is EAGAIN/EWOULDBLOCK
  bool wouldBlock() const {
    if (!error_info)
      return false;
#ifdef _WIN32
    return error_info->error_code == WSAEWOULDBLOCK;
#else
    return error_info->error_code == EAGAIN || error_info->error_code == EWOULDBLOCK;
#endif
  }

  // For compatibility with existing code
  int error_code() const {
    return error_info ? error_info->error_code : 0;
  }
};

// Specialization for void-like results (using nullptr_t to avoid void in templates)
template <>
struct IoResult<std::nullptr_t> {
  optional<SystemError> error_info;

  bool ok() const { return !error_info.has_value(); }
  explicit operator bool() const { return ok(); }

  static IoResult success() {
    return IoResult();
  }

  static IoResult error(int code, const std::string& msg = "") {
    IoResult result;
    result.error_info = SystemError(code, msg);
    return result;
  }

  static IoResult from_errno(int err) {
    return error(err, std::strerror(err));
  }

  bool wouldBlock() const {
    if (!error_info)
      return false;
#ifdef _WIN32
    return error_info->error_code == WSAEWOULDBLOCK;
#else
    return error_info->error_code == EAGAIN || error_info->error_code == EWOULDBLOCK;
#endif
  }

  int error_code() const {
    return error_info ? error_info->error_code : 0;
  }
};

// Type aliases for common I/O results
using IoCallResult = IoResult<size_t>;
using IoVoidResult = IoResult<std::nullptr_t>;

// Helper to create void results
inline IoVoidResult make_io_void_result() {
  return IoVoidResult::success();
}

} // namespace mcp

#endif // MCP_IO_RESULT_H