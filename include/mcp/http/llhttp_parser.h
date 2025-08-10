#ifndef MCP_HTTP_LLHTTP_PARSER_H
#define MCP_HTTP_LLHTTP_PARSER_H

#include <memory>

#include "mcp/http/http_parser.h"

// Forward declare llhttp types to avoid including llhttp.h in header
typedef struct llhttp__internal_s llhttp_t;
typedef struct llhttp_settings_s llhttp_settings_t;

namespace mcp {
namespace http {

/**
 * llhttp-based HTTP/1.x parser implementation
 * 
 * Uses Node.js's llhttp parser for high-performance HTTP/1.x parsing
 * Thread-safety: Parser instances are not thread-safe, create one per connection
 */
class LLHttpParser : public HttpParser {
 public:
  /**
   * Create llhttp parser
   * @param type Parser type (request, response, or both)
   * @param callbacks Callbacks to invoke during parsing
   */
  LLHttpParser(HttpParserType type, HttpParserCallbacks* callbacks);
  ~LLHttpParser() override;
  
  // HttpParser interface
  size_t execute(const char* data, size_t length) override;
  void resume() override;
  ParserCallbackResult pause() override;
  ParserStatus getStatus() const override;
  bool shouldKeepAlive() const override;
  bool isUpgrade() const override;
  HttpVersion httpVersion() const override;
  HttpMethod httpMethod() const override;
  HttpStatusCode statusCode() const override;
  std::string getError() const override;
  void reset() override;
  void finish() override;
  
 private:
  // Static callbacks for llhttp (bridge to HttpParserCallbacks)
  static int onMessageBegin(llhttp_t* parser);
  static int onUrl(llhttp_t* parser, const char* data, size_t length);
  static int onStatus(llhttp_t* parser, const char* data, size_t length);
  static int onHeaderField(llhttp_t* parser, const char* data, size_t length);
  static int onHeaderValue(llhttp_t* parser, const char* data, size_t length);
  static int onHeadersComplete(llhttp_t* parser);
  static int onBody(llhttp_t* parser, const char* data, size_t length);
  static int onMessageComplete(llhttp_t* parser);
  static int onChunkHeader(llhttp_t* parser);
  static int onChunkComplete(llhttp_t* parser);
  
  // Convert llhttp callback result to our enum
  static int toCallbackResult(ParserCallbackResult result);
  
  // Parser state
  std::unique_ptr<llhttp_t> parser_;
  std::unique_ptr<llhttp_settings_t> settings_;
  HttpParserCallbacks* callbacks_;
  HttpParserType type_;
  ParserStatus status_;
  
  // Cached values for performance
  mutable HttpMethod cached_method_;
  mutable bool method_cached_;
  mutable HttpStatusCode cached_status_;
  mutable bool status_cached_;
  mutable HttpVersion cached_version_;
  mutable bool version_cached_;
};

/**
 * Factory for creating llhttp parsers
 */
class LLHttpParserFactory : public HttpParserFactory {
 public:
  LLHttpParserFactory() = default;
  ~LLHttpParserFactory() override = default;
  
  // HttpParserFactory interface
  HttpParserPtr createParser(HttpParserType type,
                             HttpParserCallbacks* callbacks) override;
  std::vector<HttpVersion> supportedVersions() const override;
  std::string name() const override { return "llhttp"; }
};

/**
 * Create llhttp parser factory
 */
inline HttpParserFactoryPtr createLLHttpParserFactory() {
  return std::make_unique<LLHttpParserFactory>();
}

}  // namespace http
}  // namespace mcp

#endif  // MCP_HTTP_LLHTTP_PARSER_H