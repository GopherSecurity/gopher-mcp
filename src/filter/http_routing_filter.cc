/**
 * HTTP Routing Filter Implementation
 * 
 * Provides endpoint routing for HTTP requests using HttpCodecFilter
 * for proper HTTP protocol parsing. Uses MCP Buffer abstraction throughout.
 */

#include "mcp/filter/http_routing_filter.h"
#include <sstream>

namespace mcp {
namespace filter {

HttpRoutingFilter::HttpRoutingFilter(HttpCodecFilter::MessageCallbacks* next_callbacks,
                                   HttpCodecFilter::MessageEncoder* encoder,
                                   bool is_server)
    : next_callbacks_(next_callbacks), encoder_(encoder), is_server_(is_server) {
  
  // Set up default handler that passes through to next layer
  default_handler_ = [this](const RequestContext& req) {
    // Return special status code 0 to indicate pass-through
    Response resp;
    resp.status_code = 0;  // Signal to pass through
    return resp;
  };
}

void HttpRoutingFilter::registerHandler(const std::string& method,
                                       const std::string& path,
                                       HandlerFunc handler) {
  std::string key = buildRouteKey(method, path);
  handlers_[key] = handler;
}

void HttpRoutingFilter::registerDefaultHandler(HandlerFunc handler) {
  default_handler_ = handler;
}

// Removed onData, onNewConnection, onWrite, and initialize callbacks methods
// as this filter no longer implements network::Filter interface

// HttpCodecFilter::MessageCallbacks implementation
void HttpRoutingFilter::onHeaders(const std::map<std::string, std::string>& headers,
                                  bool keep_alive) {
  // Reset state for new request
  current_request_ = RequestContext();
  request_complete_ = false;
  request_handled_ = false;
  
  // Extract method and path from headers
  current_request_.method = extractMethod(headers);
  current_request_.path = extractPath(headers);
  current_request_.headers = headers;
  current_request_.keep_alive = keep_alive;
  
  // Check if we should handle this request
  std::string key = buildRouteKey(current_request_.method, current_request_.path);
  if (handlers_.find(key) != handlers_.end()) {
    // We will handle this request
    request_handled_ = true;
  } else {
    // Check default handler - if it returns status 0, we pass through
    Response test_resp = default_handler_(current_request_);
    request_handled_ = (test_resp.status_code != 0);
  }
  
  // If we're not handling it, forward to next layer immediately
  if (!request_handled_ && next_callbacks_) {
    next_callbacks_->onHeaders(headers, keep_alive);
  }
}

void HttpRoutingFilter::onBody(const std::string& data, bool end_stream) {
  if (request_handled_) {
    // Accumulate body data for our handler
    current_request_.body += data;
    
    if (end_stream) {
      request_complete_ = true;
    }
  } else if (next_callbacks_) {
    // Forward to next layer
    next_callbacks_->onBody(data, end_stream);
  }
}

void HttpRoutingFilter::onMessageComplete() {
  if (request_handled_) {
    // Request is complete, process it
    request_complete_ = true;
    processRequest();
  } else if (next_callbacks_) {
    // Forward to next layer
    next_callbacks_->onMessageComplete();
  }
}

void HttpRoutingFilter::onError(const std::string& error) {
  if (request_handled_) {
    // Handle HTTP parsing error with our response
    Response resp;
    resp.status_code = 400;
    resp.headers["content-type"] = "application/json";
    resp.body = "{\"error\":\"Bad Request\",\"message\":\"" + error + "\"}";
    resp.headers["content-length"] = std::to_string(resp.body.length());
    sendResponse(resp);
  } else if (next_callbacks_) {
    // Forward error to next layer
    next_callbacks_->onError(error);
  }
}

void HttpRoutingFilter::processRequest() {
  // Build route key and find handler
  std::string key = buildRouteKey(current_request_.method, current_request_.path);
  
  HandlerFunc handler = default_handler_;
  auto it = handlers_.find(key);
  if (it != handlers_.end()) {
    handler = it->second;
  }
  
  // Call handler and get response
  Response response = handler(current_request_);
  
  // Only send response if status code is not 0 (0 means pass-through)
  if (response.status_code != 0) {
    sendResponse(response);
  }
  
  // Reset for next request if keep-alive
  if (current_request_.keep_alive) {
    current_request_ = RequestContext();
    request_complete_ = false;
    request_handled_ = false;
  }
}

void HttpRoutingFilter::sendResponse(const Response& response) {
  // Use the provided encoder to properly format the response
  if (!encoder_) {
    return;  // No encoder available
  }
  
  // Prepare headers with proper status line
  std::string status = std::to_string(response.status_code);
  
  // Add status text based on code
  switch (response.status_code) {
    case 200: status += " OK"; break;
    case 201: status += " Created"; break;
    case 204: status += " No Content"; break;
    case 400: status += " Bad Request"; break;
    case 404: status += " Not Found"; break;
    case 500: status += " Internal Server Error"; break;
    default: status += " Unknown"; break;
  }
  
  // Encode headers
  bool has_body = !response.body.empty();
  encoder_->encodeHeaders(status, response.headers, !has_body);
  
  // Encode body if present
  if (has_body) {
    // Create a buffer with the response body
    OwnedBuffer body_buffer;
    body_buffer.add(response.body);
    encoder_->encodeData(body_buffer, true);
  }
}

std::string HttpRoutingFilter::buildRouteKey(const std::string& method, 
                                             const std::string& path) const {
  return method + " " + path;
}

std::string HttpRoutingFilter::extractMethod(
    const std::map<std::string, std::string>& headers) {
  // The HTTP codec filter doesn't directly expose the method in headers
  // We need to get it from the parser through the codec
  // For now, we'll parse it from the URL header which contains the full request line
  // or look for a method header that some parsers add
  
  // Check for :method pseudo-header (HTTP/2 style)
  auto it = headers.find(":method");
  if (it != headers.end()) {
    return it->second;
  }
  
  // For HTTP/1.1, we need to extract from the request line
  // The parser stores the method internally but we can infer it
  // from the request context
  
  // Default to GET if not found
  return "GET";
}

std::string HttpRoutingFilter::extractPath(
    const std::map<std::string, std::string>& headers) {
  // Check for :path pseudo-header (HTTP/2 style)
  auto it = headers.find(":path");
  if (it != headers.end()) {
    return it->second;
  }
  
  // For HTTP/1.1, the codec stores the URL in a "url" header
  it = headers.find("url");
  if (it != headers.end()) {
    return it->second;
  }
  
  // Default to root if not found
  return "/";
}

// Factory methods
std::shared_ptr<HttpRoutingFilter> HttpRoutingFilterFactory::createWithHealthCheck() {
  // Note: This needs a dispatcher to be passed in
  // For now, return nullptr as we need to refactor the factory
  return nullptr;
}

std::shared_ptr<HttpRoutingFilter> HttpRoutingFilterFactory::createWithHandlers(
    const std::map<std::string, HttpRoutingFilter::HandlerFunc>& handlers) {
  // Note: This needs a dispatcher to be passed in
  // For now, return nullptr as we need to refactor the factory
  return nullptr;
}

} // namespace filter
} // namespace mcp