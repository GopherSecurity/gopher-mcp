#ifndef MCP_FILTER_STREAMABLE_HTTP_FILTER_H
#define MCP_FILTER_STREAMABLE_HTTP_FILTER_H

#include <chrono>
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
 * What the MCP endpoint serves, beyond the requests themselves.
 *
 * Gathered into one value rather than five more constructor arguments,
 * and because they arrive together: every one of them comes from the same
 * block of server configuration.
 */
struct StreamableHttpOptions {
  /** Where sessions are kept. Null is stateless: none minted, none read. */
  transport::StreamableSessionManager* sessions{nullptr};

  /**
   * Protocol revisions this endpoint can serve, newest first. Empty means
   * no opinion, and refuses nothing.
   */
  std::vector<std::string> protocol_versions;

  /**
   * Whether requests declaring the handshakeless revision are served by
   * its rules rather than refused.
   *
   * Separate from the list above because the two answer different
   * questions: the list says which revisions this endpoint will serve at
   * all, and this says whether the pipeline that serves the newest one
   * exists. With it off, a request declaring that revision is refused the
   * way its own era expects rather than being served by rules it does not
   * follow.
   */
  bool enable_modern_era{false};

  /**
   * Whether a request must come from the caller its session was minted
   * for. Off means a session id alone is enough to be served as whoever
   * created it, which is only defensible where nothing distinguishes
   * callers in the first place.
   */
  bool require_principal_match{true};

  /** Whether a client may end its own session with DELETE. */
  bool allow_client_termination{true};

  /**
   * Whether a client may open a standalone event stream on the endpoint,
   * which is where everything the server says on its own initiative goes.
   */
  bool enable_get_stream{true};

  /**
   * How many such streams one session may hold at once. The protocol
   * allows several, so a second is accepted rather than refused; this is
   * only what stops one client from holding an unbounded number.
   */
  size_t max_get_streams_per_session{4};

  /**
   * How often an idle event stream says something meaningless, so that
   * anything between the two ends can tell it apart from a dead one.
   * Zero switches it off.
   */
  std::chrono::milliseconds keepalive_interval{30000};

  /**
   * Whether a client may come back to a stream it lost and be given what
   * it missed. Off means events carry no id and a client that offers one
   * is simply given a fresh stream — which is what an id it could not
   * have been issued deserves.
   */
  bool enable_resumability{true};

