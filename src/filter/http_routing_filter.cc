/**
 * HTTP Routing Filter Implementation
 *
 * Provides endpoint routing for HTTP requests using HttpCodecFilter
 * for proper HTTP protocol parsing. Uses MCP Buffer abstraction throughout.
 */

#include "mcp/filter/http_routing_filter.h"

#include <iostream>
#include <sstream>

#include "mcp/network/connection.h"

namespace mcp {
namespace filter {

HttpRoutingFilter::HttpRoutingFilter(
    HttpCodecFilter::MessageCallbacks* next_callbacks,
    HttpCodecFilter::MessageEncoder* encoder,
    bool is_server)
    : next_callbacks_(next_callbacks),
      encoder_(encoder),
      is_server_(is_server) {
  // Set up default handler that passes through to next layer
  default_handler_ = [](const RequestContext&) {
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
void HttpRoutingFilter::onHeaders(
    const std::map<std::string, std::string>& headers, bool keep_alive) {
  std::cerr << "[DEBUG] HttpRoutingFilter::onHeaders called with "
            << headers.size() << " headers, is_server=" << is_server_
            << std::endl;

  // In client mode, we're receiving responses, not requests - skip routing
  if (!is_server_) {
    std::cerr
        << "[DEBUG] HttpRoutingFilter: client mode, passing through response"
        << std::endl;
    if (next_callbacks_) {
      next_callbacks_->onHeaders(headers, keep_alive);
    }
    return;
  }

  // Server mode: route incoming requests
  std::string method = extractMethod(headers);
  std::string path = extractPath(headers);

  std::cerr << "[DEBUG] HttpRoutingFilter: method=" << method
            << " path=" << path << std::endl;

  // Check if we have a handler for this endpoint
  std::string key = buildRouteKey(method, path);
  auto handler_it = handlers_.find(key);

  if (handler_it != handlers_.end()) {
    // We have a handler - execute it immediately with available info
    RequestContext ctx;
    ctx.method = method;
    ctx.path = path;
    ctx.headers = headers;
    ctx.keep_alive = keep_alive;
    // Note: body not available yet in onHeaders

    Response resp = handler_it->second(ctx);
    if (resp.status_code != 0) {
      // Handler wants to handle this - send response immediately
      // This is appropriate for endpoints that don't need the body
      sendResponse(resp);
      return;  // Don't forward to next layer
    }
  }

  // No handler or handler returned 0 - pass through
  if (next_callbacks_) {
    next_callbacks_->onHeaders(headers, keep_alive);
  }
}

void HttpRoutingFilter::onBody(const std::string& data, bool end_stream) {
  // Stateless - always pass through
  // If we handled the request in onHeaders, this won't be called
  if (next_callbacks_) {
    next_callbacks_->onBody(data, end_stream);
  }
}

void HttpRoutingFilter::onMessageComplete() {
  std::cerr << "[DEBUG] HttpRoutingFilter::onMessageComplete called"
            << std::endl;

  // Stateless - always pass through
  // If we handled the request in onHeaders, this won't be called
  if (next_callbacks_) {
    next_callbacks_->onMessageComplete();
  }
}

void HttpRoutingFilter::onError(const std::string& error) {
  // Stateless - always pass through errors
  if (next_callbacks_) {
    next_callbacks_->onError(error);
  }
}

void HttpRoutingFilter::sendResponse(const Response& response) {
  std::cerr << "[DEBUG] HttpRoutingFilter::sendResponse called with status "
            << response.status_code << std::endl;

  // Build complete HTTP response
  std::ostringstream http_response;

  // Status line (use HTTP/1.1 for now)
  http_response << "HTTP/1.1 " << response.status_code << " ";

  // Add status text based on code
  switch (response.status_code) {
    case 200:
      http_response << "OK";
      break;
    case 201:
      http_response << "Created";
      break;
    case 204:
      http_response << "No Content";
      break;
    case 400:
      http_response << "Bad Request";
      break;
    case 404:
      http_response << "Not Found";
      break;
    case 500:
      http_response << "Internal Server Error";
      break;
    default:
      http_response << "Unknown";
      break;
  }
  http_response << "\r\n";

  // Add headers
  for (const auto& header : response.headers) {
    http_response << header.first << ": " << header.second << "\r\n";
  }

  // End headers
  http_response << "\r\n";

  // Add body if present
  if (!response.body.empty()) {
    http_response << response.body;
  }

  // Send the complete response directly through write callbacks
  if (write_callbacks_) {
    std::string response_str = http_response.str();
    OwnedBuffer response_buffer;
    response_buffer.add(response_str);
    write_callbacks_->connection().write(response_buffer, false);

    std::cerr << "[DEBUG] HttpRoutingFilter sent response: "
              << response_str.length() << " bytes" << std::endl;
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
  // For now, we'll parse it from the URL header which contains the full request
  // line or look for a method header that some parsers add

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
std::shared_ptr<HttpRoutingFilter>
HttpRoutingFilterFactory::createWithHealthCheck() {
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

}  // namespace filter
}  // namespace mcp