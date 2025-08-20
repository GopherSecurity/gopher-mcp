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

HttpRoutingFilter::HttpRoutingFilter(event::Dispatcher& dispatcher, bool is_server)
    : dispatcher_(dispatcher), is_server_(is_server) {
  
  // Create HTTP codec filter that will parse HTTP for us
  http_codec_ = std::make_unique<HttpCodecFilter>(*this, dispatcher_, is_server_);
  
  // Set up default 404 handler
  default_handler_ = [](const RequestContext& req) {
    Response resp;
    resp.status_code = 404;
    resp.headers["content-type"] = "application/json";
    resp.body = "{\"error\":\"Not Found\",\"path\":\"" + req.path + "\"}";
    resp.headers["content-length"] = std::to_string(resp.body.length());
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

network::FilterStatus HttpRoutingFilter::onData(Buffer& data, bool end_stream) {
  // Forward data to HTTP codec for parsing
  // The codec will call our MessageCallbacks methods when it parses the HTTP
  return http_codec_->onData(data, end_stream);
}

network::FilterStatus HttpRoutingFilter::onNewConnection() {
  // Reset state for new connection
  current_request_ = RequestContext();
  request_complete_ = false;
  response_buffer_.drain(response_buffer_.length());
  
  // Initialize HTTP codec for new connection
  return http_codec_->onNewConnection();
}

network::FilterStatus HttpRoutingFilter::onWrite(Buffer& data, bool end_stream) {
  // For outgoing data, pass through the HTTP codec
  return http_codec_->onWrite(data, end_stream);
}

void HttpRoutingFilter::initializeReadFilterCallbacks(
    network::ReadFilterCallbacks& callbacks) {
  read_callbacks_ = &callbacks;
  // Also initialize the HTTP codec's callbacks
  http_codec_->initializeReadFilterCallbacks(callbacks);
}

void HttpRoutingFilter::initializeWriteFilterCallbacks(
    network::WriteFilterCallbacks& callbacks) {
  write_callbacks_ = &callbacks;
  // Also initialize the HTTP codec's callbacks
  http_codec_->initializeWriteFilterCallbacks(callbacks);
}

// HttpCodecFilter::MessageCallbacks implementation
void HttpRoutingFilter::onHeaders(const std::map<std::string, std::string>& headers,
                                  bool keep_alive) {
  // Extract method and path from headers
  current_request_.method = extractMethod(headers);
  current_request_.path = extractPath(headers);
  current_request_.headers = headers;
  current_request_.keep_alive = keep_alive;
}

void HttpRoutingFilter::onBody(const std::string& data, bool end_stream) {
  // Accumulate body data
  current_request_.body += data;
  
  if (end_stream) {
    request_complete_ = true;
  }
}

void HttpRoutingFilter::onMessageComplete() {
  // Request is complete, process it
  request_complete_ = true;
  processRequest();
}

void HttpRoutingFilter::onError(const std::string& error) {
  // Handle HTTP parsing error
  // Send a 400 Bad Request response
  Response resp;
  resp.status_code = 400;
  resp.headers["content-type"] = "application/json";
  resp.body = "{\"error\":\"Bad Request\",\"message\":\"" + error + "\"}";
  resp.headers["content-length"] = std::to_string(resp.body.length());
  sendResponse(resp);
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
  
  // Check if we should continue processing (status_code = 0)
  // This allows certain requests to pass through to next filter
  if (response.status_code == 0) {
    // Don't send a response, let the request pass through
    // The next filter in the chain will handle it
    return;
  }
  
  // Send response
  sendResponse(response);
  
  // Reset for next request if keep-alive
  if (current_request_.keep_alive) {
    current_request_ = RequestContext();
    request_complete_ = false;
  }
}

void HttpRoutingFilter::sendResponse(const Response& response) {
  // Use HTTP codec's encoder to properly format the response
  auto& encoder = http_codec_->messageEncoder();
  
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
  encoder.encodeHeaders(status, response.headers, !has_body);
  
  // Encode body if present
  if (has_body) {
    // Create a buffer with the response body
    OwnedBuffer body_buffer;
    body_buffer.add(response.body);
    encoder.encodeData(body_buffer, true);
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