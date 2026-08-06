#ifndef MCP_FILTER_HTTP_SECURITY_FILTER_H
#define MCP_FILTER_HTTP_SECURITY_FILTER_H

#include <functional>
#include <map>
#include <string>
#include <vector>

#include "mcp/buffer.h"
#include "mcp/filter/http_codec_filter.h"
#include "mcp/types.h"

namespace mcp {
namespace filter {

/**
 * What the security layer decided about the request being served.
 *
 * One per connection, rewritten as each request's headers arrive. The
 * response sites read it rather than re-reading the request, because by the
 * time a response is framed the request headers are several layers away and
 * some answers are written from places that never saw them.
 */
struct RequestSecurity {
  /** The Origin exactly as sent. Empty when the request carried none. */
  std::string origin;

  /** False once a request has been refused; nothing further is served. */
  bool allowed{true};

  /** Who the request is from, as resolved by the auth hook. */
  std::string principal;
};

/**
 * Which origins may reach this server, and what CORS headers its answers
 * carry.
 *
 * A browser will happily let any page a user visits POST to a server on
 * their own machine; the Origin header is the only thing that says which
 * page it was. Checking it is what stops a hostile site from driving a
 * local MCP server through the user's browser.
 *
 * A request with no Origin at all is allowed — that is every non-browser
 * client, which had to reach the port on its own — and gets no CORS
 * headers back, because there is no browser to read them and nothing to
 * reflect.
 */
class HttpSecurityPolicy {
 public:
  /**
   * Origins permitted to reach this server, each written as a browser
   * sends one: "scheme://host" with an optional port.
   *
   * Empty applies the default set — localhost and the loopback addresses,
   * over http or https, on any port — which is what a locally installed
   * server is reachable at and nothing else. A single "*" entry allows
   * any origin; the answer still reflects the origin rather than
   * returning a wildcard, so the header stays usable if credentials are
   * ever allowed.
   */
  void setAllowedOrigins(const std::vector<std::string>& origins);

  /**
   * Extra request header names to advertise in preflight, asked for
   * whenever a preflight is answered rather than captured once: the set
   * follows the registered tools, and tools can be registered at any
   * point in a server's life.
   */
  void setExtraAllowedHeaders(std::function<std::vector<std::string>()> source);

  /** Whether a request bearing this origin may be served. */
  bool originAllowed(const std::string& origin) const;

  /**
   * CORS headers for an ordinary answer. Empty when the request carried
   * no origin.
   */
  std::map<std::string, std::string> responseHeaders(
      const RequestSecurity& security) const;

  /**
   * CORS headers for a preflight answer: what an actual request may use.
   * Empty when the request carried no origin.
   */
  std::map<std::string, std::string> preflightHeaders(
      const RequestSecurity& security) const;

  /**
   * The request header names a tool designates for its parameters.
   *
   * A property of a tool's input schema marked with "x-mcp-header" may be
   * sent as "Mcp-Param-{name}" instead of in the body. The name is the
   * annotation's own value when it is a string and the property's name
   * otherwise; nested properties contribute their own name, not a path,
   * which is why the names have to be unique across the whole schema.
   *
   * Enumerated here so preflight can name them. Whether a designation is
   * legal in the first place is a separate question from whether a
   * browser is allowed to send it.
   */
  static std::vector<std::string> paramHeadersFor(const Tool& tool);

 private:
  std::vector<std::string> allowed_origins_;
  std::function<std::vector<std::string>()> extra_headers_;
};

/** Request headers, readable without knowing how the codec cased them. */
class RequestHeadersView {
 public:
  explicit RequestHeadersView(const std::map<std::string, std::string>& headers)
      : headers_(headers) {}

  /** The named header's value, or an empty string when it was not sent. */
  std::string get(const std::string& name) const;
  bool has(const std::string& name) const;

  const std::map<std::string, std::string>& all() const { return headers_; }

 private:
  const std::map<std::string, std::string>& headers_;
};

/** What an auth hook decided about one request. */
struct AuthResult {
  /** False refuses the request with status_code and reason. */
  bool allowed{true};

