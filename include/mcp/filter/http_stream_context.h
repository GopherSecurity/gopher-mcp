/**
 * HTTP Stream Context
 *
 * Following production design patterns:
 * - Each HTTP stream (request/response pair) has its own context
 * - Supports HTTP/2 concurrent streams on single connection
 * - Context is passed through callbacks, filters remain stateless
 */

#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>

#include "mcp/buffer.h"

namespace mcp {
namespace filter {

/**
 * Per-stream HTTP context
 * Each HTTP/2 stream or HTTP/1.1 request gets its own context
 */
class HttpStreamContext {
 public:
  // Stream identifier (for HTTP/2)
  uint32_t stream_id = 0;

  // Request data accumulated during processing
  std::string method;
  std::string path;
  std::string url;  // Full URL for request
  std::map<std::string, std::string> headers;
  std::string body;
  bool keep_alive = true;

  // Response data
  std::string status;         // Response status for client mode
  std::string accept_header;  // Accept header for content negotiation

  // HTTP version from request (for transparent response formatting)
  uint8_t http_major = 1;
  uint8_t http_minor = 1;

  // Routing decision
  bool should_handle = false;      // Whether routing filter will handle this
  bool headers_forwarded = false;  // Whether headers were passed to next filter

  // Response callback if handled by routing filter
  std::function<void()> send_response;

  void reset() {
    stream_id = 0;
    method.clear();
    path.clear();
    url.clear();
    headers.clear();
    body.clear();
    keep_alive = true;
    status.clear();
    accept_header.clear();
    http_major = 1;
    http_minor = 1;
    should_handle = false;
    headers_forwarded = false;
    send_response = nullptr;
  }
};

using HttpStreamContextPtr = std::shared_ptr<HttpStreamContext>;

/**
 * Stream context manager
 * Manages contexts for concurrent HTTP streams
 */
class HttpStreamContextManager {
 public:
  // Get or create context for a stream
  HttpStreamContextPtr getOrCreateContext(uint32_t stream_id) {
    auto it = contexts_.find(stream_id);
    if (it != contexts_.end()) {
      return it->second;
    }
    auto context = std::make_shared<HttpStreamContext>();
    context->stream_id = stream_id;
    contexts_[stream_id] = context;
    return context;
  }

  // Remove context when stream completes
  void removeContext(uint32_t stream_id) { contexts_.erase(stream_id); }

  // Get context if exists
  HttpStreamContextPtr getContext(uint32_t stream_id) {
    auto it = contexts_.find(stream_id);
    return (it != contexts_.end()) ? it->second : nullptr;
  }

 private:
  std::map<uint32_t, HttpStreamContextPtr> contexts_;
};

}  // namespace filter
}  // namespace mcp