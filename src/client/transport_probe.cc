/**
 * Working out which protocol era a server speaks.
 */

#include "mcp/client/transport_probe.h"

#include "mcp/json/json_bridge.h"
#include "mcp/logging/log_macros.h"
#include "mcp/network/transport_socket.h"
#include "mcp/transport/https_sse_transport_factory.h"

#undef GOPHER_LOG_COMPONENT
#define GOPHER_LOG_COMPONENT "client"

namespace mcp {
namespace client {

namespace {

/** True when the error's data names the version complaint by name. */
bool namesUnsupportedVersion(const json::JsonValue& error) {
  const auto mentions = [](const std::string& text) {
    return text.find(modern_error::kUnsupportedProtocolVersionName) !=
           std::string::npos;
  };

  if (error.contains("data")) {
    const auto& data = error["data"];
    if (data.isString() && mentions(data.getString())) {
      return true;
    }
    if (data.isObject() && data.contains("type") && data["type"].isString() &&
        mentions(data["type"].getString())) {
      return true;
    }
  }
  // Some servers put it in the message instead. Reading both costs
  // nothing and means the classification does not depend on which one a
  // given implementation chose.
  return error.contains("message") && error["message"].isString() &&
         mentions(error["message"].getString());
}

}  // namespace

bool isModernRefusal(int status_code, const std::string& body) {
  // Only these three, which are the ones the revision's own fallback rule
  // names: a modern server refusing an introduction it has no concept of
  // answers with one of them. Anything else is a different conversation,
  // and reading a 500 or a 200 this way would stop the ladder over
  // something that says nothing about the era.
  if (status_code != 400 && status_code != 404 && status_code != 405) {
    return false;
  }
  if (body.empty()) {
    return false;
  }

  json::JsonValue parsed;
  try {
    parsed = json::JsonValue::parse(body);
  } catch (const std::exception&) {
    // A body that is not JSON is not a JSON-RPC refusal. A plain 404
    // page is the ordinary case.
    return false;
  }

  if (!parsed.isObject() || !parsed.contains("error")) {
    return false;
  }

  // An `error` that is not an object is not a JSON-RPC error, whatever
  // it says. This project's own 404 for an unknown path is
  // {"error":"not_found"} — reading that as a modern server would have
  // a client refuse to fall back to the transport that would have
  // worked.
  const auto& error = parsed["error"];
  if (!error.isObject()) {
    return false;
  }

  if (error.contains("code") && error["code"].isInteger()) {
    const int code = error["code"].getInt();
    if (code == modern_error::kHeaderMismatch ||
        code == modern_error::kMethodNotFound ||
        code == modern_error::kUnsupportedProtocolVersion ||
        code == modern_error::kMissingRequiredClientCapability) {
      return true;
    }
  }

  return namesUnsupportedVersion(error);
}

bool isInitializeAnswer(int status_code,
                        const std::string& content_type,
                        const std::string& body) {
  if (status_code < 200 || status_code >= 300) {
    return false;
  }
  // A stream is where the answer will be, and asking for the body to
  // prove it would mean reading the stream to find out whether to open
  // one.
  if (content_type.find("text/event-stream") != std::string::npos) {
    return true;
  }

  json::JsonValue parsed;
  try {
    parsed = json::JsonValue::parse(body);
  } catch (const std::exception&) {
    return false;
  }
  // A result, not merely something well-formed: 202 with an empty body
  // is the older transport accepting a message rather than answering
  // it.
  return parsed.isObject() && parsed.contains("result");
}

void NoModernProbe::probe(const std::string& url, ProbeCallback done) {
  (void)url;
  GOPHER_LOG_DEBUG("Modern probe not built; treating the server as not modern");
  if (done) {
    done(ProbeResult::notModern(0, std::string()));
  }
}

ClassicProbe::ClassicProbe(event::Dispatcher& dispatcher,
                           network::SocketInterface& socket_interface,
                           std::string protocol_version,
                           std::string client_name,
                           std::string client_version,
                           std::chrono::milliseconds timeout)
    : dispatcher_(dispatcher),
      socket_interface_(socket_interface),
      timeout_(timeout),
      protocol_version_(std::move(protocol_version)),
      client_name_(std::move(client_name)),
      client_version_(std::move(client_version)) {}

bool probeRequiresTls(const std::string& url) {
  // A question asked in plaintext of a server expecting TLS is not a
  // question that gets an answer, and the silence would be read as this
  // server not speaking anything — which is the wrong conclusion drawn
  // from the wrong evidence.
  return url.size() >= 8 && url.compare(0, 8, "https://") == 0;
}

std::unique_ptr<http::HttpAsyncClient> ClassicProbe::clientFor(
    const std::string& url) {
  const bool secure = probeRequiresTls(url);

  std::unique_ptr<network::TransportSocketFactoryBase> factory;
  if (secure) {
    transport::HttpSseTransportSocketConfig config;
    config.mode = transport::HttpSseTransportSocketConfig::Mode::CLIENT;
    config.underlying_transport =
        transport::HttpSseTransportSocketConfig::UnderlyingTransport::SSL;
    transport::HttpSseTransportSocketConfig::SslConfig ssl;
    // Judged exactly as the connection this is deciding for will be
    // judged. A probe held to a stricter standard than the connection
    // that follows it answers a question nobody asked: it would refuse
    // a certificate the real connection goes on to accept, and report
    // that as this server speaking nothing.
    //
    // What that standard should be is a separate question, and not one
    // to settle here by accident — this follows createConnectionConfig,
    // and changing it means changing both together and on purpose.
    ssl.verify_peer = false;
    ssl.alpn_protocols = std::vector<std::string>{"http/1.1"};
    // The host to present, taken from the URL between the scheme and
    // whatever ends it.
    const size_t host_start = 8;
    size_t host_end = url.find('/', host_start);
    if (host_end == std::string::npos) {
      host_end = url.size();
    }
    std::string host = url.substr(host_start, host_end - host_start);
    const size_t colon = host.rfind(':');
    if (colon != std::string::npos) {
      host = host.substr(0, colon);
    }
    if (!host.empty()) {
      ssl.sni_hostname = mcp::make_optional(host);
    }
    config.ssl_config = mcp::make_optional(ssl);
    factory = transport::createHttpsSseTransportFactory(config, dispatcher_);
  } else {
    factory.reset(new network::RawBufferTransportSocketFactory());
  }

  return std::unique_ptr<http::HttpAsyncClient>(new http::HttpAsyncClient(
      dispatcher_, socket_interface_, std::move(factory)));
}

ClassicProbe::~ClassicProbe() = default;

void ClassicProbe::probe(const std::string& url, ProbeCallback done) {
  done_ = std::move(done);
  http_ = clientFor(url);

  // The introduction, spelled the way this transport spells it: both
  // content types accepted, because a server may answer either with a
  // body or with a stream, and refusing one of them here would be
  // asking a narrower question than the one being asked.
  http::HttpRequest request;
  request.method = "POST";
  request.url = url;
  request.headers["Content-Type"] = "application/json";
  request.headers["Accept"] = "application/json, text/event-stream";
  request.body =
      "{\"jsonrpc\":\"2.0\",\"id\":0,\"method\":\"initialize\","
      "\"params\":{\"protocolVersion\":\"" +
      protocol_version_ + "\",\"capabilities\":{},\"clientInfo\":{\"name\":\"" +
      client_name_ + "\",\"version\":\"" + client_version_ + "\"}}}";

  // Its own deadline, because the client underneath has none: a server
  // that accepts a connection and then says nothing would otherwise
  // hold every rung below this one.
  deadline_ = dispatcher_.createTimer([this]() {
    GOPHER_LOG_DEBUG("No answer to the introduction within {}ms",
                     timeout_.count());
    settle(ProbeResult::unreachable("no answer within " +
                                    std::to_string(timeout_.count()) + "ms"));
  });
  deadline_->enableTimer(timeout_);

  const bool sent = http_->send(
      request,
      [this](http::HttpResponse response) {
        std::string session_id;
        auto it = response.headers.find("mcp-session-id");
        if (it == response.headers.end()) {
          it = response.headers.find("Mcp-Session-Id");
        }
        if (it != response.headers.end()) {
          session_id = it->second;
        }
        std::string content_type;
        auto type_it = response.headers.find("content-type");
        if (type_it == response.headers.end()) {
          type_it = response.headers.find("Content-Type");
        }
        if (type_it != response.headers.end()) {
          content_type = type_it->second;
        }
        settle(ProbeResult::notModern(
            response.status_code, std::move(response.body),
            std::move(session_id), std::move(content_type)));
      },
      [this](const std::string& error) {
        settle(ProbeResult::unreachable(error));
      },
      [](int status_code, const std::map<std::string, std::string>& headers) {
        // An answer that arrives as a stream is an answer, and waiting
        // for it to finish is waiting for the server to stop talking.
        // Everything the ladder needs is in the headers by now: the
        // status, the kind of answer, and the session it was given.
        auto type = headers.find("content-type");
        if (type == headers.end()) {
          type = headers.find("Content-Type");
        }
        (void)status_code;
        return type != headers.end() &&
               type->second.find("text/event-stream") != std::string::npos;
      });

  if (!sent) {
    settle(ProbeResult::unreachable("could not be asked: " + url));
  }
}

void ClassicProbe::settle(const ProbeResult& result) {
  if (deadline_) {
    deadline_->disableTimer();
  }
  // Taken before it is called, so that whichever of the answer and the
  // deadline lost the race finds nothing left to report to.
  ProbeCallback done = std::move(done_);
  done_ = nullptr;
  if (done) {
    done(result);
  }
}

}  // namespace client
}  // namespace mcp
