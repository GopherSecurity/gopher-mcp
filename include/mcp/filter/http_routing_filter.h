#ifndef MCP_FILTER_HTTP_ROUTING_FILTER_H
#define MCP_FILTER_HTTP_ROUTING_FILTER_H

#include <functional>
#include <map>
#include <memory>
#include <string>

#include "mcp/buffer.h"
#include "mcp/event/event_loop.h"
#include "mcp/filter/http_codec_filter.h"
#include "mcp/network/filter.h"

namespace mcp {
namespace filter {

/**
 * HTTP Routing Filter
 *
 * This filter provides HTTP endpoint routing capabilities.
 * It does NOT do HTTP parsing - it receives already-parsed HTTP messages
 * via the MessageCallbacks interface and routes them to registered handlers.
 *
 * Architecture:
 * - Receives parsed HTTP messages via callbacks
 * - Routes requests to registered handlers based on path and method
 * - Can pass through unhandled requests for further processing
 * - Uses MCP Buffer abstraction for all data handling
 *
 * Filter chain: [Network] → [HttpCodecFilter] → [HttpRoutingFilter] → [MCP
 * Protocol]
 */
class HttpRoutingFilter : public HttpCodecFilter::MessageCallbacks {
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
   * @param next_callbacks The next layer of callbacks to forward unhandled
   * requests to
   * @param encoder HTTP encoder for sending responses
   * @param is_server True for server mode (default), false for client mode
   */
  explicit HttpRoutingFilter(HttpCodecFilter::MessageCallbacks* next_callbacks,
                             HttpCodecFilter::MessageEncoder* encoder,
                             bool is_server = true);

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

  /**
   * Set the HTTP encoder (called after HTTP codec is created)
   * @param encoder The HTTP encoder to use for responses
   */
  void setEncoder(HttpCodecFilter::MessageEncoder* encoder) {
    encoder_ = encoder;
  }

  /**
   * Set write callbacks for sending responses
   * @param callbacks The write callbacks to use
   */
  void setWriteCallbacks(network::WriteFilterCallbacks* callbacks) {
    write_callbacks_ = callbacks;
  }

  // HttpCodecFilter::MessageCallbacks interface
  void onHeaders(const std::map<std::string, std::string>& headers,
                 bool keep_alive) override;
  void onBody(const std::string& data, bool end_stream) override;
  void onMessageComplete() override;
  void onError(const std::string& error) override;

  /**
   * Send HTTP response (made public for filter chain use)
   * @param response The response to send
   */
  void sendResponse(const Response& response);

 private:
  // Process HTTP request and route to appropriate handler
  void processRequest();

  // Route key is "METHOD /path"
  std::string buildRouteKey(const std::string& method,
                            const std::string& path) const;

  // Extract method from request line or headers
  std::string extractMethod(const std::map<std::string, std::string>& headers);

  // Extract path from request line or headers
  std::string extractPath(const std::map<std::string, std::string>& headers);

  // Components
  HttpCodecFilter::MessageCallbacks*
      next_callbacks_;                        // Next layer to forward to
  HttpCodecFilter::MessageEncoder* encoder_;  // HTTP encoder for responses
  network::WriteFilterCallbacks* write_callbacks_ =
      nullptr;  // For sending responses
  bool is_server_;

  // Registered handlers
  std::map<std::string, HandlerFunc> handlers_;

  // Default handler for unmatched requests
  HandlerFunc default_handler_;

  // Following production pattern - completely stateless filter
  // No request state stored - all decisions made immediately
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

}  // namespace filter
}  // namespace mcp

#endif  // MCP_FILTER_HTTP_ROUTING_FILTER_H