#ifndef MCP_FILTER_STREAMABLE_HTTP_FILTER_H
#define MCP_FILTER_STREAMABLE_HTTP_FILTER_H

#include <map>
#include <memory>
#include <string>

#include "mcp/core/compat.h"
#include "mcp/event/event_loop.h"
#include "mcp/filter/http_codec_filter.h"
#include "mcp/filter/json_rpc_protocol_filter.h"
#include "mcp/message_dispatch_context.h"
#include "mcp/transport/exchange_registry.h"
#include "mcp/transport/request_exchange.h"

namespace mcp {

class McpProtocolCallbacks;

namespace network {
class Connection;
}

namespace filter {

/**
 * Serves the MCP endpoint: one HTTP request in, one answer out.
 *
 * It sits between HTTP routing and the JSON-RPC parser and owns everything
 * that is decided per request rather than per connection — what the peer
 * will accept, what the body turned out to be, and which of the three
 * possible answers the request gets. Requests for anything else are handed
 * straight to the filter behind it, which still serves the older HTTP+SSE
 * transport untouched.
 *
 * The reason this is a separate filter rather than another branch in that
 * one: the answer to a POST is a property of the HTTP request, and the
 * older filter decides everything at JSON-RPC message scope. A body
 * carrying two notifications would write two responses to one request from
 * there; from here it cannot, because there is one decision point per
 * request by construction.
 *
 * Dispatcher-thread confined, like every filter.
 */
class StreamableHttpFilter : public HttpCodecFilter::MessageCallbacks,
                             public JsonRpcProtocolFilter::MessageHandler {
 public:
  /**
   * What the filter needs from the connection it is serving.
   *
   * A seam rather than a connection pointer, because the interesting
   * behaviour here is what gets written for a given request, and a test
   * that has to stand up a socket to see that tests the socket too.
   */
  class Host {
   public:
    virtual ~Host() = default;

    /** Where the bytes of one exchange go. */
    virtual transport::ExchangeSinkPtr makeSink() = 0;

    /** The connection a request arrived on; null when there is none. */
    virtual network::Connection* connection() = 0;

    /**
     * Whether the request being answered came in on HTTP/1.1. Asked once
     * per request and remembered, because by the time the answer is
     * written the connection may have moved on to another request.
     */
    virtual bool requestIsHttp11() const = 0;

    /**
     * Who the request being served is from, as already resolved by
     * whoever judged it. Empty when nothing did.
     */
    virtual const std::string& principal() const = 0;
  };

  /**
   * @param fallback Where requests this filter does not serve are sent.
   * @param exchanges The connection's registry, shared so that a
   *                  connection dying takes these exchanges with it.
   * @param mcp_path  The endpoint this filter answers for.
   */
  StreamableHttpFilter(event::Dispatcher& dispatcher,
                       McpProtocolCallbacks& mcp_callbacks,
                       HttpCodecFilter::MessageCallbacks& fallback,
                       transport::ExchangeRegistry& exchanges,
                       Host& host,
                       const std::string& mcp_path);
  ~StreamableHttpFilter() override;

  // ===== HttpCodecFilter::MessageCallbacks =====

  void onHeaders(const std::map<std::string, std::string>& headers,
                 bool keep_alive) override;
  void onBody(const std::string& data, bool end_stream) override;
  void onMessageComplete() override;
  void onError(const std::string& error) override;

  // ===== JsonRpcProtocolFilter::MessageHandler =====

  void onRequest(const jsonrpc::Request& request) override;
  void onNotification(const jsonrpc::Notification& notification) override;
  void onResponse(const jsonrpc::Response& response) override;
  void onProtocolError(const Error& error) override;

  // The sub-filter builds its own context, which knows neither this
  // connection's session id nor how this filter answers. Replace it.
  void onRequestWithContext(const jsonrpc::Request& request,
                            MessageDispatchContext& context) override;
  void onNotificationWithContext(const jsonrpc::Notification& notification,
                                 MessageDispatchContext& context) override;

  /** The exchange for the request being handled, if this filter owns it. */
  const transport::RequestExchangePtr& currentExchange() const {
    return exchange_;
  }

 private:
  /** What the body turned out to carry. */
  enum class Carried { Nothing, Request, Notification, Response };

  /** A view onto the exchange behind the message being dispatched. */
  class DispatchContext : public MessageDispatchContext {
   public:
    explicit DispatchContext(StreamableHttpFilter& parent) : parent_(parent) {}

    network::Connection* originConnection() const override;
    const std::string& transportSessionId() const override;
    VoidResult sendResponse(const jsonrpc::Response& response) override;

   private:
    StreamableHttpFilter& parent_;
  };

  /** Start an exchange for a request this filter owns. */
  void beginRequest(const std::map<std::string, std::string>& headers);
  /** Classify the buffered body and answer, exactly once. */
  void finishRequest();
  /** Give up on the current request without answering it. */
  void abandonRequest();

  /**
   * Answer with a JSON-RPC error carrying no id, which is the only thing
   * that can be said about a body that could not be understood well enough
   * to know whose request it was.
   */
  void respondWithError(int status_code, int code, const std::string& message);

  event::Dispatcher& dispatcher_;
  McpProtocolCallbacks& mcp_callbacks_;
  HttpCodecFilter::MessageCallbacks& fallback_;
  transport::ExchangeRegistry& exchanges_;
  Host& host_;
  std::string mcp_path_;

  // Parses the one message a request body may carry. Owned here rather
  // than shared, so a message on this endpoint can never be dispatched
  // through the older filter's handlers.
  std::unique_ptr<JsonRpcProtocolFilter> jsonrpc_;

  // ── Per-request state, all reset when a request begins ──
  transport::RequestExchangePtr exchange_;
  std::string body_;
  std::string session_id_;
  Carried carried_{Carried::Nothing};
  size_t dispatched_{0};
};

}  // namespace filter
}  // namespace mcp

#endif  // MCP_FILTER_STREAMABLE_HTTP_FILTER_H
