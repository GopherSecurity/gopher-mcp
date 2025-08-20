/**
 * HTTP Routing Filter Implementation
 * 
 * Provides endpoint routing for HTTP requests.
 * Maintains clean separation between protocol handling (filter chain)
 * and application logic (registered handlers).
 */

#include "mcp/filter/http_routing_filter.h"
#include <sstream>

namespace mcp {
namespace filter {

HttpRoutingFilter::HttpRoutingFilter(network::WriteFilterCallbacks* write_callbacks)
    : write_callbacks_(write_callbacks) {
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
  // This filter processes complete HTTP requests after the HTTP codec
  // has parsed them. It expects to receive parsed headers through callbacks
  // from the HTTP codec filter that should be before this in the chain.
  
  // For now, pass through to next filter
  // The actual routing happens in the callbacks from HTTP codec
  return network::FilterStatus::Continue;
}

network::FilterStatus HttpRoutingFilter::onNewConnection() {
  // Reset state for new connection
  headers_complete_ = false;
  processing_request_ = false;
  current_request_ = RequestContext();
  return network::FilterStatus::Continue;
}

network::FilterStatus HttpRoutingFilter::onWrite(Buffer& data, bool end_stream) {
  // Pass through write data
  return network::FilterStatus::Continue;
}

void HttpRoutingFilter::initializeReadFilterCallbacks(
    network::ReadFilterCallbacks& callbacks) {
  read_callbacks_ = &callbacks;
}

void HttpRoutingFilter::initializeWriteFilterCallbacks(
    network::WriteFilterCallbacks& callbacks) {
  write_callbacks_ = &callbacks;
}

void HttpRoutingFilter::processRequest(const RequestContext& request) {
  // Find handler for this route
  std::string key = buildRouteKey(request.method, request.path);
  
  HandlerFunc handler = default_handler_;
  auto it = handlers_.find(key);
  if (it != handlers_.end()) {
    handler = it->second;
  }
  
  // Call handler and get response
  Response response = handler(request);
  
  // Send response
  sendResponse(response);
}

void HttpRoutingFilter::sendResponse(const Response& response) {
  if (!write_callbacks_) {
    return;
  }
  
  auto buffer = buildResponseBuffer(response);
  if (buffer) {
    write_callbacks_->injectWriteDataToFilterChain(*buffer, false);
  }
}

std::unique_ptr<Buffer> HttpRoutingFilter::buildResponseBuffer(const Response& response) {
  auto buffer = std::make_unique<OwnedBuffer>();
  
  // Build HTTP response
  std::stringstream ss;
  
  // Status line
  ss << "HTTP/1.1 " << response.status_code;
  switch (response.status_code) {
    case 200: ss << " OK"; break;
    case 404: ss << " Not Found"; break;
    case 500: ss << " Internal Server Error"; break;
    default: ss << " Unknown"; break;
  }
  ss << "\r\n";
  
  // Headers
  for (const auto& header : response.headers) {
    ss << header.first << ": " << header.second << "\r\n";
  }
  ss << "\r\n";
  
  // Body
  ss << response.body;
  
  buffer->add(ss.str());
  return buffer;
}

std::string HttpRoutingFilter::buildRouteKey(const std::string& method,
                                            const std::string& path) const {
  return method + " " + path;
}

// Factory implementations

std::shared_ptr<HttpRoutingFilter> HttpRoutingFilterFactory::createWithHealthCheck() {
  auto filter = std::make_shared<HttpRoutingFilter>();
  
  // Register health check handler
  filter->registerHandler("GET", "/health", [](const HttpRoutingFilter::RequestContext& req) {
    HttpRoutingFilter::Response resp;
    resp.status_code = 200;
    resp.headers["content-type"] = "application/json";
    resp.body = "{\"status\":\"healthy\",\"timestamp\":" + 
                std::to_string(std::time(nullptr)) + "}";
    resp.headers["content-length"] = std::to_string(resp.body.length());
    return resp;
  });
  
  // Register ready check handler
  filter->registerHandler("GET", "/ready", [](const HttpRoutingFilter::RequestContext& req) {
    HttpRoutingFilter::Response resp;
    resp.status_code = 200;
    resp.headers["content-type"] = "application/json";
    resp.body = "{\"ready\":true}";
    resp.headers["content-length"] = std::to_string(resp.body.length());
    return resp;
  });
  
  return filter;
}

std::shared_ptr<HttpRoutingFilter> HttpRoutingFilterFactory::createWithHandlers(
    const std::map<std::string, HttpRoutingFilter::HandlerFunc>& handlers) {
  auto filter = std::make_shared<HttpRoutingFilter>();
  
  for (const auto& handler : handlers) {
    // Parse the key to extract method and path
    size_t space_pos = handler.first.find(' ');
    if (space_pos != std::string::npos) {
      std::string method = handler.first.substr(0, space_pos);
      std::string path = handler.first.substr(space_pos + 1);
      filter->registerHandler(method, path, handler.second);
    }
  }
  
  return filter;
}

} // namespace filter
} // namespace mcp