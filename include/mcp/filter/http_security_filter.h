#ifndef MCP_FILTER_HTTP_SECURITY_FILTER_H
#define MCP_FILTER_HTTP_SECURITY_FILTER_H

#include <functional>
#include <map>
#include <string>
#include <vector>

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

}  // namespace filter
}  // namespace mcp

#endif  // MCP_FILTER_HTTP_SECURITY_FILTER_H
