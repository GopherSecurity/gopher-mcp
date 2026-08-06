/**
 * The MCP endpoint's request handling. See the header for the contract.
 */

#include "mcp/filter/streamable_http_filter.h"

#include "mcp/buffer.h"
#include "mcp/http/http_parser.h"
#include "mcp/json/json_bridge.h"
#include "mcp/json/json_serialization.h"
#include "mcp/logging/log_macros.h"
#include "mcp/mcp_connection_manager.h"

namespace mcp {
namespace filter {

namespace {

// This filter only ever exists on the serving side of a connection.
const bool kServerMode = true;

/** Header lookup that tolerates the key being absent. */
std::string headerOr(const std::map<std::string, std::string>& headers,
                     const std::string& name,
                     const std::string& fallback) {
  auto it = headers.find(name);
  return it != headers.end() ? it->second : fallback;
}

/** The request target, with any query string removed. */
std::string requestPath(const std::map<std::string, std::string>& headers) {
  // Some codecs surface the target as the HTTP/2-style pseudo-header and
  // some as "url". Accept either.
  std::string path = headerOr(headers, ":path", "");
  if (path.empty()) {
    path = headerOr(headers, "url", "/");
  }
  const size_t query = path.find('?');
  return query == std::string::npos ? path : path.substr(0, query);
}

bool mentions(const std::string& header, const std::string& media_type) {
  return header.find(media_type) != std::string::npos ||
         header.find("*/*") != std::string::npos;
}

/**
 * A JSON-RPC error with no id.
 *
 * Built as text rather than through jsonrpc::Response because that type's
 * id is a RequestId, which has no null alternative — and a body that could
 * not be parsed has no id to quote back.
 */
std::string idLessError(int code, const std::string& message) {
  json::JsonValue error = json::JsonValue::object();
  error.set("code", json::JsonValue(static_cast<int64_t>(code)));
  error.set("message", json::JsonValue(message));

  json::JsonValue body = json::JsonValue::object();
  body.set("jsonrpc", json::JsonValue("2.0"));
  body.set("id", json::JsonValue());
  body.set("error", error);
  return body.toString();
}

}  // namespace

// ===== ResponseStreamImpl =====

bool StreamableHttpFilter::ResponseStreamImpl::open() {
  if (!may_stream_ || !exchange_) {
    return false;
  }
  if (exchange_->mode() == transport::RequestExchange::Mode::Stream) {
    return true;
  }
  if (exchange_->mode() != transport::RequestExchange::Mode::Open) {
    return false;
  }

  // A streamed answer is worth keeping when its client goes away: the work
  // behind it carries on, and a client that comes back can be given what
  // it missed. A single response has nothing to come back for.
  exchange_->setRetainOnDisconnect(true);
  if (!exchange_->beginStream()) {
    return false;
  }
  exchange_->setPhase(transport::RequestExchange::Phase::RespondingSseOpen);
  return true;
}

VoidResult StreamableHttpFilter::ResponseStreamImpl::sendNotification(
    const jsonrpc::Notification& notification) {
  if (!exchange_) {
    Error err;
    err.code = jsonrpc::INTERNAL_ERROR;
    err.message = "notification dropped: this request has no answer open";
    return makeVoidError(err);
  }

  if (!may_stream_) {
    // The client asked for one JSON object and progress is not it. Not an
    // error: a handler that only reports progress is still answerable, and
    // failing it here would refuse a request that can be served.
    ++dropped_;
    GOPHER_LOG_DEBUG(
        "progress dropped: the client cannot read a streamed response ({})",
        notification.method);
    return makeVoidSuccess();
  }

  if (!open()) {
    Error err;
    err.code = jsonrpc::INTERNAL_ERROR;
    err.message = "notification dropped: the answer is already committed";
    return makeVoidError(err);
  }

  return exchange_->writeEvent("message",
                               json::to_json(notification).toString())
             ? makeVoidSuccess()
             : makeVoidError(
                   Error(jsonrpc::INTERNAL_ERROR, "notification not written"));
}

VoidResult StreamableHttpFilter::ResponseStreamImpl::sendResponse(
    const jsonrpc::Response& response) {
  if (!exchange_) {
    Error err;
    err.code = jsonrpc::INTERNAL_ERROR;
    err.message = "response dropped: this request has no answer open";
    return makeVoidError(err);
  }

  // Only a stream that actually opened ends as one. A handler that asked
  // for a stream and then said nothing until its answer gets the plain
  // response it would have got anyway.
  if (exchange_->mode() != transport::RequestExchange::Mode::Stream) {
    return exchange_->respondJson(response);
  }

  exchange_->setPhase(transport::RequestExchange::Phase::RespondingSseDraining);
  if (!exchange_->writeEvent("message", json::to_json(response).toString())) {
    Error err;
    err.code = jsonrpc::INTERNAL_ERROR;
    err.message = "response not written";
    return makeVoidError(err);
  }

  // The response is the last thing on the stream. Closing here is what
  // frees the connection for the next request.
  exchange_->setPhase(transport::RequestExchange::Phase::RespondingSseClosed);
  exchange_->complete();
  return makeVoidSuccess();
}

bool StreamableHttpFilter::ResponseStreamImpl::alive() const {
  return exchange_ && !exchange_->detached() &&
         exchange_->mode() != transport::RequestExchange::Mode::Complete;
}

// ===== DispatchContext =====

network::Connection* StreamableHttpFilter::DispatchContext::originConnection()
    const {
  return parent_.host_.connection();
}

const std::string& StreamableHttpFilter::DispatchContext::transportSessionId()
    const {
  return parent_.session_id_;
}

VoidResult StreamableHttpFilter::DispatchContext::sendResponse(
    const jsonrpc::Response& response) {
  if (!parent_.exchange_) {
    Error err;
    err.code = jsonrpc::INTERNAL_ERROR;
    err.message = "response dropped: this message had no request behind it";
    return makeVoidError(err);
  }
  // The exchange knows what it has already committed to, so it is what
  // refuses a second answer rather than writing two onto one request.
  return parent_.exchange_->respondJson(response);
}

ResponseStreamPtr StreamableHttpFilter::DispatchContext::beginResponseStream() {
  if (!parent_.exchange_) {
    return nullptr;
  }
  if (!parent_.stream_) {
    parent_.stream_.reset(new ResponseStreamImpl(
        parent_.exchange_, parent_.exchange_->clientContext().accepts_sse));
  }
  return parent_.stream_;
}

// ===== StreamableHttpFilter =====

StreamableHttpFilter::StreamableHttpFilter(
    event::Dispatcher& dispatcher,
    McpProtocolCallbacks& mcp_callbacks,
    HttpCodecFilter::MessageCallbacks& fallback,
    transport::ExchangeRegistry& exchanges,
    Host& host,
    const std::string& mcp_path)
    : dispatcher_(dispatcher),
      mcp_callbacks_(mcp_callbacks),
      fallback_(fallback),
      exchanges_(exchanges),
      host_(host),
      mcp_path_(mcp_path),
      jsonrpc_(new JsonRpcProtocolFilter(*this, dispatcher, kServerMode)) {}

StreamableHttpFilter::~StreamableHttpFilter() = default;

void StreamableHttpFilter::onHeaders(
    const std::map<std::string, std::string>& headers, bool keep_alive) {
  // Always forwarded, whoever ends up serving the request: the filter
  // behind this one keeps per-connection bookkeeping that every request on
  // the connection contributes to, and skipping it for some requests would
  // leave that state describing only half the traffic.
  fallback_.onHeaders(headers, keep_alive);

  // A request left over from an earlier message would otherwise collect
  // this one's body.
  abandonRequest();

  const std::string method = headerOr(headers, ":method", "GET");
  if (method != "POST" || requestPath(headers) != mcp_path_) {
    return;
  }

  beginRequest(headers);
}

void StreamableHttpFilter::beginRequest(
    const std::map<std::string, std::string>& headers) {
  exchange_ = transport::RequestExchange::create(dispatcher_, host_.makeSink(),
                                                 nullopt);

  // Captured now rather than read when the answer is written: by then this
  // connection may be serving a different request, or none.
  exchange_->setResponseOptions(host_.requestIsHttp11(),
                                !host_.streamEndsConnection());
  exchange_->setStreamObserver(host_.streamObserver());

  auto& client = exchange_->clientContext();
  auto accept = headers.find("accept");
  if (accept != headers.end() && !accept->second.empty()) {
    client.accepts_json = mentions(accept->second, "application/json");
    client.accepts_sse = mentions(accept->second, "text/event-stream");
    if (!client.accepts_json) {
      // Answered with JSON regardless — there is nothing else to answer a
      // unary request with yet — but worth knowing when a peer complains.
      GOPHER_LOG_DEBUG(
          "MCP endpoint request does not accept application/json: {}",
          accept->second);
    }
  }

  client.protocol_version =
      headerOr(headers, "mcp-protocol-version", client.protocol_version);
  client.principal = host_.principal();
  session_id_ = headerOr(headers, "mcp-session-id", "");

  exchange_->setPhase(transport::RequestExchange::Phase::ReceivingBody);
  exchanges_.add(exchange_);
}

void StreamableHttpFilter::onBody(const std::string& data, bool end_stream) {
  if (!exchange_) {
    fallback_.onBody(data, end_stream);
    return;
  }
  body_.append(data);
}

void StreamableHttpFilter::onMessageComplete() {
  if (!exchange_) {
    fallback_.onMessageComplete();
    return;
  }
  finishRequest();
}

void StreamableHttpFilter::onError(const std::string& error) {
  abandonRequest();
  fallback_.onError(error);
}

void StreamableHttpFilter::finishRequest() {
  // Everything below runs once per HTTP request, which is the whole point:
  // one request gets one answer, whatever its body turned out to contain.
  auto exchange = exchange_;

  json::JsonValue message;
  try {
    message = json::JsonValue::parse(body_);
  } catch (const json::JsonException& e) {
    // Also where a body carrying two messages lands: a JSON document that
    // is followed by anything other than whitespace is not a document.
    GOPHER_LOG_DEBUG("MCP endpoint request body could not be parsed: {}",
                     e.what());
    respondWithError(static_cast<int>(http::HttpStatusCode::BadRequest),
                     jsonrpc::PARSE_ERROR, "Parse error");
    abandonRequest();
    return;
  }

  if (message.isArray()) {
    // The endpoint carries a single JSON-RPC message per request, and one
    // HTTP response cannot answer several of them.
    respondWithError(static_cast<int>(http::HttpStatusCode::BadRequest),
                     jsonrpc::INVALID_REQUEST,
                     "Invalid Request: one message per request");
    abandonRequest();
    return;
  }
  if (!message.isObject()) {
    respondWithError(static_cast<int>(http::HttpStatusCode::BadRequest),
                     jsonrpc::INVALID_REQUEST,
                     "Invalid Request: expected a JSON-RPC message");
    abandonRequest();
    return;
  }

  carried_ = Carried::Nothing;
  dispatched_ = 0;

  exchange->setPhase(transport::RequestExchange::Phase::Dispatching);

  // Re-serialized rather than passed through: what reaches the parser is
  // then exactly one document, whatever whitespace or line breaks the peer
  // wrapped it in.
  OwnedBuffer parsed;
  parsed.add(message.toString());
  jsonrpc_->onData(parsed, /*end_stream=*/true);

  if (exchange->mode() != transport::RequestExchange::Mode::Open) {
    // Already answered from inside the dispatch: either a handler replied,
    // or the parser rejected the message and the error went out.
    abandonRequest();
    return;
  }

  switch (carried_) {
    case Carried::Request:
      // The handler has not answered yet and is entitled not to. Nothing
      // goes on the wire until it does.
      break;
    case Carried::Notification:
    case Carried::Response:
      // Nothing to answer with, but HTTP still needs an answer.
      exchange->setPhase(transport::RequestExchange::Phase::Responding202);
      exchange->setStatus(static_cast<int>(http::HttpStatusCode::Accepted));
      exchange->respondUnary("", "");
      break;
    case Carried::Nothing:
      respondWithError(static_cast<int>(http::HttpStatusCode::BadRequest),
                       jsonrpc::INVALID_REQUEST,
                       "Invalid Request: not a JSON-RPC message");
      break;
  }

  abandonRequest();
}

void StreamableHttpFilter::respondWithError(int status_code,
                                            int code,
                                            const std::string& message) {
  if (!exchange_) {
    return;
  }
  exchange_->setPhase(transport::RequestExchange::Phase::RespondingError);
  exchange_->setStatus(status_code);
  exchange_->respondUnary("application/json", idLessError(code, message));
}

void StreamableHttpFilter::abandonRequest() {
  body_.clear();
  session_id_.clear();
  carried_ = Carried::Nothing;
  dispatched_ = 0;
  exchange_.reset();
  stream_.reset();
  // Whatever finished during this request is no longer the connection's
  // concern; a handler answering later holds its own reference.
  exchanges_.reapCompleted();
}

// ===== JsonRpcProtocolFilter::MessageHandler =====

void StreamableHttpFilter::onRequest(const jsonrpc::Request& request) {
  ++dispatched_;
  if (dispatched_ > 1 || !exchange_) {
    GOPHER_LOG_WARN("MCP endpoint request carried more than one message");
    return;
  }
  carried_ = Carried::Request;

  exchange_->setRequestId(request.id);

  // params._meta arrives already serialized, because nested JSON is
  // stringified on the way in. Carry it as it came; whoever needs a field
  // out of it can parse it.
  if (request.params.has_value()) {
    const auto& params = request.params.value();
    auto meta = params.find("_meta");
    if (meta != params.end() && holds_alternative<std::string>(meta->second)) {
      exchange_->clientContext().raw_meta =
          mcp::make_optional(get<std::string>(meta->second));
    }
  }

  // How the answer will be framed is settled by its first byte, so it has
  // to be decided before anything runs — never by looking at what a
  // handler turned out to produce.
  const StreamingMode streaming = mcp_callbacks_.streamingFor(request);
  const bool accepts_sse = exchange_->clientContext().accepts_sse;

  if (streaming == StreamingMode::Required && !accepts_sse) {
    // This handler will ask the client something and wait for the answer.
    // Serving it anyway would leave it waiting on a question the client
    // can never be shown, so the request is refused before it starts.
    GOPHER_LOG_DEBUG(
        "MCP endpoint request needs a streamed response the client will not "
        "accept: {}",
        request.method);
    respondWithError(static_cast<int>(http::HttpStatusCode::NotAcceptable),
                     jsonrpc::INVALID_REQUEST,
                     "Not Acceptable: this method answers with "
                     "text/event-stream, which this request does not accept");
    return;
  }

  DispatchContext context(*this);

  if (streaming == StreamingMode::Required) {
    // Opened before the handler runs, so the response headers are on the
    // wire before anything it emits.
    auto stream = context.beginResponseStream();
    if (stream && !stream_->open()) {
      GOPHER_LOG_ERROR("MCP endpoint response stream failed to open");
      return;
    }
  }

  mcp_callbacks_.onRequestWithContext(request, context);
}

void StreamableHttpFilter::onNotification(
    const jsonrpc::Notification& notification) {
  ++dispatched_;
  if (dispatched_ > 1) {
    GOPHER_LOG_WARN("MCP endpoint request carried more than one message");
    return;
  }
  carried_ = Carried::Notification;

  DispatchContext context(*this);
  mcp_callbacks_.onNotificationWithContext(notification, context);
}

void StreamableHttpFilter::onResponse(const jsonrpc::Response& response) {
  ++dispatched_;
  if (dispatched_ > 1) {
    GOPHER_LOG_WARN("MCP endpoint request carried more than one message");
    return;
  }
  carried_ = Carried::Response;
  mcp_callbacks_.onResponse(response);
}

void StreamableHttpFilter::onProtocolError(const Error& error) {
  GOPHER_LOG_DEBUG("MCP endpoint request rejected: {}", error.message);
  respondWithError(static_cast<int>(http::HttpStatusCode::BadRequest),
                   error.code, error.message);
}

void StreamableHttpFilter::onRequestWithContext(const jsonrpc::Request& request,
                                                MessageDispatchContext&) {
  onRequest(request);
}

void StreamableHttpFilter::onNotificationWithContext(
    const jsonrpc::Notification& notification, MessageDispatchContext&) {
  onNotification(notification);
}

}  // namespace filter
}  // namespace mcp
