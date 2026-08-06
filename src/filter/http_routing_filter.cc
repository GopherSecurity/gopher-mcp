/**
 * HTTP Routing Filter Implementation
 *
 * Provides endpoint routing for HTTP requests using HttpCodecFilter
 * for proper HTTP protocol parsing. Uses MCP Buffer abstraction throughout.
 */

#include "mcp/filter/http_routing_filter.h"

#include <cctype>
#include <sstream>

#include "mcp/http/http_parser.h"
#include "mcp/logging/log_macros.h"
#include "mcp/network/connection.h"

namespace mcp {
namespace filter {
namespace {

// Machine-readable error slug for a status code, e.g. "method_not_allowed".
std::string statusSlug(int status_code) {
  std::string slug = http::httpStatusCodeToString(
      static_cast<http::HttpStatusCode>(status_code));
  for (auto& c : slug) {
    if (c == ' ') {
      c = '_';
    } else {
      c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
  }
  return slug;
}

}  // namespace

HttpRoutingFilter::HttpRoutingFilter(
    HttpCodecFilter::MessageCallbacks* next_callbacks,
    HttpCodecFilter::MessageEncoder* encoder,
    bool is_server)
    : next_callbacks_(next_callbacks),
      encoder_(encoder),
      is_server_(is_server) {
  // Set up default handler that passes through to next layer
  default_handler_ = [](const RequestContext&) {
    // Return special status code 0 to indicate pass-through
    Response resp;
    resp.status_code = 0;  // Signal to pass through
    return resp;
  };
}

HttpRoutingFilter::RouteTarget HttpRoutingFilter::RouteTarget::handlerRoute(
    HandlerFunc handler) {
  RouteTarget target;
  target.kind = Kind::Handler;
  target.handler = std::move(handler);
  return target;
}

HttpRoutingFilter::RouteTarget HttpRoutingFilter::RouteTarget::passThrough() {
  RouteTarget target;
  target.kind = Kind::PassThrough;
  return target;
}

HttpRoutingFilter::RouteTarget HttpRoutingFilter::RouteTarget::reject(
    int status_code, const std::string& allow_header) {
  RouteTarget target;
  target.kind = Kind::Reject;
  target.status_code = status_code;
  target.allow_header = allow_header;
  return target;
}

void HttpRoutingFilter::addRoute(const std::string& method,
                                 const std::string& path,
                                 RouteTarget target) {
  routes_[buildRouteKey(method, path)] = std::move(target);
}

void HttpRoutingFilter::registerHandler(const std::string& method,
                                        const std::string& path,
                                        HandlerFunc handler) {
  addRoute(method, path, RouteTarget::handlerRoute(std::move(handler)));
}

std::string HttpRoutingFilter::allowedMethodsFor(
    const std::string& path) const {
  // Route keys are "METHOD path" in a sorted map, so methods come out in
  // a stable order regardless of the order routes were added.
  std::string allow;
  for (const auto& entry : routes_) {
    const std::string& key = entry.first;
    const size_t separator = key.find(' ');
    if (separator == std::string::npos) {
      continue;
    }
    if (key.compare(separator + 1, std::string::npos, path) != 0) {
      continue;
    }
    if (!entry.second.servesRequests()) {
      continue;
    }
    if (!allow.empty()) {
      allow += ", ";
    }
    allow.append(key, 0, separator);
  }
  return allow;
}

void HttpRoutingFilter::registerDefaultHandler(HandlerFunc handler) {
  default_handler_ = handler;
}

// Removed onData, onNewConnection, onWrite, and initialize callbacks methods
// as this filter no longer implements network::Filter interface

// HttpCodecFilter::MessageCallbacks implementation
void HttpRoutingFilter::onHeaders(
    const std::map<std::string, std::string>& headers, bool keep_alive) {
  GOPHER_LOG_DEBUG(
      "HttpRoutingFilter::onHeaders called with {} headers, is_server={}, "
      "keep_alive={}",
      headers.size(), is_server_, keep_alive);

  // In client mode, we're receiving responses, not requests - skip routing
  if (!is_server_) {
    GOPHER_LOG_DEBUG(
        "HttpRoutingFilter: client mode, passing through response");
    if (next_callbacks_) {
      next_callbacks_->onHeaders(headers, keep_alive);
    }
    return;
  }

  // Server mode: route incoming requests.
  //
  // Reset the per-request state first. These flags are otherwise only
  // cleared once a request completes, so a request that never completes
  // (parse error, reset mid-body) would leak suppression into the next
  // request on a keep-alive connection and silently drop its body.
  suppress_current_request_ = false;
  pending_post_request_ = false;
  accumulated_body_.clear();

  std::string method = extractMethod(headers);
  std::string path =
      extractPath(headers);  // Path without query string for routing

  // Get full URL including query string for handler context
  std::string full_url = path;
  auto url_it = headers.find("url");
  if (url_it != headers.end()) {
    full_url = url_it->second;
  } else {
    auto path_it = headers.find(":path");
    if (path_it != headers.end()) {
      full_url = path_it->second;
    }
  }

  GOPHER_LOG_DEBUG("HttpRoutingFilter: method={} path={}", method, path);

  // Check whether the table has a route for this endpoint
  std::string key = buildRouteKey(method, path);
  auto route_it = routes_.find(key);

  if (route_it != routes_.end()) {
    const RouteTarget& target = route_it->second;

    if (target.kind == RouteTarget::Kind::PassThrough) {
      // The table is authoritative for this route, so the default
      // handler is not consulted. Leave the pending-body flags clear so
      // the body keeps flowing to the next layer.
      if (next_callbacks_) {
        next_callbacks_->onHeaders(headers, keep_alive);
      }
      return;
    }

    if (target.kind == RouteTarget::Kind::Reject) {
      // Answered from the headers alone, whatever the method. Any body
      // is parsed by the codec and dropped below, which keeps the
      // connection framed for the next request.
      suppress_current_request_ = true;
      sendResponse(buildRejectResponse(target, path));
      return;
    }

    RequestContext ctx;
    ctx.method = method;
    ctx.path = full_url;  // Full URL with query string for handler
    ctx.headers = headers;
    ctx.keep_alive = keep_alive;
    // Note: body not available yet in onHeaders

    // For POST/PUT requests, defer handler until we have the body
    if (method == "POST" || method == "PUT" || method == "PATCH") {
      pending_post_request_ = true;
      pending_context_ = ctx;
      pending_handler_ = target.handler;
      accumulated_body_.clear();
      return;  // Wait for body
    }

    // For GET/OPTIONS etc, execute immediately
    Response resp = target.handler(ctx);
    if (resp.status_code != 0) {
      // Handler wants to handle this - send response immediately
      // This is appropriate for endpoints that don't need the body
      suppress_current_request_ = true;
      sendResponse(resp);
      return;  // Don't forward to next layer
    }
  }

  // No registered handler matched, or the matched handler explicitly returned
  // status 0. Give the default handler a chance to answer before forwarding to
  // the protocol layer. Transport paths can still opt into pass-through by
  // returning status 0 from the default handler.
  {
    RequestContext ctx;
    ctx.method = method;
    ctx.path = full_url;
    ctx.headers = headers;
    ctx.keep_alive = keep_alive;
    Response resp = default_handler_(ctx);
    if (resp.status_code != 0) {
      suppress_current_request_ = true;
      sendResponse(resp);
      return;  // Handled or rejected; do not forward to next layer.
    }
  }

  // Default handler signalled pass-through - forward to next layer
  if (next_callbacks_) {
    next_callbacks_->onHeaders(headers, keep_alive);
  }
}

void HttpRoutingFilter::onBody(const std::string& data, bool end_stream) {
  if (suppress_current_request_) {
    return;
  }

  // If we're accumulating body for a POST handler, buffer it
  if (pending_post_request_) {
    accumulated_body_ += data;
    return;  // Don't forward - we'll handle in onMessageComplete
  }

  // Otherwise pass through
  if (next_callbacks_) {
    next_callbacks_->onBody(data, end_stream);
  }
}

void HttpRoutingFilter::onMessageComplete() {
  GOPHER_LOG_DEBUG("HttpRoutingFilter::onMessageComplete called");

  if (suppress_current_request_) {
    suppress_current_request_ = false;
    return;
  }

  // If we have a pending POST request, now we have the complete body
  if (pending_post_request_) {
    pending_context_.body = accumulated_body_;
    Response resp = pending_handler_(pending_context_);
    if (resp.status_code != 0) {
      sendResponse(resp);
    } else {
      // A deferred handler cannot ask for pass-through: the body has
      // already been withheld from the next layer, so the request is
      // dropped and the client never hears back.
      GOPHER_LOG_WARN(
          "HttpRoutingFilter: handler for {} {} requested pass-through after "
          "the body was buffered; request dropped",
          pending_context_.method, pending_context_.path);
    }
    // Reset state
    pending_post_request_ = false;
    accumulated_body_.clear();
    return;
  }

  // Otherwise pass through
  if (next_callbacks_) {
    next_callbacks_->onMessageComplete();
  }
}

void HttpRoutingFilter::onError(const std::string& error) {
  // The failed request will never complete, so drop its state rather
  // than let it apply to whatever arrives next on this connection.
  suppress_current_request_ = false;
  pending_post_request_ = false;
  accumulated_body_.clear();

  // Stateless - always pass through errors
  if (next_callbacks_) {
    next_callbacks_->onError(error);
  }
}

HttpRoutingFilter::Response HttpRoutingFilter::buildRejectResponse(
    const RouteTarget& target, const std::string& path) const {
  Response resp;
  resp.status_code = target.status_code;

  // A 405 always names the methods the endpoint does serve. Deriving it
  // from the table keeps the header correct as routes are added.
  std::string allow = target.allow_header;
  if (allow.empty() &&
      target.status_code ==
          static_cast<int>(http::HttpStatusCode::MethodNotAllowed)) {
    allow = allowedMethodsFor(path);
  }
  if (!allow.empty()) {
    resp.headers["Allow"] = allow;
  }

  resp.headers["content-type"] = "application/json";
  resp.body = "{\"error\":\"" + statusSlug(target.status_code) + "\"}";
  resp.headers["content-length"] = std::to_string(resp.body.length());
  return resp;
}

void HttpRoutingFilter::sendResponse(const Response& response) {
  GOPHER_LOG_DEBUG("HttpRoutingFilter::sendResponse called with status {}",
                   response.status_code);

  // Build complete HTTP response
  std::ostringstream http_response;

  // Status line (use HTTP/1.1 for now)
  http_response << "HTTP/1.1 " << response.status_code << " "
                << http::httpStatusCodeToString(
                       static_cast<http::HttpStatusCode>(response.status_code))
                << "\r\n";

  // Whether a browser may read this answer is not a handler's decision;
  // a handler that set the header itself keeps its value.
  std::map<std::string, std::string> headers =
      response_headers_ ? response_headers_()
                        : std::map<std::string, std::string>();
  for (const auto& header : response.headers) {
    headers[header.first] = header.second;
  }

  for (const auto& header : headers) {
    http_response << header.first << ": " << header.second << "\r\n";
  }

  // End headers
  http_response << "\r\n";

  // Add body if present
  if (!response.body.empty()) {
    http_response << response.body;
  }

  // Send the complete response directly through write callbacks
  if (write_callbacks_) {
    std::string response_str = http_response.str();
    OwnedBuffer response_buffer;
    response_buffer.add(response_str);
    write_callbacks_->connection().write(response_buffer, false);

    GOPHER_LOG_DEBUG("HttpRoutingFilter sent response: {} bytes",
                     response_str.length());
  }
}

std::string HttpRoutingFilter::buildRouteKey(const std::string& method,
                                             const std::string& path) const {
  return method + " " + path;
}

std::string HttpRoutingFilter::extractMethod(
    const std::map<std::string, std::string>& headers) {
  // The HTTP codec filter doesn't directly expose the method in headers
  // We need to get it from the parser through the codec
  // For now, we'll parse it from the URL header which contains the full request
  // line or look for a method header that some parsers add

  // Check for :method pseudo-header (HTTP/2 style)
  auto it = headers.find(":method");
  if (it != headers.end()) {
    return it->second;
  }

  // For HTTP/1.1, we need to extract from the request line
  // The parser stores the method internally but we can infer it
  // from the request context

  // Default to GET if not found
  return "GET";
}

std::string HttpRoutingFilter::extractPath(
    const std::map<std::string, std::string>& headers) {
  std::string full_path;

  // Check for :path pseudo-header (HTTP/2 style)
  auto it = headers.find(":path");
  if (it != headers.end()) {
    full_path = it->second;
  } else {
    // For HTTP/1.1, the codec stores the URL in a "url" header
    it = headers.find("url");
    if (it != headers.end()) {
      full_path = it->second;
    } else {
      // Default to root if not found
      return "/";
    }
  }

  // Strip query string for routing purposes
  size_t query_pos = full_path.find('?');
  if (query_pos != std::string::npos) {
    return full_path.substr(0, query_pos);
  }
  return full_path;
}

// Factory methods
std::shared_ptr<HttpRoutingFilter>
HttpRoutingFilterFactory::createWithHealthCheck() {
  // Note: This needs a dispatcher to be passed in
  // For now, return nullptr as we need to refactor the factory
  return nullptr;
}

std::shared_ptr<HttpRoutingFilter> HttpRoutingFilterFactory::createWithHandlers(
    const std::map<std::string, HttpRoutingFilter::HandlerFunc>& handlers) {
  // Note: This needs a dispatcher to be passed in
  // For now, return nullptr as we need to refactor the factory
  return nullptr;
}

}  // namespace filter
}  // namespace mcp
