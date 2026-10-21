// Copyright 2025 Gopher Security, Inc.
// SPDX-License-Identifier: Apache-2.0

/**
 * Origin policy, CORS header generation, and the filter that applies them.
 * See the header for the contract.
 */

#include "mcp/filter/http_security_filter.h"

#include <algorithm>
#include <cctype>

#include "mcp/buffer.h"
#include "mcp/http/http_parser.h"
#include "mcp/http/response_writer.h"
#include "mcp/json/json_bridge.h"
#include "mcp/logging/log_macros.h"

namespace mcp {
namespace filter {

namespace {

// Match the HTTP codec's buffered-body bound, including streaming codec users.
constexpr size_t kMaxBufferedRequestBody = 16 * 1024 * 1024;

std::string lowered(const std::string& text) {
  std::string out;
  out.reserve(text.size());
  for (char c : text) {
    out.push_back(
        static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
  }
  return out;
}

/**
 * Split an origin into its scheme and its host, port included.
 *
 * An origin is only ever "scheme://host[:port]" — no path, no query, no
 * credentials. Anything else is not one, including the literal "null" a
 * sandboxed frame sends, and gets no say in what the default set matches.
 */
bool parseOrigin(const std::string& origin,
                 std::string* scheme,
                 std::string* host_and_port) {
  const size_t mark = origin.find("://");
  if (mark == std::string::npos || mark == 0) {
    return false;
  }
  const std::string rest = origin.substr(mark + 3);
  if (rest.empty() || rest.find_first_of("/?#@") != std::string::npos) {
    return false;
  }
  *scheme = lowered(origin.substr(0, mark));
  *host_and_port = lowered(rest);
  return true;
}

/** Split "host:port" — or "[::1]:port" — into the two parts. */
void splitPort(const std::string& host_and_port,
               std::string* host,
               std::string* port) {
  // Only a colon after the closing bracket separates a port; the ones
  // inside an IPv6 literal belong to the address.
  const size_t search_from = host_and_port.empty() || host_and_port[0] != '['
                                 ? 0
                                 : host_and_port.find(']');
  if (search_from == std::string::npos) {
    *host = host_and_port;
    port->clear();
    return;
  }
  const size_t colon = host_and_port.find(':', search_from);
  if (colon == std::string::npos) {
    *host = host_and_port;
    port->clear();
    return;
  }
  *host = host_and_port.substr(0, colon);
  *port = host_and_port.substr(colon + 1);
}

bool isAllDigits(const std::string& text) {
  return !text.empty() && std::all_of(text.begin(), text.end(), [](char c) {
    return std::isdigit(static_cast<unsigned char>(c)) != 0;
  });
}

/**
 * The set that applies when none is configured: this machine, over http or
 * https, on whatever port the page happens to be served from.
 */
bool matchesLocalDefault(const std::string& origin) {
  std::string scheme;
  std::string host_and_port;
  if (!parseOrigin(origin, &scheme, &host_and_port)) {
    return false;
  }
  if (scheme != "http" && scheme != "https") {
    return false;
  }

  std::string host;
  std::string port;
  splitPort(host_and_port, &host, &port);
  if (!port.empty() && !isAllDigits(port)) {
    return false;
  }
  return host == "localhost" || host == "127.0.0.1" || host == "[::1]";
}

/** Append names not already present, comparing without regard to case. */
void appendUnique(const std::vector<std::string>& names,
                  std::vector<std::string>* into) {
  for (const auto& name : names) {
    const std::string key = lowered(name);
    bool seen = false;
    for (const auto& existing : *into) {
      if (lowered(existing) == key) {
        seen = true;
        break;
      }
    }
    if (!seen) {
      into->push_back(name);
    }
  }
}

std::string joinWithCommas(const std::vector<std::string>& names) {
  std::string out;
  for (const auto& name : names) {
    if (!out.empty()) {
      out += ", ";
    }
    out += name;
  }
  return out;
}

/**
 * Collect designated parameter names from a schema's properties, and from
 * the properties of any object among them.
 */
void collectParamNames(const json::JsonValue& schema,
                       std::vector<std::string>* into) {
  if (!schema.isObject() || !schema.contains("properties")) {
    return;
  }
  const json::JsonValue& properties = schema["properties"];
  if (!properties.isObject()) {
    return;
  }

  for (const auto& name : properties.keys()) {
    const json::JsonValue& property = properties[name];
    if (!property.isObject()) {
      continue;
    }
    if (property.contains("x-mcp-header")) {
      const json::JsonValue& designation = property["x-mcp-header"];
      if (designation.isString()) {
        const std::string given = designation.getString();
        into->push_back("Mcp-Param-" + (given.empty() ? name : given));
      } else if (!designation.isBoolean() || designation.getBool()) {
        into->push_back("Mcp-Param-" + name);
      }
    }
    collectParamNames(property, into);
  }
}

// Everything the transport itself can send. A wildcard is not usable here
// once credentials are allowed, so the list is spelled out and grows only
// by what a tool designates.
const char* const kFixedAllowedHeaders[] = {
    "Content-Type",         "Accept",        "Authorization", "Mcp-Session-Id",
    "MCP-Protocol-Version", "Last-Event-ID", "Mcp-Method",    "Mcp-Name"};

/**
 * A JSON-RPC error with no id.
 *
 * A request refused on its headers alone was never read, so there is no id
 * to quote back — and jsonrpc::Response cannot express one, its id having
 * no null alternative.
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

void HttpSecurityPolicy::setAllowedOrigins(
    const std::vector<std::string>& origins) {
  allowed_origins_ = origins;
}

void HttpSecurityPolicy::setExtraAllowedHeaders(
    std::function<std::vector<std::string>()> source) {
  extra_headers_ = std::move(source);
}

bool HttpSecurityPolicy::originAllowed(const std::string& origin) const {
  // No origin means no browser: the caller reached the port directly and
  // there is no cross-site question to answer.
  if (origin.empty()) {
    return true;
  }

  if (allowed_origins_.empty()) {
    return matchesLocalDefault(origin);
  }

  const std::string candidate = lowered(origin);
  for (const auto& allowed : allowed_origins_) {
    if (allowed == "*" || lowered(allowed) == candidate) {
      return true;
    }
  }
  return false;
}

std::map<std::string, std::string> HttpSecurityPolicy::responseHeaders(
    const RequestSecurity& security) const {
  std::map<std::string, std::string> headers;
  if (security.origin.empty()) {
    return headers;
  }

  headers["Access-Control-Allow-Origin"] = security.origin;
  // The answer depends on who asked, so a shared cache must not hand one
  // origin's answer to another.
  headers["Vary"] = "Origin";
  // Without this a browser cannot read the session id off the response at
  // all, which leaves it unable to continue the session it was just given.
  headers["Access-Control-Expose-Headers"] = "Mcp-Session-Id";
  return headers;
}

std::map<std::string, std::string> HttpSecurityPolicy::preflightHeaders(
    const RequestSecurity& security) const {
  std::map<std::string, std::string> headers = responseHeaders(security);
  if (headers.empty()) {
    return headers;
  }

  headers["Access-Control-Allow-Methods"] = "POST, GET, DELETE, OPTIONS";
  headers["Access-Control-Max-Age"] = "86400";

  std::vector<std::string> allowed;
  for (const char* name : kFixedAllowedHeaders) {
    allowed.push_back(name);
  }
  if (extra_headers_) {
    appendUnique(extra_headers_(), &allowed);
  }
  headers["Access-Control-Allow-Headers"] = joinWithCommas(allowed);
  return headers;
}

std::vector<std::string> HttpSecurityPolicy::paramHeadersFor(const Tool& tool) {
  std::vector<std::string> names;
  if (tool.inputSchema.has_value()) {
    collectParamNames(tool.inputSchema.value(), &names);
  }
  return names;
}

// ===== RequestHeadersView =====

std::string RequestHeadersView::get(const std::string& name) const {
  const std::string wanted = lowered(name);
  for (const auto& entry : headers_) {
    if (lowered(entry.first) == wanted) {
      return entry.second;
    }
  }
  return std::string();
}

bool RequestHeadersView::has(const std::string& name) const {
  const std::string wanted = lowered(name);
  for (const auto& entry : headers_) {
    if (lowered(entry.first) == wanted) {
      return true;
    }
  }
  return false;
}

// ===== AuthResult =====

AuthResult AuthResult::allow(const std::string& principal) {
  AuthResult result;
  result.allowed = true;
  result.principal = principal;
  return result;
}

AuthResult AuthResult::deny(int status_code, const std::string& reason) {
  AuthResult result;
  result.allowed = false;
  result.status_code = status_code;
  result.reason = reason;
  return result;
}

// ===== HttpSecurityFilter =====

HttpSecurityFilter::HttpSecurityFilter(HttpCodecFilter::MessageCallbacks& next,
                                       const HttpSecurityPolicy& policy,
                                       RequestSecurity& security,
                                       Host& host)
    : next_(next), policy_(policy), security_(security), host_(host) {}

HttpSecurityFilter::~HttpSecurityFilter() = default;

void HttpSecurityFilter::setAuthCallback(AuthCallback callback) {
  auth_ = std::move(callback);
  admission_current_ = nullptr;
}

void HttpSecurityFilter::clearRequestState() {
  security_.request_authorization.clear();
  request_headers_.clear();
  request_body_.clear();
  admission_current_ = nullptr;
}

void HttpSecurityFilter::onHeaders(
    const std::map<std::string, std::string>& headers, bool keep_alive) {
  refused_ = false;
  clearRequestState();
  keep_alive_ = keep_alive;

  const RequestHeadersView view(headers);
  const std::string origin = view.get("origin");

  if (!policy_.originAllowed(origin)) {
    // Nothing about this request is recorded: an answer that reflected the
    // origin back would tell the browser the request had been allowed.
    security_ = RequestSecurity();
    security_.allowed = false;
    GOPHER_LOG_WARN("request refused: origin not permitted: {}", origin);
    refuse(static_cast<int>(http::HttpStatusCode::Forbidden),
           jsonrpc::INVALID_REQUEST, "Forbidden: origin not permitted",
           /*close_connection=*/true);
    return;
  }

