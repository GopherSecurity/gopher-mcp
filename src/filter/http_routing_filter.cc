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
  // Check if this looks like an HTTP request
  // We need to peek at the data to see if it's an HTTP request for our endpoints
  
  // Quick check: Does it start with a known HTTP method?
  if (data.length() > 10) {
    // Peek at the first bytes without consuming
    std::string peek(static_cast<const char*>(data.linearize(100)), 
                     std::min(size_t(100), data.length()));
    
    // Check if it's an HTTP request for one of our registered endpoints
    bool is_registered_endpoint = false;
    std::string method;
    std::string path;
    
    // Simple HTTP request line parsing
    if (peek.find("GET ") == 0 || peek.find("POST ") == 0 || 
        peek.find("PUT ") == 0 || peek.find("DELETE ") == 0) {
      
      size_t method_end = peek.find(' ');
      size_t path_start = method_end + 1;
      size_t path_end = peek.find(' ', path_start);
      
      if (method_end != std::string::npos && path_end != std::string::npos) {
        method = peek.substr(0, method_end);
        path = peek.substr(path_start, path_end - path_start);
        
        // Check if we have a handler for this endpoint
        std::string key = buildRouteKey(method, path);
        is_registered_endpoint = (handlers_.find(key) != handlers_.end());
      }
    }
    
    // If it's one of our registered endpoints, handle it
    if (is_registered_endpoint) {
      // Parse the full HTTP request
      std::string request_data(static_cast<const char*>(data.linearize(data.length())), 
                               data.length());
      
      RequestContext req;
      req.method = method;
      req.path = path;
      
      // Simple header parsing (find \r\n\r\n)
      size_t headers_end = request_data.find("\r\n\r\n");
      if (headers_end != std::string::npos) {
        // Parse headers
        size_t pos = request_data.find("\r\n");
        while (pos < headers_end) {
          size_t next_pos = request_data.find("\r\n", pos + 2);
          if (next_pos > headers_end) break;
          
          std::string header_line = request_data.substr(pos + 2, next_pos - pos - 2);
          size_t colon = header_line.find(':');
          if (colon != std::string::npos) {
            std::string name = header_line.substr(0, colon);
            std::string value = header_line.substr(colon + 1);
            // Trim leading whitespace from value
            size_t value_start = value.find_first_not_of(" \t");
            if (value_start != std::string::npos) {
              value = value.substr(value_start);
            }
            req.headers[name] = value;
          }
          pos = next_pos;
        }
        
        // Get body if present
        if (headers_end + 4 < request_data.length()) {
          req.body = request_data.substr(headers_end + 4);
        }
      }
      
      // Check keep-alive
      auto conn_header = req.headers.find("Connection");
      req.keep_alive = (conn_header == req.headers.end() || 
                       conn_header->second != "close");
      
      // Process the request and send response
      processRequest(req);
      
      // Consume the data since we handled it
      data.drain(data.length());
      
      // Stop iteration - we handled this request
      return network::FilterStatus::StopIteration;
    }
  }
  
  // Not one of our endpoints - pass through to next filter (MCP protocol)
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