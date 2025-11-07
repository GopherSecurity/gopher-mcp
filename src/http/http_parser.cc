#include "mcp/http/http_parser.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <sstream>

#include "mcp/buffer.h"

#if MCP_HAS_LLHTTP
#include "mcp/http/llhttp_parser.h"
#endif

#if MCP_HAS_NGHTTP2
#include "mcp/http/nghttp2_parser.h"
#endif

namespace mcp {
namespace http {

// Note: The specialized factory classes LLHttp10ParserFactory and
// LLHttp11ParserFactory are now declared in llhttp_parser.h to solve the issue
// where parsers need to report their version immediately after creation. The
// HttpParserSelector knows which version it wants, but the generic factory
// interface doesn't pass version information. By creating version-specific
// factories, each parser is created with the correct version hint from the
// start.

namespace {

// Convert string to lowercase
std::string toLower(const std::string& str) {
  std::string result = str;
  std::transform(result.begin(), result.end(), result.begin(), ::tolower);
  return result;
}

}  // namespace

// HttpHeaders implementation
class HttpHeadersImpl : public HttpHeaders {
 public:
  HttpHeadersImpl() = default;
  ~HttpHeadersImpl() override = default;

  void add(const std::string& name, const std::string& value) override {
    std::string lower_name = toLower(name);
    headers_[lower_name].push_back(value);
    ordered_headers_.push_back({name, value});
    byte_size_ += name.size() + value.size() + 4;  // ": " and "\r\n"
  }

  void set(const std::string& name, const std::string& value) override {
    std::string lower_name = toLower(name);

    // Remove existing headers
    auto it = headers_.find(lower_name);
    if (it != headers_.end()) {
      // Calculate size reduction
      for (const auto& v : it->second) {
        byte_size_ -= name.size() + v.size() + 4;
      }
      headers_.erase(it);
    }

    // Remove from ordered list
    ordered_headers_.erase(
        std::remove_if(ordered_headers_.begin(), ordered_headers_.end(),
                       [&lower_name](const auto& pair) {
                         return toLower(pair.first) == lower_name;
                       }),
        ordered_headers_.end());

    // Add new value
    add(name, value);
  }

  void remove(const std::string& name) override {
    std::string lower_name = toLower(name);

    // Remove from map
    auto it = headers_.find(lower_name);
    if (it != headers_.end()) {
      // Calculate size reduction
      for (const auto& v : it->second) {
        byte_size_ -= name.size() + v.size() + 4;
      }
      headers_.erase(it);
    }

    // Remove from ordered list
    ordered_headers_.erase(
        std::remove_if(ordered_headers_.begin(), ordered_headers_.end(),
                       [&lower_name](const auto& pair) {
                         return toLower(pair.first) == lower_name;
                       }),
        ordered_headers_.end());
  }

  optional<std::string> get(const std::string& name) const override {
    std::string lower_name = toLower(name);
    auto it = headers_.find(lower_name);
    if (it != headers_.end() && !it->second.empty()) {
      // Return first value
      return mcp::make_optional(it->second.front());
    }
    return nullopt;
  }

  std::vector<std::string> getAll(const std::string& name) const override {
    std::string lower_name = toLower(name);
    auto it = headers_.find(lower_name);
    if (it != headers_.end()) {
      return it->second;
    }
    return {};
  }

  bool has(const std::string& name) const override {
    std::string lower_name = toLower(name);
    return headers_.find(lower_name) != headers_.end();
  }

  void clear() override {
    headers_.clear();
    ordered_headers_.clear();
    byte_size_ = 0;
  }

  HttpHeaderMap getMap() const override {
    HttpHeaderMap result;
    for (const auto& header : headers_) {
      const auto& name = header.first;
      const auto& values = header.second;
      if (!values.empty()) {
        // Join multiple values with comma
        std::string joined;
        for (size_t i = 0; i < values.size(); ++i) {
          if (i > 0)
            joined += ", ";
          joined += values[i];
        }
        result[name] = joined;
      }
    }
    return result;
  }

  void forEach(std::function<void(const std::string&, const std::string&)> cb)
      const override {
    for (const auto& header : ordered_headers_) {
      cb(header.first, header.second);
    }
  }