  /** Who the request is from. Only meaningful when allowed. */
  std::string principal;

  int status_code{403};
  std::string reason;

  static AuthResult allow(const std::string& principal);
  static AuthResult deny(int status_code, const std::string& reason);
};

/**
 * Resolves who a request is from.
 *
 * A hook rather than an implementation: bearer tokens, OAuth and mutual TLS
 * all decide this differently, and none of them belongs in a transport.
 */
using AuthCallback = std::function<AuthResult(const RequestHeadersView&)>;

/**
 * Everything a connection needs to decide who it serves.
 *
 * Carried as one value because it travels together: a filter chain is
 * built from a configuration and a pair of hooks, and threading three
 * more arguments through a constructor that already takes a dozen would
 * make the call sites unreadable.
 */
struct HttpSecurityOptions {
  /** Empty applies the policy's default set. */
  std::vector<std::string> allowed_origins;

  /** Absent serves everyone as "anonymous". */
  AuthCallback auth;

  /**
   * Extra request header names to advertise in preflight, asked for on
   * every preflight because the set follows the registered tools.
   */
  std::function<std::vector<std::string>()> extra_allowed_headers;
};

/**
 * Refuses requests that should not be served, before anything can act on
 * them.
 *
 * Sits directly behind the HTTP codec, so every route — the MCP endpoint,
 * the older event stream and callback paths, and the plain HTTP handlers —
 * passes through it. A refused request is answered here and its body never
 * reaches a layer that could act on it, which is the only arrangement where
 * a rejection is worth anything: a request whose headers were refused but
 * whose body still ran would be both answered twice and acted upon once.
 *
 * Dispatcher-thread confined, like every filter.
 */
class HttpSecurityFilter : public HttpCodecFilter::MessageCallbacks {
 public:
  /**
   * What the filter needs from the connection it is serving.
   *
   * A seam rather than a connection pointer: what this filter does is
   * decide, and a test that had to stand up a connection to watch it
   * decide would be testing the connection too.
   */
  class Host {
   public:
    virtual ~Host() = default;

    /**
     * Send an already-framed answer.
     *
     * @param close_connection End the connection once the bytes are out.
     */
    virtual void writeResponse(Buffer& data, bool close_connection) = 0;

    /** Whether the request being answered came in on HTTP/1.1. */
    virtual bool requestIsHttp11() const = 0;
  };

  /**
   * @param next     Where accepted requests go.
   * @param policy   Which origins are served; outlives this filter.
   * @param security Where this filter records what it decided, read later
   *                 by whoever frames the answer.
   */
  HttpSecurityFilter(HttpCodecFilter::MessageCallbacks& next,
                     const HttpSecurityPolicy& policy,
                     RequestSecurity& security,
                     Host& host);
  ~HttpSecurityFilter() override;

  /** Replaces the default, which serves everyone as "anonymous". */
  void setAuthCallback(AuthCallback callback);

  // ===== HttpCodecFilter::MessageCallbacks =====

  void onHeaders(const std::map<std::string, std::string>& headers,
                 bool keep_alive) override;
  void onBody(const std::string& data, bool end_stream) override;
  void onMessageComplete() override;
  void onError(const std::string& error) override;

 private:
  /**
   * Answer the request being parsed and stop it going any further.
   *
   * @param close_connection True when the connection should not be reused:
   *                         a request from an origin this server does not
   *                         serve is not a conversation worth continuing.
   */
  void refuse(int status_code,
              int code,
              const std::string& message,
              bool close_connection);

  HttpCodecFilter::MessageCallbacks& next_;
  const HttpSecurityPolicy& policy_;
  RequestSecurity& security_;
  Host& host_;
  AuthCallback auth_;

  // Set when the current request has already been answered here. Cleared
  // when the next request's headers arrive.
  bool refused_{false};
};

}  // namespace filter
}  // namespace mcp

#endif  // MCP_FILTER_HTTP_SECURITY_FILTER_H
