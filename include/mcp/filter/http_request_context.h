/**
 * HTTP Request Context
 *
 * Following production design patterns:
 * - Request state lives in a context object, not in filters
 * - Context flows through the filter chain via callbacks
 * - Filters are stateless processors
 */

#pragma once

#include <map>
#include <memory>
#include <string>

#include "mcp/buffer.h"

namespace mcp {
namespace filter {

/**
 * HTTP Request Context
 * Holds all state for a single HTTP request as it flows through filters
 */
class HttpRequestContext {
 public:
  // Request data
  std::string method;
  std::string path;
  std::map<std::string, std::string> headers;
  OwnedBuffer body;
  bool keep_alive = true;

  // Response data (if filter handles the request)
  bool handled = false;
  int response_status = 0;
  std::map<std::string, std::string> response_headers;
  std::string response_body;

  // Reset for reuse
  void reset() {
    method.clear();
    path.clear();
    headers.clear();
    body.drain(body.length());
    keep_alive = true;
    handled = false;
    response_status = 0;
    response_headers.clear();
    response_body.clear();
  }
};

using HttpRequestContextPtr = std::shared_ptr<HttpRequestContext>;

}  // namespace filter
}  // namespace mcp