  size_t byteSize() const override { return byte_size_; }

 private:
  // Case-insensitive map of headers (lowercase name -> values)
  std::unordered_map<std::string, std::vector<std::string>> headers_;

  // Ordered list of headers as added (preserves case and order)
  std::vector<std::pair<std::string, std::string>> ordered_headers_;

  // Total byte size
  size_t byte_size_{0};
};

// HttpMessage implementation
class HttpMessageImpl : public HttpMessage {
 public:
  HttpMessageImpl(bool is_request)
      : is_request_(is_request),
        method_(HttpMethod::GET),
        status_code_(HttpStatusCode::OK),
        version_(HttpVersion::HTTP_1_1),
        headers_(std::make_unique<HttpHeadersImpl>()),
        body_(createBuffer()),
        chunked_(false),
        has_trailers_(false),
        trailers_(std::make_unique<HttpHeadersImpl>()) {}

  ~HttpMessageImpl() override = default;

  // Request methods
  HttpMethod method() const override { return method_; }
  std::string methodString() const override {
    return httpMethodToString(method_);
  }
  std::string uri() const override { return uri_; }
  std::string path() const override { return path_; }
  std::string query() const override { return query_; }

  // Response methods
  HttpStatusCode statusCode() const override { return status_code_; }
  std::string statusText() const override { return status_text_; }

  // Common methods
  HttpVersion version() const override { return version_; }
  HttpHeaders& headers() override { return *headers_; }
  const HttpHeaders& headers() const override { return *headers_; }
  Buffer& body() override { return *body_; }
  const Buffer& body() const override { return *body_; }
  bool isChunked() const override { return chunked_; }
  bool hasTrailers() const override { return has_trailers_; }
  HttpHeaders& trailers() override { return *trailers_; }
  const HttpHeaders& trailers() const override { return *trailers_; }

  // Setters for parser
  void setMethod(HttpMethod method) { method_ = method; }
  void setUri(const std::string& uri) {
    uri_ = uri;
    // Parse path and query
    size_t query_pos = uri.find('?');
    if (query_pos != std::string::npos) {
      path_ = uri.substr(0, query_pos);
      query_ = uri.substr(query_pos + 1);
    } else {
      path_ = uri;
      query_.clear();
    }
  }
  void setStatusCode(HttpStatusCode code) { status_code_ = code; }
  void setStatusText(const std::string& text) { status_text_ = text; }
  void setVersion(HttpVersion version) { version_ = version; }
  void setChunked(bool chunked) { chunked_ = chunked; }
  void setHasTrailers(bool has) { has_trailers_ = has; }

 private:
  bool is_request_;

  // Request fields
  HttpMethod method_;
  std::string uri_;
  std::string path_;
  std::string query_;

  // Response fields
  HttpStatusCode status_code_;
  std::string status_text_;

  // Common fields
  HttpVersion version_;
  std::unique_ptr<HttpHeaders> headers_;
  std::unique_ptr<Buffer> body_;
  bool chunked_;
  bool has_trailers_;
  std::unique_ptr<HttpHeaders> trailers_;
};

// HttpParserSelector implementation
class HttpParserSelectorImpl : public HttpParserSelector {
 public:
  HttpParserSelectorImpl() = default;
  ~HttpParserSelectorImpl() override = default;

  void registerFactory(const std::vector<HttpVersion>& versions,
                       HttpParserFactorySharedPtr factory) override {
    for (auto version : versions) {
      factories_[version] = factory;
    }
  }

  HttpParserPtr createParser(HttpVersion version,
                             HttpParserType type,
                             HttpParserCallbacks* callbacks) override {
    auto it = factories_.find(version);
    if (it != factories_.end()) {
      return it->second->createParser(type, callbacks);
    }
    return nullptr;
  }

