#ifndef MCP_HTTP_HTTP_PARSER_H
#define MCP_HTTP_HTTP_PARSER_H

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

#include "mcp/buffer.h"
#include "mcp/core/compat.h"
#include "mcp/core/result.h"

namespace mcp {
namespace http {

// HTTP status codes
enum class HttpStatusCode : uint16_t {
  // 1xx Informational
  Continue = 100,
  SwitchingProtocols = 101,
  
  // 2xx Success
  OK = 200,
  Created = 201,
  Accepted = 202,
  NoContent = 204,
  
  // 3xx Redirection
  MovedPermanently = 301,
  Found = 302,
  NotModified = 304,
  
  // 4xx Client Error
  BadRequest = 400,
  Unauthorized = 401,
  Forbidden = 403,
  NotFound = 404,
  MethodNotAllowed = 405,
  RequestTimeout = 408,
  
  // 5xx Server Error
  InternalServerError = 500,
  NotImplemented = 501,
  BadGateway = 502,
  ServiceUnavailable = 503,
  GatewayTimeout = 504
};

// HTTP methods
enum class HttpMethod {
  GET,
  POST,
  PUT,
  DELETE,
  HEAD,
  OPTIONS,
  PATCH,
  CONNECT,
  TRACE,
  UNKNOWN
};

// HTTP version
enum class HttpVersion {
  HTTP_1_0,
  HTTP_1_1,
  HTTP_2,
  HTTP_3,
  UNKNOWN
};

// Parser type enum
enum class HttpParserType {
  REQUEST,
  RESPONSE,
  BOTH
};

// Parser callback results
enum class ParserCallbackResult {
  Success = 0,
  Error = -1,
  Pause = 1,
  NoBody = 2,
  NoBodyData = 3
};

// Parser status
enum class ParserStatus {
  Ok,
  Paused,
  Error,
  NeedMoreData
};

// Forward declarations
class HttpParser;
class HttpParserCallbacks;
class HttpMessage;
class HttpHeaders;

using HttpParserPtr = std::unique_ptr<HttpParser>;
using HttpHeaderMap = std::unordered_map<std::string, std::string>;

/**
 * HTTP headers container
 * Maintains header order and allows case-insensitive lookups
 */
class HttpHeaders {
 public:
  virtual ~HttpHeaders() = default;
  
  // Add a header (appends if exists)
  virtual void add(const std::string& name, const std::string& value) = 0;
  
  // Set a header (replaces if exists)
  virtual void set(const std::string& name, const std::string& value) = 0;
  
  // Remove a header
  virtual void remove(const std::string& name) = 0;
  
  // Get header value (case-insensitive)
  virtual optional<std::string> get(const std::string& name) const = 0;
  
  // Get all values for a header
  virtual std::vector<std::string> getAll(const std::string& name) const = 0;
  
  // Check if header exists
  virtual bool has(const std::string& name) const = 0;
  
  // Clear all headers
  virtual void clear() = 0;
  
  // Get all headers as map
  virtual HttpHeaderMap getMap() const = 0;
  
  // Iterate headers in order
  virtual void forEach(std::function<void(const std::string&, const std::string&)> cb) const = 0;
  
  // Get total byte size
  virtual size_t byteSize() const = 0;
};

using HttpHeadersPtr = std::unique_ptr<HttpHeaders>;
using HttpHeadersSharedPtr = std::shared_ptr<HttpHeaders>;

/**
 * HTTP message (request or response)
 */
class HttpMessage {
 public:
  virtual ~HttpMessage() = default;
  
  // Request-specific methods
  virtual HttpMethod method() const = 0;
  virtual std::string methodString() const = 0;
  virtual std::string uri() const = 0;
  virtual std::string path() const = 0;
  virtual std::string query() const = 0;
  
  // Response-specific methods
  virtual HttpStatusCode statusCode() const = 0;
  virtual std::string statusText() const = 0;
  
  // Common methods
  virtual HttpVersion version() const = 0;
  virtual HttpHeaders& headers() = 0;
  virtual const HttpHeaders& headers() const = 0;
  virtual Buffer& body() = 0;
  virtual const Buffer& body() const = 0;
  virtual bool isChunked() const = 0;
  virtual bool hasTrailers() const = 0;
  virtual HttpHeaders& trailers() = 0;
  virtual const HttpHeaders& trailers() const = 0;
};

using HttpMessagePtr = std::unique_ptr<HttpMessage>;
using HttpMessageSharedPtr = std::shared_ptr<HttpMessage>;

/**
 * HTTP parser callbacks interface
 * Implement this to receive parsing events
 */
class HttpParserCallbacks {
 public:
  virtual ~HttpParserCallbacks() = default;
  
  /**
   * Called when message parsing begins
   */
  virtual ParserCallbackResult onMessageBegin() = 0;
  
  /**
   * Called when URL is parsed (request only)
   */
  virtual ParserCallbackResult onUrl(const char* data, size_t length) = 0;
  
  /**
   * Called when status is parsed (response only)
   */
  virtual ParserCallbackResult onStatus(const char* data, size_t length) = 0;
  
  /**
   * Called for each header field
   */
  virtual ParserCallbackResult onHeaderField(const char* data, size_t length) = 0;
  
  /**
   * Called for each header value
   */
  virtual ParserCallbackResult onHeaderValue(const char* data, size_t length) = 0;
  
