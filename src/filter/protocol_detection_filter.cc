/**
 * Protocol Detection Filter Implementation
 */

#include "mcp/filter/protocol_detection_filter.h"

#include <algorithm>
#include <cctype>
#include <cstring>

#include "mcp/buffer.h"
#include "mcp/filter/http_routing_filter.h"
#include "mcp/filter/sse_codec_filter.h"
#include "mcp/network/connection.h"

namespace mcp {
namespace filter {

// Define static constants
constexpr size_t ProtocolDetectionFilter::kMaxDetectionBytes;

// HTTP methods to detect
static const std::vector<std::string> kHttpMethods = {
    "GET",     "POST",  "PUT",     "DELETE", "HEAD",
    "OPTIONS", "PATCH", "CONNECT", "TRACE"};

ProtocolDetectionFilter::ProtocolDetectionFilter(ProtocolHandler handler)
    : protocol_handler_(std::move(handler)) {}

network::FilterStatus ProtocolDetectionFilter::onNewConnection() {
  // Reset detection state for new connection
  detection_complete_ = false;
  detected_protocol_ = DetectedProtocol::Unknown;

  // Create new detection buffer
  detection_buffer_ = std::make_unique<OwnedBuffer>();

  return network::FilterStatus::Continue;
}

network::FilterStatus ProtocolDetectionFilter::onData(Buffer& data,
                                                      bool end_stream) {
  if (detection_complete_) {
    // Detection done, pass through
    return network::FilterStatus::Continue;
  }

  // Add data to detection buffer
  detection_buffer_->move(data);

  // Try to detect protocol
  auto result = detectProtocol(*detection_buffer_);

  if (result.protocol != DetectedProtocol::Unknown) {
    // Protocol detected
    completeDetection(result);

    // Move remaining data back to input buffer
    data.move(*detection_buffer_);

    // Continue with detected protocol filters
    return network::FilterStatus::Continue;
  }

  // Check if we've buffered too much
  if (detection_buffer_->length() >= kMaxDetectionBytes) {
    // Default to JSON-RPC if we can't determine
    result.protocol = DetectedProtocol::JsonRpc;
    completeDetection(result);

    // Move data back
    data.move(*detection_buffer_);
    return network::FilterStatus::Continue;
  }

  // Need more data
  return network::FilterStatus::StopIteration;
}

network::FilterStatus ProtocolDetectionFilter::onWrite(Buffer& data,
                                                       bool end_stream) {
  // Pass through writes
  return network::FilterStatus::Continue;
}

ProtocolDetectionResult ProtocolDetectionFilter::detectProtocol(
    const Buffer& data) {
  ProtocolDetectionResult result;

  size_t data_len = data.length();
  if (data_len < kMinDetectionBytes) {
    result.needs_more_data = true;
    return result;
  }

  // Create a temporary copy to linearize (since linearize is non-const)
  size_t inspect_len = std::min(data_len, kMaxDetectionBytes);
  std::vector<char> buffer_copy(inspect_len);
  data.copyOut(0, inspect_len, buffer_copy.data());
  const char* bytes = buffer_copy.data();
  result.bytes_inspected = inspect_len;

  // Check for HTTP request
  if (isHttpRequest(data)) {
    result.protocol = DetectedProtocol::HTTP;

    // Extract method and path
    std::string first_line;
    const char* line_end =
        static_cast<const char*>(memchr(bytes, '\n', inspect_len));
    if (line_end) {
      size_t line_len = line_end - bytes;
      if (line_len > 0 && bytes[line_len - 1] == '\r') {
        line_len--;
      }
      first_line.assign(bytes, line_len);
    }

    // Parse HTTP request line
    size_t space1 = first_line.find(' ');
    if (space1 != std::string::npos) {
      result.http_method = first_line.substr(0, space1);
      size_t space2 = first_line.find(' ', space1 + 1);
      if (space2 != std::string::npos) {
        result.http_path = first_line.substr(space1 + 1, space2 - space1 - 1);
      }
    }

    // Check for SSE request (Accept: text/event-stream)
    auto headers = extractHttpHeaders(data);
    auto accept_it = headers.find("accept");
    if (accept_it != headers.end() &&
        accept_it->second.find("text/event-stream") != std::string::npos) {
      result.is_sse_request = true;
      result.protocol = DetectedProtocol::SSE;
    }

    return result;
  }

  // Check for JSON-RPC
  if (isJsonRpcRequest(data)) {
    result.protocol = DetectedProtocol::JsonRpc;
    return result;
  }

  // Check if we need more data
  // Look for complete JSON object or HTTP request
  bool has_json_start = (bytes[0] == '{');
  bool has_http_like = false;
  for (const auto& method : kHttpMethods) {
    if (inspect_len >= method.length() &&
        memcmp(bytes, method.c_str(), method.length()) == 0) {
      has_http_like = true;
      break;
    }
  }

  if (has_json_start || has_http_like) {
    // Could be either, need more data to determine
    result.needs_more_data = true;
  } else {
    // Doesn't look like supported protocol, default to JSON-RPC
    result.protocol = DetectedProtocol::JsonRpc;
  }

  return result;
}

bool ProtocolDetectionFilter::isHttpRequest(const Buffer& data) {
  size_t data_len = data.length();
  if (data_len < 16) {  // Minimum for "GET / HTTP/1.0\r\n"
    return false;
  }

  // Check if starts with HTTP method
  size_t check_len = std::min(data_len, size_t(128));
  std::vector<char> buffer_copy(check_len);
  data.copyOut(0, check_len, buffer_copy.data());
  const char* bytes = buffer_copy.data();

  for (const auto& method : kHttpMethods) {
    size_t method_len = method.length();
    if (data_len >= method_len + 1 &&
        memcmp(bytes, method.c_str(), method_len) == 0 &&
        bytes[method_len] == ' ') {
      // Found HTTP method followed by space
      // Look for HTTP version in first line
      const char* line_end = static_cast<const char*>(
          memchr(bytes, '\n', std::min(data_len, size_t(128))));
      if (line_end) {
        std::string first_line(bytes, line_end - bytes);
        if (first_line.find(" HTTP/") != std::string::npos) {
          return true;
        }
      }
    }
  }

  return false;
}

bool ProtocolDetectionFilter::isJsonRpcRequest(const Buffer& data) {
  size_t data_len = data.length();
  if (data_len < 2) {
    return false;
  }

  size_t check_len = std::min(data_len, size_t(256));
  std::vector<char> buffer_copy(check_len);
  data.copyOut(0, check_len, buffer_copy.data());
  const char* bytes = buffer_copy.data();

  // Skip whitespace
  size_t pos = 0;
  while (pos < data_len && std::isspace(bytes[pos])) {
    pos++;
  }

  if (pos >= data_len) {
    return false;
  }

  // Check for JSON object start
  if (bytes[pos] != '{') {
    return false;
  }

  // Look for "jsonrpc" field
  std::string start(bytes + pos, std::min(data_len - pos, size_t(256)));
  if (start.find("\"jsonrpc\"") != std::string::npos ||
      start.find("'jsonrpc'") != std::string::npos) {
    return true;
  }

  // Also check for "method" field (common in JSON-RPC)
  if (start.find("\"method\"") != std::string::npos ||
      start.find("'method'") != std::string::npos) {
    // Likely JSON-RPC even without explicit version field
    return true;
  }

  return false;
}

std::map<std::string, std::string> ProtocolDetectionFilter::extractHttpHeaders(
    const Buffer& data) {
  std::map<std::string, std::string> headers;

  size_t data_len = data.length();
  size_t check_len = std::min(data_len, kMaxDetectionBytes);
  std::vector<char> buffer_copy(check_len);
  data.copyOut(0, check_len, buffer_copy.data());
  const char* bytes = buffer_copy.data();

  // Find end of headers (empty line)
  const char* headers_end =
      static_cast<const char*>(memmem(bytes, data_len, "\r\n\r\n", 4));
  if (!headers_end) {
    headers_end = static_cast<const char*>(memmem(bytes, data_len, "\n\n", 2));
    if (!headers_end) {
      return headers;  // Headers not complete
    }
  }

  // Skip first line (request line)
  const char* line_start =
      static_cast<const char*>(memchr(bytes, '\n', data_len));
  if (!line_start) {
    return headers;
  }
  line_start++;  // Move past newline

  // Parse headers
  while (line_start < headers_end) {
    const char* line_end = static_cast<const char*>(
        memchr(line_start, '\n', headers_end - line_start));
    if (!line_end) {
      break;
    }

    size_t line_len = line_end - line_start;
    if (line_len > 0 && line_start[line_len - 1] == '\r') {
      line_len--;
    }

    // Parse header line
    const char* colon =
        static_cast<const char*>(memchr(line_start, ':', line_len));
    if (colon) {
      // Extract header name (lowercase)
      std::string name(line_start, colon - line_start);
      std::transform(name.begin(), name.end(), name.begin(), ::tolower);

      // Extract header value (trim whitespace)
      const char* value_start = colon + 1;
      while (value_start < line_end && std::isspace(*value_start)) {
        value_start++;
      }
      const char* value_end = line_start + line_len;
      while (value_end > value_start && std::isspace(*(value_end - 1))) {
        value_end--;
      }

      if (value_end > value_start) {
        headers[name] = std::string(value_start, value_end - value_start);
      }
    }

    line_start = line_end + 1;
  }

  return headers;
}

void ProtocolDetectionFilter::completeDetection(
    const ProtocolDetectionResult& result) {
  detection_complete_ = true;
  detected_protocol_ = result.protocol;

  // Invoke protocol handler
  if (protocol_handler_) {
    protocol_handler_(result.protocol, result);
  }
}

void ProtocolDetectionFilter::injectBufferedData() {
  if (detection_buffer_ && detection_buffer_->length() > 0 && read_callbacks_) {
    // Continue reading will pass buffered data to next filter
    read_callbacks_->continueReading();
  }
}

// ProtocolRoutingFilterFactory implementation

network::FilterSharedPtr
ProtocolRoutingFilterFactory::createProtocolDetectionFilter() {
  // Create a detection filter that will handle protocol detection
  auto filter = std::make_shared<ProtocolDetectionFilter>(
      [this](DetectedProtocol protocol, const ProtocolDetectionResult& result) {
        // Protocol handler will be invoked when detection completes
        // The actual connection handling happens in the filter's callbacks
      });

  return filter;
}

std::vector<network::FilterSharedPtr>
ProtocolRoutingFilterFactory::createHttpFilterChain(
    network::Connection& connection, bool is_sse_request) {
  std::vector<network::FilterSharedPtr> filters;

  if (!config_.enable_http) {
    return filters;
  }

  // Note: In production, this would create the full HTTP filter chain
  // including HttpCodecFilter, HttpRoutingFilter, and SSE filter if needed
  // For now, we return empty to avoid incomplete type issues

  // Notify callback
  if (config_.on_http_connection) {
    config_.on_http_connection(connection);
  }

  return filters;
}

std::vector<network::FilterSharedPtr>
ProtocolRoutingFilterFactory::createMcpFilterChain(
    network::Connection& connection) {
  std::vector<network::FilterSharedPtr> filters;

  if (!config_.enable_mcp) {
    return filters;
  }

  // Create MCP JSON-RPC filter
  // Note: This would need proper callbacks and dispatcher in real
  // implementation For now, we just demonstrate the structure auto mcp_filter =
  // std::make_shared<JsonRpcProtocolFilter>(callbacks, dispatcher, true);
  // filters.push_back(mcp_filter);

  // Notify callback
  if (config_.on_mcp_connection) {
    config_.on_mcp_connection(connection);
  }

  return filters;
}

void ProtocolRoutingFilterFactory::handleDetectedProtocol(
    network::Connection& connection,
    DetectedProtocol protocol,
    const ProtocolDetectionResult& result) {
  std::vector<network::FilterSharedPtr> filters;

  switch (protocol) {
    case DetectedProtocol::HTTP:
    case DetectedProtocol::SSE:
      filters = createHttpFilterChain(connection, result.is_sse_request);
      break;

    case DetectedProtocol::JsonRpc:
      filters = createMcpFilterChain(connection);
      break;

    default:
      // Unknown protocol, connection will be closed
      return;
  }

  // Note: In production, filters would be added through the connection's filter
  // manager The exact mechanism depends on the network layer implementation For
  // now, this demonstrates the architectural approach
}

// TransparentProtocolHandler implementation

bool TransparentProtocolHandler::isMcpHttpRequest(
    const std::string& path,
    const std::map<std::string, std::string>& headers) const {
  // Check if path matches MCP endpoints
  if (path == config_.mcp_path || path == config_.mcp_sse_path) {
    return true;
  }

  // Check for MCP-specific headers
  auto content_type = headers.find("content-type");
  if (content_type != headers.end()) {
    if (content_type->second.find("application/json-rpc") !=
            std::string::npos ||
        content_type->second.find("application/vnd.mcp") != std::string::npos) {
      return true;
    }
  }

  return false;
}

void TransparentProtocolHandler::routeHttpRequest(
    const std::string& method,
    const std::string& path,
    const std::map<std::string, std::string>& headers,
    std::function<void(bool is_mcp)> callback) {
  bool is_mcp = isMcpHttpRequest(path, headers);

  if (is_mcp) {
    // Check if SSE is preferred
    if (config_.prefer_sse_for_mcp) {
      auto accept = headers.find("accept");
      if (accept != headers.end() &&
          accept->second.find("text/event-stream") != std::string::npos) {
        // Client accepts SSE, use it for MCP
      }
    }
  }

  callback(is_mcp);
}

bool TransparentProtocolHandler::convertHttpToMcp(
    const std::string& body, std::string& out_json_rpc) const {
  // Parse JSON body as MCP message
  try {
    // Check if already JSON-RPC format
    if (body.find("\"jsonrpc\"") != std::string::npos) {
      // Already JSON-RPC format
      out_json_rpc = body;
      return true;
    }

    // Convert plain JSON to JSON-RPC
    // Implementation would properly parse and convert
    out_json_rpc = body;
    return true;
  } catch (...) {
    return false;
  }
}

std::string TransparentProtocolHandler::convertMcpToHttp(
    const std::string& json_rpc, bool use_sse) const {
  std::string response;

  if (use_sse) {
    // Format as SSE event
    response = "event: message\n";
    response += "data: ";
    // Serialize message to JSON
    response += "{}";  // Would use actual JSON serialization
    response += "\n\n";
  } else {
    // Format as plain JSON response
    response = "HTTP/1.1 200 OK\r\n";
    response += "Content-Type: application/json\r\n";
    // Add content length
    std::string json = "{}";  // Would use actual JSON serialization
    response += "Content-Length: " + std::to_string(json.length()) + "\r\n";
    response += "\r\n";
    response += json;
  }

  return response;
}

}  // namespace filter
}  // namespace mcp