  HttpParserPtr detectAndCreateParser(const char* data,
                                      size_t length,
                                      HttpParserType type,
                                      HttpParserCallbacks* callbacks) override {
    // Check for HTTP/2 connection preface
    // "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n"
    const char* http2_preface = "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";
    size_t preface_len = 24;

    if (length >= preface_len &&
        std::memcmp(data, http2_preface, preface_len) == 0) {
      // This is HTTP/2
      return createParser(HttpVersion::HTTP_2, type, callbacks);
    }

    // Check for HTTP/1.x in first line
    if (length >= 8) {
      std::string first_line(data, std::min(length, size_t(100)));

      // Look for HTTP version string
      if (first_line.find("HTTP/1.1") != std::string::npos) {
        return createParser(HttpVersion::HTTP_1_1, type, callbacks);
      } else if (first_line.find("HTTP/1.0") != std::string::npos) {
        return createParser(HttpVersion::HTTP_1_0, type, callbacks);
      }

      // Check if it looks like an HTTP/1.x request
      // (starts with a method like GET, POST, etc.)
      if (first_line.find("GET ") == 0 || first_line.find("POST ") == 0 ||
          first_line.find("PUT ") == 0 || first_line.find("DELETE ") == 0 ||
          first_line.find("HEAD ") == 0 || first_line.find("OPTIONS ") == 0 ||
          first_line.find("PATCH ") == 0 || first_line.find("CONNECT ") == 0 ||
          first_line.find("TRACE ") == 0) {
        // Default to HTTP/1.1 for requests
        return createParser(HttpVersion::HTTP_1_1, type, callbacks);
      }
    }

    // Default to HTTP/1.1
    return createParser(HttpVersion::HTTP_1_1, type, callbacks);
  }

  HttpParserPtr createParserFromAlpn(const std::string& alpn_protocol,
                                     HttpParserType type,
                                     HttpParserCallbacks* callbacks) override {
    // Map ALPN protocol to HTTP version
    if (alpn_protocol == "h2") {
      return createParser(HttpVersion::HTTP_2, type, callbacks);
    } else if (alpn_protocol == "http/1.1") {
      return createParser(HttpVersion::HTTP_1_1, type, callbacks);
    } else if (alpn_protocol == "http/1.0") {
      return createParser(HttpVersion::HTTP_1_0, type, callbacks);
    }

    // Default to HTTP/1.1
    return createParser(HttpVersion::HTTP_1_1, type, callbacks);
  }

  std::vector<std::string> getSupportedAlpnProtocols() const override {
    std::vector<std::string> protocols;

    // Add protocols based on available parsers
    if (factories_.find(HttpVersion::HTTP_2) != factories_.end()) {
      protocols.push_back("h2");
    }
    if (factories_.find(HttpVersion::HTTP_1_1) != factories_.end()) {
      protocols.push_back("http/1.1");
    }
    if (factories_.find(HttpVersion::HTTP_1_0) != factories_.end()) {
      protocols.push_back("http/1.0");
    }

    return protocols;
  }

