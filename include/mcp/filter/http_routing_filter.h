#ifndef MCP_FILTER_HTTP_ROUTING_FILTER_H
#define MCP_FILTER_HTTP_ROUTING_FILTER_H

#include <functional>
#include <map>
#include <string>
#include <memory>

#include "mcp/network/filter.h"
#include "mcp/buffer.h"
#include "mcp/filter/http_codec_filter.h"
#include "mcp/event/event_loop.h"

namespace mcp {
namespace filter {

/**
 * HTTP Routing Filter
 * 
 * This filter provides HTTP endpoint routing capabilities.
 * It composes with HttpCodecFilter for proper HTTP parsing,
 * routing requests to registered handlers based on path and method.
 * 
 * Architecture:
 * - Composes HttpCodecFilter for HTTP protocol parsing
 * - Uses MCP Buffer abstraction for all data handling
 * - Routes parsed requests to registered handlers
 * - Application registers handlers for specific paths
 * 
 * Filter chain: [Network] → [HttpCodecFilter] → [HttpRoutingFilter] → [App]
 */
class HttpRoutingFilter : public network::Filter,
                         public HttpCodecFilter::MessageCallbacks {
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
   * @param dispatcher Event dispatcher for async operations
   * @param is_server True for server mode (default), false for client mode
   */
  explicit HttpRoutingFilter(event::Dispatcher& dispatcher, bool is_server = true);
  
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
  
  // network::Filter interface
  network::FilterStatus onData(Buffer& data, bool end_stream) override;
  network::FilterStatus onNewConnection() override;
  network::FilterStatus onWrite(Buffer& data, bool end_stream) override;
  
  void initializeReadFilterCallbacks(network::ReadFilterCallbacks& callbacks) override;
  void initializeWriteFilterCallbacks(network::WriteFilterCallbacks& callbacks) override;
  
  // HttpCodecFilter::MessageCallbacks interface
  void onHeaders(const std::map<std::string, std::string>& headers,
                 bool keep_alive) override;
  void onBody(const std::string& data, bool end_stream) override;
  void onMessageComplete() override;
  void onError(const std::string& error) override;
  
private:
  // Process HTTP request and route to appropriate handler
  void processRequest();
  
  // Send HTTP response using HTTP codec
  void sendResponse(const Response& response);
  
  // Route key is "METHOD /path"
  std::string buildRouteKey(const std::string& method, const std::string& path) const;
  
  // Extract method from request line or headers
  std::string extractMethod(const std::map<std::string, std::string>& headers);
  
  // Extract path from request line or headers
  std::string extractPath(const std::map<std::string, std::string>& headers);
  
  // Components
  event::Dispatcher& dispatcher_;
  std::unique_ptr<HttpCodecFilter> http_codec_;
  bool is_server_;
  
  // Registered handlers
  std::map<std::string, HandlerFunc> handlers_;
  
  // Default handler for unmatched requests
  HandlerFunc default_handler_;
  
  // Current request being processed
  RequestContext current_request_;
  bool request_complete_{false};
  
  // Callbacks
  network::ReadFilterCallbacks* read_callbacks_{nullptr};
  network::WriteFilterCallbacks* write_callbacks_{nullptr};
  
  // Buffer for accumulating response data
  OwnedBuffer response_buffer_;
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