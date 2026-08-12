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
#include "mcp/protocol/header_sentinel.h"
#include "mcp/protocol/modern_era.h"
#include "mcp/protocol/protocol_versions.h"

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

// The one method a client may send before it has a session, and therefore
// the only one that can create it.
const char kInitializeMethod[] = "initialize";

// What a session id is called on the wire, both ways.
const char kSessionHeader[] = "Mcp-Session-Id";

// The method by which a client ends the session it was given.
const char kDeleteMethod[] = "DELETE";

// The method by which a client opens the standalone event stream.
const char kGetMethod[] = "GET";

/**
 * The protocol revision an initialize response settled on.
 *
 * Read back off the answer rather than negotiated again here: the layer
 * that answered is the one that knows which revisions it can actually
 * serve, and re-deriving it would be a second opinion that could differ
 * from what the client was told. Empty when the answer says nothing.
 */
std::string negotiatedVersion(const jsonrpc::Response& response) {
  if (!response.result.has_value()) {
    return std::string();
  }
  const auto& result = response.result.value();
  if (holds_alternative<json::JsonValue>(result)) {
    const auto& value = get<json::JsonValue>(result);
    if (value.isObject() && value.contains("protocolVersion") &&
        value["protocolVersion"].isString()) {
      return value["protocolVersion"].getString();
    }
    return std::string();
  }
  if (holds_alternative<Metadata>(result)) {
    const auto& metadata = get<Metadata>(result);
    auto it = metadata.find("protocolVersion");
    if (it != metadata.end() && holds_alternative<std::string>(it->second)) {
      return get<std::string>(it->second);
    }
  }
  return std::string();
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

/**
 * The same, carrying what the caller needs in order to do better.
 *
 * A refusal that only says no leaves a peer to retry the same request. A
 * version complaint that names what this server does serve lets it pick
 * something, which is the difference between a dead end and a
 * negotiation.
 */
std::string idLessError(int code,
                        const std::string& message,
                        const json::JsonValue& data) {
  json::JsonValue error = json::JsonValue::object();
  error.set("code", json::JsonValue(static_cast<int64_t>(code)));
  error.set("message", json::JsonValue(message));
  error.set("data", data);

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
  // Only now is there a stream to name. Told before anything is written
  // to it, since the name is what every event on it is numbered under.
  if (on_open_) {
    auto announce = std::move(on_open_);
    on_open_ = nullptr;
    announce();
  }
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

VoidResult StreamableHttpFilter::ResponseStreamImpl::sendRequest(
    const jsonrpc::Request& request) {
  if (!exchange_) {
    Error err;
    err.code = jsonrpc::INTERNAL_ERROR;
    err.message = "question dropped: this request has no answer open";
    return makeVoidError(err);
  }

  if (!may_stream_) {
    // Unlike progress, this cannot be dropped and carried on from: the
    // handler is waiting for an answer that would now never arrive, so
    // the caller has to hear that it will not.
    Error err;
    err.code = jsonrpc::INTERNAL_ERROR;
    err.message =
        "question dropped: this client cannot read a streamed response, so "
        "there is nowhere to ask it";
    return makeVoidError(err);
  }

  if (!open()) {
    Error err;
    err.code = jsonrpc::INTERNAL_ERROR;
    err.message = "question dropped: the answer is already committed";
    return makeVoidError(err);
  }

  return exchange_->writeEvent("message", json::to_json(request).toString())
             ? makeVoidSuccess()
             : makeVoidError(
                   Error(jsonrpc::INTERNAL_ERROR, "question not written"));
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
  if (!exchange_->writeEvent("message",
                             exchange_->serializeResponse(response))) {
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
  // Decided before the first byte, which is the last moment a session can
  // still be withdrawn from an answer that did not earn one.
  parent_.settleMintedSession(response);

  // The exchange knows what it has already committed to, so it is what
  // refuses a second answer rather than writing two onto one request.
  return parent_.exchange_->respondJson(response);
}

ResponseStreamPtr StreamableHttpFilter::DispatchContext::beginResponseStream() {
  if (!parent_.exchange_) {
    return nullptr;
  }
  if (!parent_.stream_) {
    // Both captured now rather than read when the stream opens: a handler
    // may keep this handle past its dispatch, and by then the filter is
    // answering some other request whose exchange and session are not
    // the ones this answer belongs to.
    StreamableHttpFilter* filter = &parent_;
    transport::RequestExchangePtr exchange = parent_.exchange_;
    const std::string session_id = parent_.session_id_;
    std::weak_ptr<int> alive = parent_.alive_;

    parent_.stream_.reset(new ResponseStreamImpl(
        parent_.exchange_, parent_.exchange_->clientContext().accepts_sse,
        [filter, exchange, session_id, alive]() {
          if (alive.expired()) {
            // The connection is gone. Nothing could reach this stream to
            // be told about it, and nothing could come back to it.
            return;
          }
          filter->registerResponseStream(exchange, session_id,
                                         filter->nameThisStream(exchange));
        }));
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
    const std::string& mcp_path,
    const StreamableHttpOptions& options)
    : dispatcher_(dispatcher),
      mcp_callbacks_(mcp_callbacks),
      fallback_(fallback),
      exchanges_(exchanges),
      host_(host),
      mcp_path_(mcp_path),
      options_(options),
      sessions_(options.sessions),
      alive_(new int(0)),
      jsonrpc_(new JsonRpcProtocolFilter(*this, dispatcher, kServerMode)) {}

StreamableHttpFilter::~StreamableHttpFilter() {
  if (sessions_ == nullptr || get_stream_session_id_.empty()) {
    return;
  }

  // This connection is going. Its stream is not: the session owns it, and
  // what is written there while nobody is connected is kept for a client
  // that comes back. Only the connection is forgotten — the pointer below
  // is compared and never followed, which is what makes it safe to use
  // one that may already be dangling.
  transport::StreamableSessionManager* sessions = sessions_;
  network::Connection* conn = get_stream_conn_;
  const std::string id = get_stream_session_id_;

  if (sessions->ownedBy(id, dispatcher_)) {
    if (auto* session = sessions->find(id)) {
      transport::StreamableSessionManager::detachConnection(*session, conn);
    }
    return;
  }
  sessions->withSession(
      dispatcher_, id,
      [conn](transport::SessionCtx& session) {
        transport::StreamableSessionManager::detachConnection(session, conn);
      },
      nullptr);
}

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
  if (requestPath(headers) != mcp_path_) {
    return;
  }
  // DELETE and GET only reach here when the route table admits them, and
  // the conditions are restated rather than assumed: a stream belongs to a
  // session, so with no sessions there is nothing to open one against, and
  // answering anyway would leave a client holding a stream nothing can
  // ever route a message to.
  const bool serves_streams =
      options_.enable_get_stream && sessions_ != nullptr;
  const bool served = method == "POST" || method == kDeleteMethod ||
                      (method == kGetMethod && serves_streams);
  if (!served) {
    return;
  }

  method_ = method;
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
  exchange_->setFramedHeaders(host_.framedResponseHeaders());

  auto& client = exchange_->clientContext();
  auto accept = headers.find("accept");
  if (accept != headers.end() && !accept->second.empty()) {
    client.stated_accept = true;
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
  // Settled from the header alone, and settled now: a GET or a DELETE has
  // no body to say it again, and the answer those get depends on which
  // era asked.
  client.era = protocol::modern::isModernVersion(client.protocol_version)
                   ? transport::ProtocolEra::Modern
                   : transport::ProtocolEra::Classic;
  client.principal = host_.principal();

  // Kept because they are compared against the body, which has not been
  // read yet. Only the mirrored ones: the rest of a request's headers are
  // not this filter's business.
  mirrored_headers_.clear();
  for (const auto& header : headers) {
    if (header.first == "mcp-method" || header.first == "mcp-name" ||
        header.first.compare(0, 10, "mcp-param-") == 0) {
      mirrored_headers_[header.first] = header.second;
    }
  }

  // What the client says it last saw. Judged against the session, so it
  // is read here and placed there.
  last_event_id_ = headerOr(headers, "last-event-id", "");

  if (client.era == transport::ProtocolEra::Modern) {
    // Neither exists in this era. Both are disregarded rather than
    // refused: they are what an older client sends, and a request is not
    // wrong for carrying something this revision simply dropped.
    if (!last_event_id_.empty()) {
      GOPHER_LOG_DEBUG(
          "MCP endpoint request offered a resume point to a revision with no "
          "resumable streams; ignoring it");
      last_event_id_.clear();
    }
  }

  const std::string offered_session = headerOr(headers, "mcp-session-id", "");
  if (sessions_ == nullptr) {
    // Stateless: an inbound session id is not merely unrecognised, it is
    // disregarded. Passing it on would let a caller name any application
    // session it liked on a server that keeps none of its own, and be
    // handed whatever state was sitting under that name.
    if (!offered_session.empty()) {
      GOPHER_LOG_DEBUG(
          "MCP endpoint request presented a session id to a server that keeps "
          "no sessions; ignoring it");
    }
    session_id_.clear();
  } else if (client.era == transport::ProtocolEra::Modern) {
    // Disregarded, exactly as on a server that keeps none: this revision
    // has no sessions, so an id offered by an older client names nothing
    // and must not be allowed to name something.
    if (!offered_session.empty()) {
      GOPHER_LOG_DEBUG(
          "MCP endpoint request presented a session id under a revision that "
          "has none; ignoring it");
    }
    session_id_.clear();
  } else {
    // Recorded now, judged once the body says whether this request needed
    // one — and touched only if that judgement lets it be served.
    session_id_ = offered_session;
  }

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

  if (method_ == kDeleteMethod || method_ == kGetMethod) {
    // Nothing to parse: the headers are the whole request. Both still have
    // to name a session they are entitled to, so both go through the same
    // judgement as everything else.
    validateThenDispatch(std::string());
    return;
  }

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

  // Re-serialized rather than passed through: what reaches the parser is
  // then exactly one document, whatever whitespace or line breaks the peer
  // wrapped it in.
  body_ = message.toString();

  // The method decides whether this request needed a session at all, and
  // it is not known until the body has been read — which is why the check
  // lives here rather than back where the headers arrived.
  std::string method_name;
  if (message.contains("method") && message["method"].isString()) {
    method_name = message["method"].getString();
  }

  // Read before the version is judged, because in the modern era the body
  // is where the version is actually declared and the header only mirrors
  // it. Reading it is not accepting it: what the two say is compared next.
  readModernContext(message);

  if (exchange_->clientContext().era == transport::ProtocolEra::Modern) {
    if (!validateModernRequest(message, method_name)) {
      abandonRequest();
      return;
    }
  }

  if (!settleProtocolVersion(method_name)) {
    abandonRequest();
    return;
  }

  // After the version, because a request for a revision this server does
  // not serve is refused for that rather than for the method it names.
  if (exchange_->clientContext().era == transport::ProtocolEra::Modern &&
      !modernMethodExists(method_name)) {
    abandonRequest();
    return;
  }

  validateThenDispatch(method_name);
}

/**
 * Everything the newest revision requires of a request before it is
 * served, in the order the caller can act on.
 *
 * The body is the source of truth throughout: headers exist so that
 * something between the two ends can route without parsing, and a header
 * that disagrees with the body is exactly the case that would let a
 * router and a server act on different values. So a disagreement is a
 * refusal rather than a preference.
 *
 * @return False when the request has been answered and is over.
 */
bool StreamableHttpFilter::validateModernRequest(
    const json::JsonValue& message, const std::string& method_name) {
  if (!exchange_) {
    return false;
  }
  auto& client = exchange_->clientContext();

  const auto refuse = [this](const std::string& why) {
    GOPHER_LOG_DEBUG("MCP endpoint refused a modern request: {}", why);
    respondWithError(static_cast<int>(http::HttpStatusCode::BadRequest),
                     protocol::modern::kHeaderMismatch, "Bad Request: " + why);
    return false;
  };

  // A response is not something a client may send. There is nothing on
  // this endpoint that asked it a question, so a body carrying an answer
  // is a client speaking rules this era does not have.
  if (method_name.empty() &&
      (message.contains("result") || message.contains("error"))) {
    return refuse(
        "this endpoint takes requests and notifications, and a response "
        "answers a question it never asked");
  }

  // A notification's headers are not specified by this revision, so
  // there is nothing here to hold one to.
  const bool is_request = message.contains("id") && !message["id"].isNull();
  if (!is_request) {
    return true;
  }

  if (body_protocol_version_.empty()) {
    return refuse("every request carries its protocol version in its body");
  }
  if (client.protocol_version.empty()) {
    return refuse(std::string("every request carries the ") +
                  protocol::modern::kProtocolVersionHeader + " header");
  }
  if (client.protocol_version != body_protocol_version_) {
    return refuse(std::string(protocol::modern::kProtocolVersionHeader) +
                  " says " + client.protocol_version + " and the body says " +
                  body_protocol_version_);
  }

  const std::string method_header =
      headerOr(mirrored_headers_, "mcp-method", "");
  if (method_header.empty()) {
    return refuse(std::string("every request carries the ") +
                  protocol::modern::kMethodHeader + " header");
  }
  if (method_header != method_name) {
    return refuse(std::string(protocol::modern::kMethodHeader) + " says " +
                  method_header + " and the body says " + method_name);
  }

  if (protocol::modern::carriesName(method_name)) {
    const std::string field = protocol::modern::nameFieldFor(method_name);
    json::JsonValue named;
    if (message.contains("params") && message["params"].isObject() &&
        message["params"].contains(field)) {
      named = message["params"][field];
    }
    if (named.isNull()) {
      return refuse(method_name + " names what it is about in params." + field +
                    ", and this one does not");
    }

    auto name_header = mirrored_headers_.find("mcp-name");
    if (name_header == mirrored_headers_.end()) {
      return refuse(std::string(method_name) + " carries the " +
                    protocol::modern::kNameHeader + " header");
    }
    if (!protocol::modern::headerMatchesValue(name_header->second, named)) {
      return refuse(std::string(protocol::modern::kNameHeader) + " value '" +
                    name_header->second + "' does not match the body");
    }
  }

  return true;
}

void StreamableHttpFilter::readModernContext(const json::JsonValue& message) {
  if (!exchange_) {
    return;
  }
  auto& client = exchange_->clientContext();

  if (!message.contains("params") || !message["params"].isObject()) {
    return;
  }
  const auto& params = message["params"];
  if (!params.contains("_meta") || !params["_meta"].isObject()) {
    return;
  }
  const auto& meta = params["_meta"];

  // The version in the body is the one that counts; the header mirrors
  // it. Taken here so that the comparison between them has something to
  // compare, and so that the era is settled by the body on a transport
  // where the header could have been rewritten in flight.
  if (meta.contains(protocol::modern::kMetaProtocolVersion) &&
      meta[protocol::modern::kMetaProtocolVersion].isString()) {
    body_protocol_version_ =
        meta[protocol::modern::kMetaProtocolVersion].getString();
    // A body declaring the modern revision is a modern request even if
    // its header does not say so — that is a modern client with a
    // missing header, and it has to be told which of those it is rather
    // than quietly served by the older rules.
    if (protocol::modern::isModernVersion(body_protocol_version_)) {
      client.era = transport::ProtocolEra::Modern;
    }
  }

  // Kept as they arrived. Neither is the transport's business beyond
  // carrying it; whoever needs a field parses what it needs.
  if (meta.contains(protocol::modern::kMetaClientInfo) &&
      meta[protocol::modern::kMetaClientInfo].isObject()) {
    client.client_info =
        mcp::make_optional(meta[protocol::modern::kMetaClientInfo].toString());
  }
  if (meta.contains(protocol::modern::kMetaClientCapabilities) &&
      meta[protocol::modern::kMetaClientCapabilities].isObject()) {
    client.client_capabilities = mcp::make_optional(
        meta[protocol::modern::kMetaClientCapabilities].toString());
  }
}

bool StreamableHttpFilter::settleProtocolVersion(
    const std::string& method_name) {
  if (!exchange_) {
    return false;
  }

  auto& client = exchange_->clientContext();

  if (method_name == kInitializeMethod) {
    // Which revision the two ends will speak is what initialize is for.
    // Judging its header would be refusing the conversation that decides
    // the answer.
    return true;
  }

  if (client.protocol_version.empty()) {
    // The header only became mandatory after this revision, so a request
    // without one is from a peer speaking that revision rather than from
    // a peer that forgot.
    client.protocol_version = protocol::kLegacyAssumedVersion;
    return true;
  }

  if (options_.protocol_versions.empty()) {
    // No list configured is no opinion, and refuses nothing.
    return true;
  }

  if (protocol::isSupportedVersion(client.protocol_version,
                                   options_.protocol_versions)) {
    return true;
  }

  // Named rather than merely refused: a peer told only "no" retries the
  // same request, and a peer told what is served can pick something.
  std::string served;
  for (const auto& version : options_.protocol_versions) {
    if (!served.empty()) {
      served += ", ";
    }
    served += version;
  }
  GOPHER_LOG_DEBUG(
      "MCP endpoint asked for protocol revision {}, which it "
      "does not serve",
      client.protocol_version);

  if (client.era == transport::ProtocolEra::Modern) {
    // The same refusal, in the shape its own era reads: a code of its
    // own, and the list as data rather than as prose, so a client can
    // pick from it without parsing a sentence.
    json::JsonValue supported = json::JsonValue::array();
    for (const auto& version : options_.protocol_versions) {
      supported.push_back(json::JsonValue(version));
    }
    json::JsonValue data = json::JsonValue::object();
    data.set(protocol::modern::kSupportedVersionsField, supported);
    data.set(protocol::modern::kRequestedVersionField,
             json::JsonValue(client.protocol_version));
    respondWithError(static_cast<int>(http::HttpStatusCode::BadRequest),
                     protocol::modern::kUnsupportedProtocolVersion,
                     "Unsupported protocol version", data);
    return false;
  }

  respondWithError(static_cast<int>(http::HttpStatusCode::BadRequest),
                   jsonrpc::INVALID_REQUEST,
                   "Bad Request: unsupported MCP-Protocol-Version " +
                       client.protocol_version + "; this server serves " +
                       served);
  return false;
}

bool StreamableHttpFilter::modernMethodExists(const std::string& method_name) {
  if (!exchange_ || method_name.empty()) {
    return true;
  }
  if (mcp_callbacks_.knowsMethod(method_name)) {
    return true;
  }

  // 404 with a JSON-RPC error, which is what distinguishes a server that
  // is there and has no such method from a URL that is not an endpoint at
  // all — and a client detecting which era it is talking to reads exactly
  // that difference.
  GOPHER_LOG_DEBUG("MCP endpoint has no method '{}'", method_name);
  respondWithError(static_cast<int>(http::HttpStatusCode::NotFound),
                   protocol::modern::kMethodNotFound,
                   "Method not found: " + method_name);
  return false;
}

StreamableHttpFilter::SessionVerdict StreamableHttpFilter::judgeSession(
    const std::string& id, bool exempt) const {
  if (sessions_ == nullptr) {
    // Stateless: there is nothing to present and nothing to withhold.
    return SessionVerdict::Serve;
  }
  if (id.empty()) {
    return exempt ? SessionVerdict::Serve : SessionVerdict::Missing;
  }

  transport::SessionCtx* session = sessions_->find(id);
  if (session == nullptr) {
    return SessionVerdict::Unknown;
  }
  if (!transport::StreamableSessionManager::secureEquals(session->id, id)) {
    return SessionVerdict::Unknown;
  }

  if (options_.require_principal_match &&
      !transport::StreamableSessionManager::secureEquals(session->principal,
                                                         host_.principal())) {
    // Holding the id is not the same as being the caller it was minted
    // for. Deliberately without a touch: a caller who is not entitled to
    // the session must not be able to keep it alive either.
    return SessionVerdict::WrongPrincipal;
  }

  session->last_activity = std::chrono::steady_clock::now();
  return SessionVerdict::Serve;
}

void StreamableHttpFilter::refuseSession(SessionVerdict verdict) {
  switch (verdict) {
    case SessionVerdict::Missing:
      GOPHER_LOG_DEBUG("MCP endpoint request arrived without a session id");
      respondWithError(static_cast<int>(http::HttpStatusCode::BadRequest),
                       jsonrpc::INVALID_REQUEST,
                       "Bad Request: Mcp-Session-Id is required for every "
                       "request after initialize");
      return;
    case SessionVerdict::Unknown:
      // The status a client is told to re-initialize on, which is the only
      // way back from a session that no longer exists.
      GOPHER_LOG_DEBUG("MCP endpoint request named a session that is gone");
      respondWithError(static_cast<int>(http::HttpStatusCode::NotFound),
                       jsonrpc::INVALID_REQUEST,
                       "Not Found: no such session; send initialize again");
      return;
    case SessionVerdict::WrongPrincipal:
      GOPHER_LOG_WARN(
          "MCP endpoint request presented a session belonging to someone "
          "else");
      respondWithError(static_cast<int>(http::HttpStatusCode::Forbidden),
                       jsonrpc::INVALID_REQUEST,
                       "Forbidden: this session belongs to another caller");
      return;
    case SessionVerdict::Serve:
      return;
  }
}

void StreamableHttpFilter::validateThenDispatch(
    const std::string& method_name) {
  const bool exempt = method_name == kInitializeMethod;

  if (sessions_ != nullptr && exempt && !session_id_.empty() &&
      !sessions_->known(session_id_)) {
    // Introducing yourself is how a client recovers from a session that is
    // gone, so a stale id here is dropped rather than refused — otherwise
    // the one request that could get the client a new session is the one
    // its old id prevents.
    GOPHER_LOG_DEBUG("initialize arrived with a session id that is gone");
    session_id_.clear();
  }

  if (sessions_ == nullptr || exempt || session_id_.empty() ||
      sessions_->ownedBy(session_id_, dispatcher_)) {
    // Everything needed is readable from here.
    Judgement judged;
    judged.verdict = judgeSession(session_id_, exempt);
    if (judged.verdict == SessionVerdict::Serve && sessions_ != nullptr) {
      if (auto* session = sessions_->find(session_id_)) {
        judged.live_get_streams =
            transport::StreamableSessionManager::countStreams(
                *session, transport::StreamCtx::Kind::Get,
                /*connected_only=*/false);
        judged.resume = transport::StreamableSessionManager::resumeFrom(
            *session, last_event_id_);
      }
    }
    resumeAfterValidation(judged);
    return;
  }

  // The session belongs to another thread and only that thread may read
  // it. Nothing further on this connection is parsed until the answer
  // comes back: HTTP/1.1 answers in request order, so a request behind
  // this one cannot be answered first.
  parked_ = true;
  host_.holdInput(true);

  auto judged = std::make_shared<Judgement>();
  const std::string id = session_id_;
  const std::string resume_from = last_event_id_;
  std::weak_ptr<int> alive = alive_;

  const bool terminating = method_ == kDeleteMethod;

  sessions_->withSession(
      dispatcher_, id,
      [this, judged, id, exempt, terminating,
       resume_from](transport::SessionCtx& session) {
        judged->verdict = judgeSession(id, exempt);
        if (judged->verdict != SessionVerdict::Serve) {
          return;
        }
        if (terminating) {
          // Ending it belongs on the thread that owns it, which is this
          // one and not the one the request arrived on.
          sessions_->remove(id);
          return;
        }
        // Counted in the same visit, since this is the only thread that
        // may look at the session's streams at all — and for the same
        // reason, where the client says it got to is placed here too.
        judged->live_get_streams =
            transport::StreamableSessionManager::countStreams(
                session, transport::StreamCtx::Kind::Get,
                /*connected_only=*/false);
        judged->resume = transport::StreamableSessionManager::resumeFrom(
            session, resume_from);
      },
      [this, judged, alive](bool found) {
        if (alive.expired()) {
          // The connection died while its request was being judged. There
          // is nobody left to answer and nothing left to answer through.
          return;
        }
        if (!found) {
          judged->verdict = SessionVerdict::Unknown;
        }
        resumeAfterValidation(*judged);
      });
}

void StreamableHttpFilter::resumeAfterValidation(const Judgement& judged) {
  // Cleared first so the paths below do not lift the hold themselves, and
  // released last — see the end of this function.
  const bool was_parked = parked_;
  parked_ = false;

  if (exchange_) {
    const bool modern =
        exchange_->clientContext().era == transport::ProtocolEra::Modern;
    if (refuseNonPostForModern()) {
      abandonRequest();
    } else if (!modern && judged.verdict != SessionVerdict::Serve) {
      // Skipped for a modern request, which has no session to judge: the
      // verdict would refuse it for not carrying an id its own revision
      // does not have.
      refuseSession(judged.verdict);
      abandonRequest();
    } else if (method_ == kDeleteMethod) {
      terminateSession();
    } else if (method_ == kGetMethod) {
      openEventStream(judged);
    } else {
      dispatchBody();
    }
  }

  if (was_parked) {
    // Only now. Letting input through any earlier would parse the next
    // request on this connection while this one still owed an answer —
    // and the next request begins by taking over the per-request state
    // this one is answering from, so its answer would simply never be
    // written.
    host_.holdInput(false);
  }
}

bool StreamableHttpFilter::refuseNonPostForModern() {
  if (!exchange_ || method_ == "POST") {
    return false;
  }
  if (exchange_->clientContext().era != transport::ProtocolEra::Modern) {
    return false;
  }

  // The newest revision serves POST and nothing else: no standalone
  // stream to open with a GET, and no session to end with a DELETE. The
  // Allow header says POST alone whatever else this endpoint serves for
  // older callers, because it answers what *this* caller may send.
  GOPHER_LOG_DEBUG(
      "MCP endpoint refused {} from a caller speaking a revision "
      "that serves POST alone",
      method_);
  exchange_->setPhase(transport::RequestExchange::Phase::RespondingError);
  exchange_->setStatus(
      static_cast<int>(http::HttpStatusCode::MethodNotAllowed));
  exchange_->setResponseHeader("Allow", "POST");
  exchange_->respondUnary(
      "application/json",
      idLessError(jsonrpc::INVALID_REQUEST,
                  "Method Not Allowed: this revision serves POST alone"));
  return true;
}

void StreamableHttpFilter::openEventStream(const Judgement& judged) {
  if (!exchange_) {
    return;
  }
  const size_t live_streams = judged.live_get_streams;

  const auto& client = exchange_->clientContext();
  if (!client.stated_accept || !client.accepts_sse) {
    // There is only one thing a GET here produces, so a client that has
    // not asked for it by name has not asked for it — whether it named
    // something else or named nothing at all.
    GOPHER_LOG_DEBUG("event stream refused: the client did not ask for one{}",
                     client.stated_accept ? "" : " (no Accept header)");
    respondWithError(static_cast<int>(http::HttpStatusCode::NotAcceptable),
                     jsonrpc::INVALID_REQUEST,
                     "Not Acceptable: this endpoint answers GET with "
                     "text/event-stream, which this request did not ask for");
    abandonRequest();
    return;
  }

  if (live_streams >= options_.max_get_streams_per_session) {
    // Holding several at once is allowed, so this is a bound on memory
    // rather than a rule about how many a client ought to want.
    GOPHER_LOG_DEBUG("event stream refused: session already holds {}",
                     live_streams);
    respondWithError(static_cast<int>(http::HttpStatusCode::TooManyRequests),
                     jsonrpc::INVALID_REQUEST,
                     "Too Many Requests: this session already holds as many "
                     "event streams as it may");
    abandonRequest();
    return;
  }

  // Worth keeping when its client goes away: the stream belongs to the
  // session rather than to the connection, and a client that comes back is
  // owed whatever it missed.
  exchange_->setRetainOnDisconnect(true);
  // Named before it says anything, since the name is what every event on
  // it is numbered under and a client comes back holding one of those.
  const std::string stream_id = nameThisStream(exchange_);
  if (!exchange_->beginStream()) {
    GOPHER_LOG_ERROR("event stream failed to open");
    abandonRequest();
    return;
  }
  exchange_->setPhase(transport::RequestExchange::Phase::RespondingSseOpen);

  // Deliberately no endpoint event. The older transport announces a
  // callback URL as its first event; this one has no separate endpoint to
  // announce, and a client reading one here would post its requests
  // somewhere that does not exist.
  replayThenRegister(judged.resume, stream_id);
  abandonRequest();
}

std::string StreamableHttpFilter::nameThisStream(
    const transport::RequestExchangePtr& exchange) {
  if (sessions_ == nullptr || !exchange) {
    // Nowhere to look a returning client up, so there is nothing to be
    // gained by naming what it would be looking for.
    return std::string();
  }

  const std::string stream_id = sessions_->reserveStreamId();
  if (stream_id.empty() || !options_.enable_resumability) {
    // The name is still worth having — it is what the session's own
    // bookkeeping goes under — but without resumability nothing is kept
    // behind it, and so nothing says it on the wire.
    return stream_id;
  }
  exchange->setRetainedEventLimit(options_.replay_buffer_events);
  exchange->makeResumable(stream_id, sessions_->accounting());
  return stream_id;
}

void StreamableHttpFilter::replayThenRegister(
    const transport::StreamableSessionManager::ResumePoint& resume,
    const std::string& stream_id) {
  transport::RequestExchangePtr exchange = exchange_;
  if (!exchange) {
    return;
  }

  // Remembered before anything else can go wrong, so this connection
  // going away still detaches the stream from its session.
  get_stream_exchange_ = exchange;
  get_stream_session_id_ = session_id_;
  get_stream_conn_ = host_.connection();

  if (!resume.found || !resume.exchange || resume.dispatcher == nullptr ||
      !options_.enable_resumability) {
    registerEventStream(exchange, stream_id);
    armKeepalive();
    return;
  }

  auto deliver = [this, exchange, stream_id](
                     const std::vector<transport::RetainedEvent>& missed) {
    for (const auto& event : missed) {
      // Under the id it was first sent with: the client's place in the
      // stream it lost has to go on meaning the same thing, and it is
      // that id it would come back with a second time.
      exchange->writeEvent(event.event, event.data,
                           optional<std::string>(event.id));
    }
    GOPHER_LOG_DEBUG("replayed {} event(s) to a resumed stream", missed.size());
    // Only now: what the session hands over on registration is what the
    // server said while nothing was connected, which comes after.
    registerEventStream(exchange, stream_id);
    armKeepalive();
  };

  // Only an answering stream is followed. A standalone one that was lost
  // is replaced by this one, and what the server says next goes to the
  // newest stream anyway — following it as well would send everything
  // twice. An answering stream still has a handler behind it, producing
  // for a request this client cannot ask again.
  transport::RequestExchangePtr follower =
      resume.kind == transport::StreamCtx::Kind::PostResponse ? exchange
                                                              : nullptr;

  if (resume.dispatcher->isThreadSafe()) {
    deliver(transport::StreamableSessionManager::collectAndFollow(
        resume.exchange, resume.cursor, follower, &dispatcher_));
    return;
  }

  // The stream being resumed was running over a connection on another
  // thread, and its buffer may only be read there. The answer waits: it
  // has already begun, nothing else is being served on this connection
  // while it is open, and there is nothing else to give this client.
  event::Dispatcher* mine = &dispatcher_;
  event::Dispatcher* theirs = resume.dispatcher;
  std::weak_ptr<int> alive = alive_;
  transport::RequestExchangePtr source = resume.exchange;
  const std::string cursor = resume.cursor;

  theirs->post([source, cursor, follower, mine, deliver, alive]() {
    auto missed = transport::StreamableSessionManager::collectAndFollow(
        source, cursor, follower, mine);
    mine->post([deliver, missed, alive]() {
      if (alive.expired()) {
        return;
      }
      deliver(missed);
    });
  });
}

void StreamableHttpFilter::armKeepalive() {
  if (!get_stream_exchange_ || options_.keepalive_interval.count() <= 0) {
    return;
  }
  if (!keepalive_timer_) {
    keepalive_timer_ = dispatcher_.createTimer([this]() {
      if (!get_stream_exchange_ ||
          get_stream_exchange_->mode() !=
              transport::RequestExchange::Mode::Stream) {
        // Nothing to keep alive any more, and nothing to re-arm for.
        return;
      }
      get_stream_exchange_->writeComment("keep-alive");
      armKeepalive();
    });
  }
  keepalive_timer_->enableTimer(options_.keepalive_interval);
}

void StreamableHttpFilter::registerEventStream(
    const transport::RequestExchangePtr& exchange,
    const std::string& stream_id) {
  if (sessions_ == nullptr || !exchange) {
    return;
  }

  const std::string id = get_stream_session_id_;
  network::Connection* conn = get_stream_conn_;
  event::Dispatcher* dispatcher = &dispatcher_;
  transport::StreamableSessionManager* sessions = sessions_;

  auto attach = [sessions, exchange, conn, dispatcher,
                 stream_id](transport::SessionCtx& session) {
    sessions->openStream(session, stream_id, transport::StreamCtx::Kind::Get,
                         exchange, conn, *dispatcher);
  };

  if (sessions_->ownedBy(id, dispatcher_)) {
    if (auto* session = sessions_->find(id)) {
      attach(*session);
    }
    return;
  }
  // The session lives on another thread, so the record of the stream is
  // made there. The bytes stay here: the exchange may only be touched
  // where its connection is, and the name it is writing under was settled
  // before either of those threads had to agree on anything.
  sessions_->withSession(dispatcher_, id, attach, nullptr);
}

void StreamableHttpFilter::registerResponseStream(
    const transport::RequestExchangePtr& exchange,
    const std::string& session_id,
    const std::string& stream_id) {
  if (sessions_ == nullptr || !exchange || session_id.empty() ||
      stream_id.empty()) {
    return;
  }

  // The connection is deliberately not recorded. An answering stream ends
  // with its answer, and a client that lost it comes back on a stream of
  // its own rather than expecting this one to be reattached.
  event::Dispatcher* dispatcher = &dispatcher_;
  transport::StreamableSessionManager* sessions = sessions_;
  network::Connection* conn = host_.connection();

  auto attach = [sessions, exchange, conn, dispatcher,
                 stream_id](transport::SessionCtx& session) {
    sessions->openStream(session, stream_id,
                         transport::StreamCtx::Kind::PostResponse, exchange,
                         conn, *dispatcher);
  };

  if (sessions_->ownedBy(session_id, dispatcher_)) {
    if (auto* session = sessions_->find(session_id)) {
      attach(*session);
    }
    return;
  }
  sessions_->withSession(dispatcher_, session_id, attach, nullptr);
}

void StreamableHttpFilter::terminateSession() {
  if (!exchange_) {
    return;
  }

  if (sessions_ != nullptr && !session_id_.empty() &&
      sessions_->ownedBy(session_id_, dispatcher_)) {
    // Owned here means the judgement ran here too, so the session is still
    // standing and this is where it ends. When it is owned elsewhere the
    // thread that judged it has already ended it — the same predicate
    // chose that path.
    sessions_->remove(session_id_);
  }

  GOPHER_LOG_DEBUG("MCP endpoint ended session {} at the client's request",
                   session_id_);

  // There is genuinely nothing to say back: the session the client was
  // asking about no longer exists.
  exchange_->setPhase(transport::RequestExchange::Phase::Responding202);
  exchange_->setStatus(static_cast<int>(http::HttpStatusCode::NoContent));
  exchange_->respondUnary("", "");
  abandonRequest();
}

void StreamableHttpFilter::dispatchBody() {
  auto exchange = exchange_;

  exchange->setPhase(transport::RequestExchange::Phase::Dispatching);

  OwnedBuffer parsed;
  parsed.add(body_);
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

void StreamableHttpFilter::mintSessionFor(const jsonrpc::Request& request) {
  if (sessions_ == nullptr || !exchange_) {
    return;
  }
  if (exchange_->clientContext().era == transport::ProtocolEra::Modern) {
    // The newest revision has no sessions at all. Minting one for a
    // client that will never echo it would leave state behind on every
    // request, and naming it in the answer would invite a client to.
    return;
  }
  if (request.method != kInitializeMethod) {
    return;
  }
  if (!session_id_.empty()) {
    // The client already has one. Whether it is still a session this
    // server recognises is a separate question, and not one for here.
    return;
  }

  transport::SessionCtx* session =
      sessions_->createSession(dispatcher_, host_.principal());
  if (session == nullptr) {
    // No id could be drawn. The request is still answerable — it just
    // answers a client that will have to keep introducing itself.
    GOPHER_LOG_ERROR("MCP endpoint could not mint a session id");
    return;
  }

  session_id_ = session->id;
  minted_session_id_ = session->id;

  // Attached now rather than when the answer is written, because an answer
  // that streams has its headers on the wire before the handler has said
  // anything at all.
  exchange_->setResponseHeader(kSessionHeader, session->id);
  GOPHER_LOG_DEBUG("MCP endpoint minted session {} for principal '{}'",
                   session->id, session->principal);
}

void StreamableHttpFilter::settleMintedSession(
    const jsonrpc::Response& response) {
  if (minted_session_id_.empty() || sessions_ == nullptr) {
    return;
  }
  const std::string id = minted_session_id_;
  minted_session_id_.clear();

  if (response.error.has_value()) {
    // The client is not initialized, so there is nothing for a session to
    // be the continuation of. Handing back an id here would have it echoed
    // on every later request and refused every time.
    if (exchange_ && exchange_->removeResponseHeader(kSessionHeader)) {
      sessions_->remove(id);
      session_id_.clear();
      GOPHER_LOG_DEBUG("session {} dropped: initialize was refused", id);
      return;
    }
    // The id has already gone out — a streamed answer announces its
    // headers before the handler has decided anything — so the session
    // stands rather than being withdrawn behind the client's back.
    GOPHER_LOG_DEBUG("session {} kept: its id had already been sent", id);
    return;
  }

  transport::SessionCtx* session = sessions_->find(id);
  if (session == nullptr) {
    return;
  }
  session->negotiated_protocol_version = negotiatedVersion(response);
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

void StreamableHttpFilter::respondWithError(int status_code,
                                            int code,
                                            const std::string& message,
                                            const json::JsonValue& data) {
  if (!exchange_) {
    return;
  }
  exchange_->setPhase(transport::RequestExchange::Phase::RespondingError);
  exchange_->setStatus(status_code);
  exchange_->respondUnary("application/json", idLessError(code, message, data));
}

void StreamableHttpFilter::abandonRequest() {
  if (parked_) {
    // Whatever is coming back for this request has nothing to answer any
    // more, so stop holding the connection's input on its behalf.
    parked_ = false;
    host_.holdInput(false);
  }
  body_.clear();
  session_id_.clear();
  body_protocol_version_.clear();
  mirrored_headers_.clear();
  minted_session_id_.clear();
  method_.clear();
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

  // Before the framing decision below, because a streamed answer puts its
  // headers on the wire the moment it opens, and the session id is one of
  // them.
  mintSessionFor(request);

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