 private:
  std::unordered_map<HttpVersion, HttpParserFactorySharedPtr> factories_;
};

// Factory functions

HttpHeadersPtr createHttpHeaders() {
  return std::make_unique<HttpHeadersImpl>();
}

HttpMessagePtr createHttpRequest(HttpMethod method,
                                 const std::string& uri,
                                 HttpVersion version) {
  auto msg = std::make_unique<HttpMessageImpl>(true);
  msg->setMethod(method);
  msg->setUri(uri);
  msg->setVersion(version);
  return std::move(msg);
}

HttpMessagePtr createHttpResponse(HttpStatusCode code, HttpVersion version) {
  auto msg = std::make_unique<HttpMessageImpl>(false);
  msg->setStatusCode(code);
  msg->setStatusText(httpStatusCodeToString(code));
  msg->setVersion(version);
  return std::move(msg);
}

HttpParserSelectorPtr createHttpParserSelector() {
  auto selector = std::make_unique<HttpParserSelectorImpl>();

  // Auto-register available parsers based on compile-time flags
#if MCP_HAS_LLHTTP
  // Note: Register separate factories for HTTP/1.0 and HTTP/1.1
  // Instead of one factory handling both versions, we use version-specific
  // factories This ensures parsers report the correct HTTP version immediately
  // after creation, not just after parsing data. The tests expect
  // parser->httpVersion() to return the requested version right after
  // createParser(version, ...) is called.
  selector->registerFactory({HttpVersion::HTTP_1_0},
                            std::make_shared<http::LLHttp10ParserFactory>());
  selector->registerFactory({HttpVersion::HTTP_1_1},
                            std::make_shared<http::LLHttp11ParserFactory>());
#endif

#if MCP_HAS_NGHTTP2
  // Register nghttp2 for HTTP/2
  auto nghttp2_factory = std::make_shared<http::Nghttp2ParserFactory>();
  selector->registerFactory({HttpVersion::HTTP_2}, nghttp2_factory);
#endif

  return selector;
}

// Helper functions

const char* httpMethodToString(HttpMethod method) {
  switch (method) {
    case HttpMethod::GET:
      return "GET";
    case HttpMethod::POST:
      return "POST";
    case HttpMethod::PUT:
      return "PUT";
    case HttpMethod::DELETE:
      return "DELETE";
    case HttpMethod::HEAD:
      return "HEAD";
    case HttpMethod::OPTIONS:
      return "OPTIONS";
    case HttpMethod::PATCH:
      return "PATCH";
    case HttpMethod::CONNECT:
      return "CONNECT";
    case HttpMethod::TRACE:
      return "TRACE";
    default:
      return "UNKNOWN";
  }
}

HttpMethod httpMethodFromString(const std::string& method) {
  if (method == "GET")
    return HttpMethod::GET;
  if (method == "POST")
    return HttpMethod::POST;
  if (method == "PUT")
    return HttpMethod::PUT;
  if (method == "DELETE")
    return HttpMethod::DELETE;
  if (method == "HEAD")
    return HttpMethod::HEAD;
  if (method == "OPTIONS")
    return HttpMethod::OPTIONS;
  if (method == "PATCH")
    return HttpMethod::PATCH;
  if (method == "CONNECT")
    return HttpMethod::CONNECT;
  if (method == "TRACE")
    return HttpMethod::TRACE;
  return HttpMethod::UNKNOWN;
}

const char* httpVersionToString(HttpVersion version) {
  switch (version) {
    case HttpVersion::HTTP_1_0:
      return "HTTP/1.0";
    case HttpVersion::HTTP_1_1:
      return "HTTP/1.1";
    case HttpVersion::HTTP_2:
      return "HTTP/2";
    case HttpVersion::HTTP_3:
      return "HTTP/3";
    case HttpVersion::UNKNOWN:
    default:
      // Note: Return "UNKNOWN" instead of "HTTP/1.1" for unknown versions
      // The test expects httpVersionToString(HttpVersion::UNKNOWN) to return
      // "UNKNOWN" This ensures proper error reporting and debugging when
      // version detection fails
      return "UNKNOWN";
  }
}

const char* httpStatusCodeToString(HttpStatusCode code) {
  switch (code) {
    case HttpStatusCode::Continue:
      return "Continue";
    case HttpStatusCode::SwitchingProtocols:
      return "Switching Protocols";
    case HttpStatusCode::OK:
      return "OK";
    case HttpStatusCode::Created:
      return "Created";
    case HttpStatusCode::Accepted:
      return "Accepted";
    case HttpStatusCode::NoContent:
      return "No Content";
    case HttpStatusCode::MovedPermanently:
      return "Moved Permanently";
    case HttpStatusCode::Found:
      return "Found";
    case HttpStatusCode::NotModified:
      return "Not Modified";
    case HttpStatusCode::BadRequest:
      return "Bad Request";
    case HttpStatusCode::Unauthorized:
      return "Unauthorized";
    case HttpStatusCode::Forbidden:
      return "Forbidden";
    case HttpStatusCode::NotFound:
      return "Not Found";
    case HttpStatusCode::MethodNotAllowed:
      return "Method Not Allowed";
    case HttpStatusCode::RequestTimeout:
      return "Request Timeout";
    case HttpStatusCode::InternalServerError:
      return "Internal Server Error";
    case HttpStatusCode::NotImplemented:
      return "Not Implemented";
    case HttpStatusCode::BadGateway:
      return "Bad Gateway";
    case HttpStatusCode::ServiceUnavailable:
      return "Service Unavailable";
    case HttpStatusCode::GatewayTimeout:
      return "Gateway Timeout";
    default:
      return "Unknown";
  }
}

}  // namespace http
}  // namespace mcp