  /** How many events one stream keeps for a client that may return. */
  size_t replay_buffer_events{256};
};

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

    /**
     * Stop turning arriving bytes into requests, and start again.
     *
     * Used while a request waits on an answer from another thread. HTTP/1.1
     * delivers responses in request order, so a request behind one that is
     * still being judged cannot be answered first — and letting it be
     * parsed meanwhile is how it would be.
     */
    virtual void holdInput(bool hold) = 0;
  };

  /**
   * @param fallback Where requests this filter does not serve are sent.
   * @param exchanges The connection's registry, shared so that a
   *                  connection dying takes these exchanges with it.
   * @param mcp_path  The endpoint this filter answers for.
   * @param options   What this endpoint serves besides the requests. Its
   *                  defaults are stateless — no session is ever minted,
   *                  and an inbound session id is ignored rather than
   *                  believed, since a server that keeps no sessions has no
   *                  way to tell whose id it was handed.
   */
  StreamableHttpFilter(
      event::Dispatcher& dispatcher,
      McpProtocolCallbacks& mcp_callbacks,
      HttpCodecFilter::MessageCallbacks& fallback,
      transport::ExchangeRegistry& exchanges,
      Host& host,
      const std::string& mcp_path,
      const StreamableHttpOptions& options = StreamableHttpOptions());
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

  /**
   * What a request's session id turned out to be worth.
   *
   * The rules belong to the session rather than to any one method, so this
   * is deliberately method-agnostic and shared: POST and DELETE both go
   * through it, and the standalone event stream will too.
   */
  enum class SessionVerdict {
    Serve,           // known, and the caller is entitled to it
    Missing,         // required and not presented
    Unknown,         // never issued, already ended, or expired
    WrongPrincipal,  // real, but not this caller's to use
  };

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
    /**
     * @param on_open Told the moment this becomes a stream, which is when
     *                it becomes something a client could be given a name
     *                for and could later come back to.
     */
    ResponseStreamImpl(transport::RequestExchangePtr exchange,
                       bool may_stream,
                       std::function<void()> on_open)
        : exchange_(std::move(exchange)),
          may_stream_(may_stream),
          on_open_(std::move(on_open)) {}

    VoidResult sendNotification(
        const jsonrpc::Notification& notification) override;
    VoidResult sendRequest(const jsonrpc::Request& request) override;
    VoidResult sendResponse(const jsonrpc::Response& response) override;
    bool alive() const override;

    /** Open the stream now, before anything else is written. */
    bool open();

    /** Notifications discarded because the client could not read them. */
    size_t droppedNotifications() const { return dropped_; }

   private:
    transport::RequestExchangePtr exchange_;
    bool may_stream_;
    std::function<void()> on_open_;
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

  /**
   * Settle which protocol revision this request is speaking, and refuse it
   * if that is one this server cannot serve.
   *
   * @return False when the request has been answered and is over.
   */
  bool settleProtocolVersion(const std::string& method_name);

  /**
   * Judge the session this request presented. Runs on the thread that owns
   * the session, which is not always this one.
   *
   * @param exempt True for a request entitled to arrive without a session —
   *               an initialize, which is how one is obtained.
   */
  SessionVerdict judgeSession(const std::string& id, bool exempt) const;

  /**
   * What the thread owning a session concluded about a request presenting
   * it, gathered in one visit because that is the only thread entitled to
   * look at any of it.
   */
  struct Judgement {
    SessionVerdict verdict{SessionVerdict::Unknown};
    size_t live_get_streams{0};
    /** Where the client says it got to, placed against what is still held. */
    transport::StreamableSessionManager::ResumePoint resume;
  };

  /** Answer a request whose session did not entitle it to be served. */
  void refuseSession(SessionVerdict verdict);

  /**
   * Run the judgement and carry on, taking a thread hop only if the session
   * belongs to another dispatcher.
   */
  void validateThenDispatch(const std::string& method_name);

  /** Continue a request whose session has now been judged. */
  void resumeAfterValidation(const Judgement& judged);

  /** Hand the buffered body to the parser, which dispatches it. */
  void dispatchBody();

  /** Answer a DELETE, which ends the session it names. */
  void terminateSession();

  /**
   * Answer a GET by opening the session's standalone event stream — the
   * one a client leaves open for everything the server says on its own
   * initiative.
   */
  void openEventStream(const Judgement& judged);

  /**
   * Give this request's answer a name a client could come back to, and a
   * buffer behind it. Empty when this server keeps no sessions, and so
   * has nowhere to look a returning client up.
   */
  std::string nameThisStream(const transport::RequestExchangePtr& exchange);

  /**
   * Hand a resuming client what it missed, then put the stream on its
   * session — in that order, since what the session hands over on
   * registration is what came after.
   */
  void replayThenRegister(
      const transport::StreamableSessionManager::ResumePoint& resume,
      const std::string& stream_id);

  /** Register the stream just opened against its session. */
  void registerEventStream(const transport::RequestExchangePtr& exchange,
                           const std::string& stream_id);

  /** The same, for the stream a request is being answered on. */
  void registerResponseStream(const transport::RequestExchangePtr& exchange,
                              const std::string& session_id,
                              const std::string& stream_id);

  /** Start, and keep restarting, this connection's stream keep-alive. */
  void armKeepalive();
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
  StreamableHttpOptions options_;
  transport::StreamableSessionManager* sessions_;

  // Handed to anything posted to another thread, so a continuation that
  // comes back after the connection died can tell that it has.
  std::shared_ptr<int> alive_;

  // Parses the one message a request body may carry. Owned here rather
  // than shared, so a message on this endpoint can never be dispatched
  // through the older filter's handlers.
  std::unique_ptr<JsonRpcProtocolFilter> jsonrpc_;

  // ── Per-request state, all reset when a request begins ──
  transport::RequestExchangePtr exchange_;
  std::string body_;
  std::string session_id_;
  // Where the client says it last got to, if it said. Read from the
  // request rather than remembered per connection: the whole point of it
  // is that this is not the connection the events were sent on.
  std::string last_event_id_;
  // Set only for a request that created its session, which is the one
  // request whose answer decides whether that session survives.
  std::string minted_session_id_;
  Carried carried_{Carried::Nothing};
  size_t dispatched_{0};
  // What the request asked for, which decides how it is answered before
  // anything about its body is known.
  std::string method_;
  // True while a request is waiting on a judgement from another thread.
  // Nothing else on this connection is parsed meanwhile, so the request
  // members below are still this request's when the answer comes back.
  bool parked_{false};

  // The session whose standalone event stream this connection is carrying,
  // and the connection itself. Kept past the request that opened it, so
  // the stream can be detached from its session when the client goes: the
  // pointer is only ever compared, never followed.
  std::string get_stream_session_id_;
  network::Connection* get_stream_conn_{nullptr};

  // The stream itself, and what keeps it looking alive. Both belong to the
  // connection rather than to any one request: the timer stops when this
  // filter goes, which is exactly when there is no longer a connection
  // worth holding open.
  transport::RequestExchangePtr get_stream_exchange_;
  event::TimerPtr keepalive_timer_;

  // The streamed answer for the request being dispatched, if it asked for
  // one. Held only for the length of the dispatch — whoever is producing
  // the answer holds its own reference and may outlive this filter.
  std::shared_ptr<ResponseStreamImpl> stream_;
};

}  // namespace filter
}  // namespace mcp

#endif  // MCP_FILTER_STREAMABLE_HTTP_FILTER_H
