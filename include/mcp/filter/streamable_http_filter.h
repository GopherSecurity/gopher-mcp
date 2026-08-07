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
#include "mcp/transport/streamable_session_manager.h"

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

    /**
     * What a response has to carry when this filter frames it itself
     * rather than letting the codec downstream do it — which origin may
     * read it, above all. Asked once per request, because the answer
     * depends on the request and a streamed one is framed long after the
     * connection has stopped remembering which request it is answering.
     */
    virtual http::ResponseWriter::HeaderList framedResponseHeaders() const = 0;

    /**
     * Told when a response on this connection starts and stops streaming,
     * so the connection can stop turning arriving bytes into requests it
     * would have no way to answer in order.
     */
    virtual http::ResponseWriter::Observer* streamObserver() = 0;

    /**
     * Whether a streamed response leaves the connection unusable, so the
     * answer says so up front rather than leaving a client to discover it.
     */
    virtual bool streamEndsConnection() const = 0;
  };

  /**
   * @param fallback Where requests this filter does not serve are sent.
   * @param exchanges The connection's registry, shared so that a
   *                  connection dying takes these exchanges with it.
   * @param mcp_path  The endpoint this filter answers for.
   * @param sessions  Where sessions are kept. Null is stateless mode: no
   *                  session is ever minted, and an inbound session id is
   *                  ignored rather than believed — a server that keeps no
   *                  sessions has no way to tell whose id it was handed.
   */
  StreamableHttpFilter(event::Dispatcher& dispatcher,
                       McpProtocolCallbacks& mcp_callbacks,
                       HttpCodecFilter::MessageCallbacks& fallback,
                       transport::ExchangeRegistry& exchanges,
                       Host& host,
                       const std::string& mcp_path,
                       transport::StreamableSessionManager* sessions = nullptr);
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

  /**
   * A streamed answer, held by whoever is still producing it.
   *
   * Holds the exchange outright rather than through the filter: the whole
   * point is that a handler may keep this after its dispatch returned and
   * after the connection it arrived on has gone.
   */
  class ResponseStreamImpl : public ResponseStream {
   public:
    ResponseStreamImpl(transport::RequestExchangePtr exchange, bool may_stream)
        : exchange_(std::move(exchange)), may_stream_(may_stream) {}

    VoidResult sendNotification(
        const jsonrpc::Notification& notification) override;
    VoidResult sendResponse(const jsonrpc::Response& response) override;
    bool alive() const override;

    /** Open the stream now, before anything else is written. */
    bool open();

    /** Notifications discarded because the client could not read them. */
    size_t droppedNotifications() const { return dropped_; }

   private:
    transport::RequestExchangePtr exchange_;
    bool may_stream_;
    size_t dropped_{0};
  };

  /** A view onto the exchange behind the message being dispatched. */
  class DispatchContext : public MessageDispatchContext {
   public:
    explicit DispatchContext(StreamableHttpFilter& parent) : parent_(parent) {}

    network::Connection* originConnection() const override;
    const std::string& transportSessionId() const override;
    VoidResult sendResponse(const jsonrpc::Response& response) override;
    ResponseStreamPtr beginResponseStream() override;

   private:
    StreamableHttpFilter& parent_;
  };

  /** Start an exchange for a request this filter owns. */
  void beginRequest(const std::map<std::string, std::string>& headers);

  /**
   * Give a client that is introducing itself a session to come back with.
   *
   * Done before the handler runs, not after, so the request being served
   * is already keyed on the session it creates — otherwise the terms
   * agreed at initialize would be recorded against an identity the client
   * never hears about and can never present again.
   */
  void mintSessionFor(const jsonrpc::Request& request);

  /**
   * Settle a session against the answer its initialize earned: keep it and
   * record what was agreed, or drop it if nothing was.
   */
  void settleMintedSession(const jsonrpc::Response& response);
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
  transport::StreamableSessionManager* sessions_;

  // Parses the one message a request body may carry. Owned here rather
  // than shared, so a message on this endpoint can never be dispatched
  // through the older filter's handlers.
  std::unique_ptr<JsonRpcProtocolFilter> jsonrpc_;

  // ── Per-request state, all reset when a request begins ──
  transport::RequestExchangePtr exchange_;
  std::string body_;
  std::string session_id_;
  // Set only for a request that created its session, which is the one
  // request whose answer decides whether that session survives.
  std::string minted_session_id_;
  Carried carried_{Carried::Nothing};
  size_t dispatched_{0};

  // The streamed answer for the request being dispatched, if it asked for
  // one. Held only for the length of the dispatch — whoever is producing
  // the answer holds its own reference and may outlive this filter.
  std::shared_ptr<ResponseStreamImpl> stream_;
};

}  // namespace filter
}  // namespace mcp

#endif  // MCP_FILTER_STREAMABLE_HTTP_FILTER_H