  security_ = RequestSecurity();
  security_.origin = origin;
  security_.request_authorization = view.get("authorization");
  request_headers_ = headers;

  if (!authenticate()) {
    return;
  }

  // A route may answer immediately from its headers. Do not expose them
  // until the completed request has passed its final admission check.
}

bool HttpSecurityFilter::authenticate() {
  security_.principal.clear();
  admission_current_ = nullptr;
  if (auth_) {
    const AuthResult decision = auth_(RequestHeadersView(request_headers_));
    if (!decision.allowed) {
      // The origin was fine, so the answer still carries its CORS headers —
      // otherwise a browser cannot read why it was turned away, and a
      // client that could have retried with credentials never learns to.
      security_.allowed = false;
      GOPHER_LOG_DEBUG("request refused by auth hook: {}", decision.reason);
      refuse(decision.status_code, jsonrpc::INVALID_REQUEST,
             decision.reason.empty() ? "Unauthorized" : decision.reason,
             /*close_connection=*/false, decision.response_headers);
      return false;
    }
    security_.principal = decision.principal;
    admission_current_ = decision.still_current;
  }
  return true;
}

void HttpSecurityFilter::onBody(const std::string& data, bool end_stream) {
  if (refused_) {
    return;
  }
  if (data.size() > kMaxBufferedRequestBody - request_body_.size()) {
    security_.allowed = false;
    refuse(413, jsonrpc::INVALID_REQUEST, "HTTP request body too large",
           /*close_connection=*/true);
    request_body_.clear();
    return;
  }
  request_body_.append(data);
}

void HttpSecurityFilter::onMessageComplete() {
  if (refused_) {
    return;
  }
  struct Cleanup {
    HttpSecurityFilter& filter;
    ~Cleanup() { filter.clearRequestState(); }
  } cleanup{*this};
  if ((!admission_current_ || !admission_current_()) && !authenticate()) {
    return;
  }
  next_.onHeaders(request_headers_, keep_alive_);
  if (!request_body_.empty()) {
    next_.onBody(request_body_, /*end_stream=*/true);
  }
  next_.onMessageComplete();
}

void HttpSecurityFilter::onError(const std::string& error) {
  // A request that failed to parse will never complete, so whatever was
  // decided about it no longer applies to what arrives next.
  refused_ = false;
  clearRequestState();
  next_.onError(error);
}

void HttpSecurityFilter::refuse(
    int status_code,
    int code,
    const std::string& message,
    bool close_connection,
    const std::map<std::string, std::string>& extra_headers) {
  // Set before anything is written: the body of the request being refused
  // is still arriving, and it must not reach the layer behind this one
  // even if the write below fails.
  refused_ = true;
  clearRequestState();

  http::ResponseWriter::Options options;
  options.http_1_1 = host_.requestIsHttp11();
  options.keep_alive = !close_connection;

  http::ResponseWriter::HeaderList headers;
  headers.emplace_back("Content-Type", "application/json");
  for (const auto& header : policy_.responseHeaders(security_)) {
    headers.emplace_back(header.first, header.second);
  }
  for (const auto& header : extra_headers) {
    headers.emplace_back(header.first, header.second);
  }

  http::ResponseWriter writer(options);
  writer.startUnary(status_code, headers, idLessError(code, message));

  OwnedBuffer response;
  writer.drainTo(response);
  host_.writeResponse(response, close_connection);
}

}  // namespace filter
}  // namespace mcp
