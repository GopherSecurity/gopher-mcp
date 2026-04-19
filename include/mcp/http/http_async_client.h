#ifndef MCP_HTTP_HTTP_ASYNC_CLIENT_H
#define MCP_HTTP_HTTP_ASYNC_CLIENT_H

#include <functional>
#include <map>
#include <memory>
#include <string>

#include "mcp/event/event_loop.h"
#include "mcp/network/socket_interface.h"
#include "mcp/network/transport_socket.h"

namespace mcp {
namespace http {

// Request handed to HttpAsyncClient::send.
// The url is a full absolute URL (scheme://host[:port]/path). host must be
// a dotted-quad IPv4 literal — hostname resolution is not performed here
// so that callers running on the dispatcher thread never block on DNS.
struct HttpRequest {
  std::string method{"POST"};
  std::string url;
  std::map<std::string, std::string> headers;
  std::string body;
};

// Response delivered to the HttpResponseCallback on success.
struct HttpResponse {
  int status_code{0};
  std::string status_text;
  std::map<std::string, std::string> headers;
  std::string body;
};

// Exactly one of these fires per send().
using HttpResponseCallback = std::function<void(HttpResponse)>;
using HttpErrorCallback = std::function<void(const std::string& error)>;

/**
 * HttpAsyncClient — minimal fire-and-forget HTTP/1.1 client.
 *
 * Each send() creates an isolated request context: its own client
 * connection, its own HttpCodecFilter (client mode), and its own
 * callbacks. Request contexts are owned by the client until they
 * complete, then handed to Dispatcher::deferredDelete so teardown
 * runs past the current callback frame (Envoy-style lifetime, which
 * avoids destroying a connection from inside its own callback).
 *
 * All public methods must be invoked from the dispatcher thread —
 * this matches the project-wide convention that mutating network
 * objects off-thread is undefined.
 */
class HttpAsyncClient {
 public:
  HttpAsyncClient(
      event::Dispatcher& dispatcher,
      network::SocketInterface& socket_interface,
      std::unique_ptr<network::TransportSocketFactoryBase> transport_factory);
  ~HttpAsyncClient();

  HttpAsyncClient(const HttpAsyncClient&) = delete;
  HttpAsyncClient& operator=(const HttpAsyncClient&) = delete;

  /**
   * Send an HTTP request. Exactly one of on_response or on_error will
   * be invoked (on the dispatcher thread) before the request context
   * is torn down. Returns false if the URL could not be parsed; in
   * that case no callback fires.
   */
  bool send(const HttpRequest& request,
            HttpResponseCallback on_response,
            HttpErrorCallback on_error);

 private:
  class RequestContext;

  // Called by RequestContext when it completes (success or failure).
  // Extracts the unique_ptr from active_requests_ and hands it to
  // Dispatcher::deferredDelete so the connection and filter tear down
  // after the current callback returns.
  void finishRequest(RequestContext* ctx);

  event::Dispatcher& dispatcher_;
  network::SocketInterface& socket_interface_;
  std::unique_ptr<network::TransportSocketFactoryBase> transport_factory_;
  std::map<RequestContext*, std::unique_ptr<RequestContext>> active_requests_;
};

using HttpAsyncClientPtr = std::unique_ptr<HttpAsyncClient>;

}  // namespace http
}  // namespace mcp

#endif  // MCP_HTTP_HTTP_ASYNC_CLIENT_H
