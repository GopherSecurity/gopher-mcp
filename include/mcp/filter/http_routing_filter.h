#ifndef MCP_FILTER_HTTP_ROUTING_FILTER_H
#define MCP_FILTER_HTTP_ROUTING_FILTER_H

#include <functional>
#include <map>
#include <string>

#include "mcp/network/filter.h"
#include "mcp/buffer.h"

namespace mcp {
namespace filter {

/**
 * HTTP Routing Filter
 * 
 * This filter provides HTTP endpoint routing capabilities.
 * It sits between the HTTP codec and the application layer,
 * routing requests to registered handlers based on path and method.
 * 
 * Architecture:
 * - Maintains separation of concerns
 * - Filter chain handles protocol translation
 * - This filter handles endpoint routing
 * - Application registers handlers for specific paths
 */
class HttpRoutingFilter : public network::Filter {
public:
  // HTTP request context passed to handlers
  struct RequestContext {
    std::string method;
    std::string path;
    std::map<std::string, std::string> headers;
    std::string body;
    bool keep_alive;
  };
  
  // HTTP response returned by handlers
  struct Response {
    int status_code = 200;
    std::map<std::string, std::string> headers;
    std::string body;
  };
  
  // Handler function type
  using HandlerFunc = std::function<Response(const RequestContext&)>;
  
  /**
   * Constructor
   * @param write_callbacks Write callbacks for sending responses
   */
  explicit HttpRoutingFilter(network::WriteFilterCallbacks* write_callbacks = nullptr);
  
  /**
   * Register a handler for a specific path and method
   * @param method HTTP method (GET, POST, etc.)
   * @param path URL path (e.g., "/health")
   * @param handler Function to handle the request
   */
  void registerHandler(const std::string& method, 
                       const std::string& path,
                       HandlerFunc handler);
  
  /**
   * Register a default handler for unmatched requests
   * @param handler Function to handle unmatched requests
   */
  void registerDefaultHandler(HandlerFunc handler);
  
  // Filter interface
  network::FilterStatus onData(Buffer& data, bool end_stream) override;
  network::FilterStatus onNewConnection() override;
  network::FilterStatus onWrite(Buffer& data, bool end_stream) override;
  
  void initializeReadFilterCallbacks(network::ReadFilterCallbacks& callbacks) override;
  void initializeWriteFilterCallbacks(network::WriteFilterCallbacks& callbacks) override;
  
private:
  // Process HTTP request and route to appropriate handler
  void processRequest(const RequestContext& request);
  
  // Send HTTP response
  void sendResponse(const Response& response);
  
  // Build response buffer
  std::unique_ptr<Buffer> buildResponseBuffer(const Response& response);
  
  // Route key is "METHOD /path"
  std::string buildRouteKey(const std::string& method, const std::string& path) const;
  
  // Registered handlers
  std::map<std::string, HandlerFunc> handlers_;
  
  // Default handler for unmatched requests
  HandlerFunc default_handler_;
  
  // Current request being processed
  RequestContext current_request_;
  
  // Callbacks
  network::ReadFilterCallbacks* read_callbacks_{nullptr};
  network::WriteFilterCallbacks* write_callbacks_{nullptr};
  
  // State
  bool headers_complete_{false};
  bool processing_request_{false};
};

/**
 * HTTP Routing Filter Factory
 * 
 * Creates HTTP routing filters with pre-configured handlers
 */
class HttpRoutingFilterFactory {
public:
  /**
   * Create a filter with standard health check endpoint
   */
  static std::shared_ptr<HttpRoutingFilter> createWithHealthCheck();
  
  /**
   * Create a filter with custom handlers
   */
  static std::shared_ptr<HttpRoutingFilter> createWithHandlers(
      const std::map<std::string, HttpRoutingFilter::HandlerFunc>& handlers);
};

} // namespace filter
} // namespace mcp

#endif // MCP_FILTER_HTTP_ROUTING_FILTER_H