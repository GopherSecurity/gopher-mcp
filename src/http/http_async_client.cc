#include "mcp/http/http_async_client.h"

#include <cassert>
#include <memory>
#include <sstream>
#include <string>
#include <utility>

#include "mcp/buffer.h"
#include "mcp/filter/http_codec_filter.h"
#include "mcp/network/address.h"
#include "mcp/network/address_impl.h"
#include "mcp/network/connection.h"
#include "mcp/network/connection_impl.h"
#include "mcp/network/socket_impl.h"

namespace mcp {
namespace http {

namespace {

// Minimal URL parser — the input contract says "absolute URL with an
// IPv4 literal host", which is all the current callers produce. We
// stay strict and return false on anything we don't recognize so
// callers get a clean error instead of a mystery connection attempt.
struct ParsedUrl {
  bool tls{false};
  std::string host;
  uint16_t port{0};
  std::string path{"/"};
};

bool parseUrl(const std::string& url, ParsedUrl& out) {
  auto proto_end = url.find("://");
  if (proto_end == std::string::npos) {
    return false;
  }
  const std::string scheme = url.substr(0, proto_end);
  if (scheme == "http") {
    out.tls = false;
    out.port = 80;
  } else if (scheme == "https") {
    out.tls = true;
    out.port = 443;
  } else {
    return false;
  }

  const size_t host_start = proto_end + 3;
  const size_t path_start = url.find('/', host_start);
  std::string hostport = (path_start == std::string::npos)
                             ? url.substr(host_start)
                             : url.substr(host_start, path_start - host_start);
  out.path = (path_start == std::string::npos) ? std::string{"/"}
                                               : url.substr(path_start);
  if (hostport.empty()) {
    return false;
  }
  const auto colon = hostport.find(':');
  if (colon == std::string::npos) {
    out.host = hostport;
  } else {
    out.host = hostport.substr(0, colon);
    try {
      out.port = static_cast<uint16_t>(std::stoi(hostport.substr(colon + 1)));
    } catch (...) {
      return false;
    }
  }
  return !out.host.empty();
}

std::string statusTextForCode(int code) {
  switch (code) {
    case 200:
      return "OK";
    case 201:
      return "Created";
    case 202:
      return "Accepted";
    case 204:
      return "No Content";
    case 400:
      return "Bad Request";
    case 401:
      return "Unauthorized";
    case 403:
      return "Forbidden";
    case 404:
      return "Not Found";
    case 500:
      return "Internal Server Error";
    default:
      return "";
  }
}

}  // namespace

/**
 * RequestContext - per-request state for HttpAsyncClient.
 *
 * Owns the outbound connection and the response-parsing codec filter
 * for a single HTTP request. Lives until the response arrives (or an
 * error occurs), at which point HttpAsyncClient::finishRequest hands
 * the context to Dispatcher::deferredDelete so teardown runs past the
 * current callback frame.
 */
class HttpAsyncClient::RequestContext
    : public network::ConnectionCallbacks,
      public filter::HttpCodecFilter::MessageCallbacks,
      public event::DeferredDeletable {
 public:
  RequestContext(HttpAsyncClient& parent,
                 HttpRequest request,
                 ParsedUrl url,
                 HttpResponseCallback on_response,
                 HttpErrorCallback on_error)
      : parent_(parent),
        request_(std::move(request)),
        url_(std::move(url)),
        on_response_(std::move(on_response)),
        on_error_(std::move(on_error)) {}

  // Build the connection and codec filter, then initiate connect().
  // Caller must guarantee dispatcher-thread context.
  bool start() {
    auto& dispatcher = parent_.dispatcher_;
    auto& socket_interface = parent_.socket_interface_;

    // Build remote address from the IPv4 literal in the URL.
    auto remote_address =
        std::make_shared<network::Address::Ipv4Instance>(url_.host, url_.port);

    // Create a non-blocking TCP socket via the MCP socket interface.
    auto fd_result = socket_interface.socket(network::SocketType::Stream,
                                             network::Address::Type::Ip,
                                             network::Address::IpVersion::v4,
                                             /*v6only=*/false);
    if (!fd_result.ok()) {
      return false;
    }
    auto io_handle =
        socket_interface.ioHandleForFd(*fd_result.value, /*socket_v6only=*/false);
    if (!io_handle) {
      socket_interface.close(*fd_result.value);
      return false;
    }
    io_handle->setBlocking(false);

    auto local_address =
        network::Address::anyAddress(network::Address::IpVersion::v4, 0);
    auto connection_socket = std::make_unique<network::ConnectionSocketImpl>(
        std::move(io_handle), local_address, remote_address);

    // Transport socket — let the injected factory decide TCP vs TLS.
    auto* client_factory =
        dynamic_cast<network::ClientTransportSocketFactory*>(
            parent_.transport_factory_.get());
    if (!client_factory) {
      return false;
    }
    auto transport_socket = client_factory->createTransportSocket(nullptr);

    // Build the connection. connected=false means connect() will kick
    // off the TCP handshake when called.
    auto connection_impl = std::make_unique<network::ConnectionImpl>(
        dispatcher, std::move(connection_socket), std::move(transport_socket),
        /*connected=*/false);
    connection_impl->addConnectionCallbacks(*this);

    // Install HttpCodecFilter in client mode as a READ filter only.
    // We intentionally do not install it as a write filter: in client
    // mode HttpCodecFilter rewrites outgoing bytes into its own HTTP
    // request shape (hardcoded MCP-oriented headers, SSE handling).
    // HttpAsyncClient formats the request itself so callers keep full
    // control over method, path, and headers.
    codec_filter_ = std::make_shared<filter::HttpCodecFilter>(
        *this, dispatcher, /*is_server=*/false);
    connection_impl->filterManager().addReadFilter(codec_filter_);
    connection_impl->filterManager().initializeReadFilters();

    // The ClientConnection wrapper is what connect() is called on.
    connection_ = std::unique_ptr<network::ClientConnection>(
        static_cast<network::ClientConnection*>(connection_impl.release()));
    connection_->connect();
    return true;
  }

  // network::ConnectionCallbacks
  void onEvent(network::ConnectionEvent event) override {
    switch (event) {
      case network::ConnectionEvent::Connected:
        writeRequestBytes();
        break;
      case network::ConnectionEvent::RemoteClose:
      case network::ConnectionEvent::LocalClose:
        // If we have not yet delivered a response, treat close-before-
        // complete as an error. If the response already fired, a close
        // here is just cleanup and we ignore it.
        if (!completed_) {
          fail("connection closed before response completed");
        }
        break;
      default:
        break;
    }
  }
  void onAboveWriteBufferHighWatermark() override {}
  void onBelowWriteBufferLowWatermark() override {}

  // filter::HttpCodecFilter::MessageCallbacks
  void onHeaders(const std::map<std::string, std::string>& headers,
                 bool /*keep_alive*/) override {
    for (const auto& kv : headers) {
      if (kv.first == ":status") {
        try {
          response_.status_code = std::stoi(kv.second);
        } catch (...) {
          response_.status_code = 0;
        }
      } else if (kv.first == "status") {
        response_.status_text = kv.second;
      } else {
        response_.headers[kv.first] = kv.second;
      }
    }
    if (response_.status_text.empty()) {
      response_.status_text = statusTextForCode(response_.status_code);
    }
  }

  void onBody(const std::string& data, bool end_stream) override {
    // HttpCodecFilter in client mode emits each body chunk twice: once
    // inline as it arrives (end_stream=false) and once again with the
    // fully accumulated body at message-complete (end_stream=true). We
    // only care about the final complete body, so take the end_stream
    // delivery and overwrite whatever streamed in.
    if (end_stream) {
      response_.body = data;
    }
  }

  void onMessageComplete() override {
    if (completed_) {
      return;
    }
    completed_ = true;
    auto cb = std::move(on_response_);
    if (cb) {
      cb(std::move(response_));
    }
    // We are still inside HttpCodecFilter::dispatch on the stack: the
    // parser is in the middle of parsing and will try to drain the
    // input buffer by the number of bytes it consumed once we return.
    // Closing the connection here would flush that same buffer and
    // make the drain overshoot. Defer close + teardown past the
    // current callback frame so the parser can unwind cleanly first.
    parent_.finishRequest(this);
  }

  void onError(const std::string& error) override {
    fail("codec error: " + error);
  }

 private:
  void fail(const std::string& message) {
    if (completed_) {
      return;
    }
    completed_ = true;
    auto cb = std::move(on_error_);
    if (cb) {
      cb(message);
    }
    parent_.finishRequest(this);
  }

  void writeRequestBytes() {
    std::ostringstream req;
    req << request_.method << " " << url_.path << " HTTP/1.1\r\n";
    req << "Host: " << url_.host << ":" << url_.port << "\r\n";
    bool has_content_length = false;
    bool has_connection = false;
    for (const auto& h : request_.headers) {
      req << h.first << ": " << h.second << "\r\n";
      if (h.first == "Content-Length" || h.first == "content-length") {
        has_content_length = true;
      } else if (h.first == "Connection" || h.first == "connection") {
        has_connection = true;
      }
    }
    if (!has_content_length) {
      req << "Content-Length: " << request_.body.size() << "\r\n";
    }
    if (!has_connection) {
      // Default to close — one-shot by design. Callers who want to
      // keep connections open can pass Connection: keep-alive.
      req << "Connection: close\r\n";
    }
    req << "\r\n" << request_.body;

    const std::string bytes = req.str();
    OwnedBuffer buffer;
    buffer.add(bytes.c_str(), bytes.length());
    connection_->write(buffer, /*end_stream=*/false);
  }

  HttpAsyncClient& parent_;
  HttpRequest request_;
  ParsedUrl url_;
  HttpResponseCallback on_response_;
  HttpErrorCallback on_error_;
  std::unique_ptr<network::ClientConnection> connection_;
  std::shared_ptr<filter::HttpCodecFilter> codec_filter_;
  HttpResponse response_;
  bool completed_{false};
};

// HttpAsyncClient implementation

HttpAsyncClient::HttpAsyncClient(
    event::Dispatcher& dispatcher,
    network::SocketInterface& socket_interface,
    std::unique_ptr<network::TransportSocketFactoryBase> transport_factory)
    : dispatcher_(dispatcher),
      socket_interface_(socket_interface),
      transport_factory_(std::move(transport_factory)) {}

HttpAsyncClient::~HttpAsyncClient() {
  // Any requests still in flight at destruction are dropped. We don't
  // fire on_error_ because destruction is observable to the owner —
  // they know their callbacks won't be invoked after they destroy the
  // client. Contexts destruct in-place via map clearing.
  active_requests_.clear();
}

bool HttpAsyncClient::send(const HttpRequest& request,
                           HttpResponseCallback on_response,
                           HttpErrorCallback on_error) {
  assert(dispatcher_.isThreadSafe() &&
         "HttpAsyncClient::send must be called from the dispatcher thread");

  ParsedUrl url;
  if (!parseUrl(request.url, url)) {
    return false;
  }

  auto ctx = std::make_unique<RequestContext>(*this, request, std::move(url),
                                              std::move(on_response),
                                              std::move(on_error));
  auto* raw = ctx.get();
  active_requests_.emplace(raw, std::move(ctx));
  if (!raw->start()) {
    active_requests_.erase(raw);
    return false;
  }
  return true;
}

void HttpAsyncClient::finishRequest(RequestContext* ctx) {
  auto it = active_requests_.find(ctx);
  if (it == active_requests_.end()) {
    return;
  }
  std::unique_ptr<RequestContext> owned = std::move(it->second);
  active_requests_.erase(it);
  // Defer destruction past the current callback frame so we don't
  // unwind the connection and filter from within their own callback.
  dispatcher_.deferredDelete(std::move(owned));
}

}  // namespace http
}  // namespace mcp