  /**
   * Called when headers are complete
   */
  virtual ParserCallbackResult onHeadersComplete() = 0;
  
  /**
   * Called for body data chunks
   */
  virtual ParserCallbackResult onBody(const char* data, size_t length) = 0;
  
  /**
   * Called when message is complete
   */
  virtual ParserCallbackResult onMessageComplete() = 0;
  
  /**
   * Called for chunk header (chunked encoding)
   */
  virtual ParserCallbackResult onChunkHeader(size_t length) = 0;
  
  /**
   * Called when chunk is complete
   */
  virtual ParserCallbackResult onChunkComplete() = 0;
  
  /**
   * Called on parser error
   */
  virtual void onError(const std::string& error) = 0;
};

/**
 * Abstract HTTP parser interface
 * Supports different HTTP versions and parser implementations
 */
class HttpParser {
 public:
  virtual ~HttpParser() = default;
  
  /**
   * Execute parser on input data
   * Returns number of bytes consumed
   */
  virtual size_t execute(const char* data, size_t length) = 0;
  
  /**
   * Resume a paused parser
   */
  virtual void resume() = 0;
  
  /**
   * Pause the parser
   */
  virtual ParserCallbackResult pause() = 0;
  
  /**
   * Get parser status
   */
  virtual ParserStatus getStatus() const = 0;
  
  /**
   * Check if should keep connection alive
   */
  virtual bool shouldKeepAlive() const = 0;
  
  /**
   * Check if this is an upgrade request
   */
  virtual bool isUpgrade() const = 0;
  
  /**
   * Get HTTP version
   */
  virtual HttpVersion httpVersion() const = 0;
  
  /**
   * Get HTTP method (request only)
   */
  virtual HttpMethod httpMethod() const = 0;
  
  /**
   * Get status code (response only)
   */
  virtual HttpStatusCode statusCode() const = 0;
  
  /**
   * Get error string if in error state
   */
  virtual std::string getError() const = 0;
  
  /**
   * Reset parser for reuse
   */
  virtual void reset() = 0;
  
  /**
   * Finish parsing (for HTTP/2 and similar)
   */
  virtual void finish() = 0;
};

/**
 * HTTP parser factory interface
 */
class HttpParserFactory {
 public:
  virtual ~HttpParserFactory() = default;
  
  /**
   * Create a parser instance
   */
  virtual HttpParserPtr createParser(HttpParserType type,
                                     HttpParserCallbacks* callbacks) = 0;
  
  /**
   * Get supported HTTP versions
   */
  virtual std::vector<HttpVersion> supportedVersions() const = 0;
  
  /**
   * Get parser implementation name
   */
  virtual std::string name() const = 0;
};

using HttpParserFactoryPtr = std::unique_ptr<HttpParserFactory>;
using HttpParserFactorySharedPtr = std::shared_ptr<HttpParserFactory>;

/**
 * Parser selector for multi-version support
 */
class HttpParserSelector {
 public:
  virtual ~HttpParserSelector() = default;
  
  /**
   * Register a parser factory for specific versions
   */
  virtual void registerFactory(const std::vector<HttpVersion>& versions,
                               HttpParserFactorySharedPtr factory) = 0;
  
  /**
   * Create parser for a specific version
   */
  virtual HttpParserPtr createParser(HttpVersion version,
                                     HttpParserType type,
                                     HttpParserCallbacks* callbacks) = 0;
  
  /**
   * Auto-detect version and create parser
   */
  virtual HttpParserPtr detectAndCreateParser(const char* data,
                                              size_t length,
                                              HttpParserType type,
                                              HttpParserCallbacks* callbacks) = 0;
  
  /**
   * Create parser based on ALPN protocol
   * @param alpn_protocol The negotiated ALPN protocol (e.g., "h2", "http/1.1")
   */
  virtual HttpParserPtr createParserFromAlpn(const std::string& alpn_protocol,
                                             HttpParserType type,
                                             HttpParserCallbacks* callbacks) = 0;
  
  /**
   * Get supported ALPN protocols
   */
  virtual std::vector<std::string> getSupportedAlpnProtocols() const = 0;
};

using HttpParserSelectorPtr = std::unique_ptr<HttpParserSelector>;

// Factory functions

/**
 * Create default HTTP headers implementation
 */
HttpHeadersPtr createHttpHeaders();

/**
 * Create HTTP message
 */
HttpMessagePtr createHttpRequest(HttpMethod method,
                                 const std::string& uri,
                                 HttpVersion version = HttpVersion::HTTP_1_1);

HttpMessagePtr createHttpResponse(HttpStatusCode code,
                                  HttpVersion version = HttpVersion::HTTP_1_1);

/**
 * Create parser selector
 */
HttpParserSelectorPtr createHttpParserSelector();

// Helper functions

/**
 * Convert method enum to string
 */
const char* httpMethodToString(HttpMethod method);

/**
 * Parse method from string
 */
HttpMethod httpMethodFromString(const std::string& method);

/**
 * Convert version enum to string
 */
const char* httpVersionToString(HttpVersion version);

/**
 * Get status code description
 */
const char* httpStatusCodeToString(HttpStatusCode code);

}  // namespace http
}  // namespace mcp

#endif  // MCP_HTTP_HTTP_PARSER_H