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
   * What the route table does with a matched (method, path).
   *
   * Handler     - run the registered callback.
   * PassThrough - hand the request to the next protocol layer untouched.
   *               The table is authoritative for such a route, so the
   *               default handler is not consulted for it.
   * Reject      - answer immediately from the request headers. Any body
   *               is drained and discarded, so a rejecting route can
   *               never inspect a payload. A 405 always carries an Allow
   *               header: the explicit allow_header when set, otherwise
   *               the value rendered from this table for the path.
   */
  struct RouteTarget {
    enum class Kind { Handler, PassThrough, Reject };

    Kind kind = Kind::Handler;
    HandlerFunc handler;       // Kind::Handler only
    int status_code = 0;       // Kind::Reject only
    std::string allow_header;  // Kind::Reject only; empty means derive

    static RouteTarget handlerRoute(HandlerFunc handler);
    static RouteTarget passThrough();
    static RouteTarget reject(int status_code,
                              const std::string& allow_header = "");

    // A method may only be advertised in Allow when its route would
    // really serve the request. Rejections must never be advertised.
    bool servesRequests() const {
      return kind == Kind::Handler || kind == Kind::PassThrough;
    }
  };

  /**
   * Add a route to the table, replacing any route with the same method
   * and path.
   */
  void addRoute(const std::string& method,
                const std::string& path,
                RouteTarget target);

  /**
   * Render the Allow header value for a path from the table itself:
   * every method whose route would serve a request, comma separated and
   * in a stable order. Empty when nothing serves the path.
   *
   * The path must already have its query string stripped.
   */
  std::string allowedMethodsFor(const std::string& path) const;

  /**
   * Read-only view of the route table, keyed by "METHOD /path".
   */
  const std::map<std::string, RouteTarget>& routes() const { return routes_; }

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

  /**
   * Extra headers to put on every response this filter sends.
   *
   * Asked per response, because what belongs on an answer depends on the
   * request it answers, and a handler has no business deciding who may
   * read what it returned. A handler's own header of the same name wins,
   * so a route that needs something different can still say so.
   */
  void setResponseHeaderProvider(
      std::function<std::map<std::string, std::string>()> provider) {
    response_headers_ = std::move(provider);
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
  // Route key is "METHOD /path"
  std::string buildRouteKey(const std::string& method,
                            const std::string& path) const;

  // Build the immediate response for a Reject route.
  Response buildRejectResponse(const RouteTarget& target,
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

  // Route table, keyed by "METHOD /path"
  std::map<std::string, RouteTarget> routes_;

  // Default handler for unmatched requests
  HandlerFunc default_handler_;

  // Extra headers for every response this filter sends.
  std::function<std::map<std::string, std::string>()> response_headers_;

  // State for POST requests that need body
  bool pending_post_request_ = false;
  bool suppress_current_request_ = false;
  RequestContext pending_context_;
  HandlerFunc pending_handler_;
  std::string accumulated_body_;
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
