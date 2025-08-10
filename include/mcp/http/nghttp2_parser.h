#ifndef MCP_HTTP_NGHTTP2_PARSER_H
#define MCP_HTTP_NGHTTP2_PARSER_H

#include <memory>
#include <string>
#include <vector>
#include <map>

#include "mcp/http/http_parser.h"

// Forward declare nghttp2 types to avoid exposing nghttp2.h in header
struct nghttp2_session;
struct nghttp2_session_callbacks;

namespace mcp {
namespace http {

/**
 * nghttp2-based HTTP/2 parser implementation
 * 
 * Provides HTTP/2 parsing using the nghttp2 library
 */
class Nghttp2Parser : public HttpParser {
 public:
  explicit Nghttp2Parser(HttpParserType type, HttpParserCallbacks* callbacks);
  ~Nghttp2Parser() override;
  
  // HttpParser interface
  size_t execute(const char* data, size_t length) override;
  void resume() override;
  ParserCallbackResult pause() override;
  ParserStatus getStatus() const override;
  bool shouldKeepAlive() const override;
  bool isUpgrade() const override;
  HttpVersion httpVersion() const override { return HttpVersion::HTTP_2; }
  HttpMethod httpMethod() const override;
  HttpStatusCode statusCode() const override;
  std::string getError() const override;
  void reset() override;
  void finish() override;
  
  // HTTP/2 specific methods
  void submitSettings(const std::map<uint32_t, uint32_t>& settings);
  void submitPing(const uint8_t* opaque_data);
  void submitGoaway(uint32_t last_stream_id, uint32_t error_code);
  void submitWindowUpdate(int32_t stream_id, int32_t window_size_increment);
  void submitPriority(int32_t stream_id, int32_t weight, uint8_t exclusive);
  
  // Get pending data to send
  std::vector<uint8_t> getPendingData();
  
 public:
  // nghttp2 callbacks - need to be public for wrapper functions
  static int onFrameRecvCallback(nghttp2_session* session, 
                                 const void* frame, void* user_data);
  static int onDataChunkRecvCallback(nghttp2_session* session, uint8_t flags,
                                     int32_t stream_id, const uint8_t* data,
                                     size_t len, void* user_data);
  static int onStreamCloseCallback(nghttp2_session* session, int32_t stream_id,
                                   uint32_t error_code, void* user_data);
  static int onHeaderCallback(nghttp2_session* session, const void* frame,
                              const uint8_t* name, size_t namelen,
                              const uint8_t* value, size_t valuelen,
                              uint8_t flags, void* user_data);
  static int onBeginHeadersCallback(nghttp2_session* session, 
                                    const void* frame, void* user_data);
  static ssize_t onSendCallback(nghttp2_session* session, const uint8_t* data,
                                size_t length, int flags, void* user_data);
  
 private:
  
  // Helper methods
  void initializeSession();
  void processFrame(const void* frame);
  void handleError(int error_code);
  
  // Stream tracking
  struct StreamData {
    HttpMethod method{HttpMethod::UNKNOWN};
    std::string uri;
    HttpStatusCode status{HttpStatusCode::OK};
    std::map<std::string, std::string> headers;
    std::vector<uint8_t> body;
    bool headers_complete{false};
  };
  
  // Members
  HttpParserCallbacks* callbacks_;
  HttpParserType type_;
  ParserStatus status_;
  nghttp2_session* session_;
  nghttp2_session_callbacks* nghttp2_callbacks_;
  std::map<int32_t, StreamData> streams_;
  int32_t current_stream_id_{0};
  std::vector<uint8_t> send_buffer_;
  std::string error_message_;
  
  // Cached values
  mutable HttpMethod cached_method_{HttpMethod::UNKNOWN};
  mutable bool method_cached_{false};
  mutable HttpStatusCode cached_status_{HttpStatusCode::OK};
  mutable bool status_cached_{false};
};

/**
 * Factory for creating nghttp2 parser instances
 */
class Nghttp2ParserFactory : public HttpParserFactory {
 public:
  HttpParserPtr createParser(HttpParserType type,
                             HttpParserCallbacks* callbacks) override;
  
  std::vector<HttpVersion> supportedVersions() const override;
  
  std::string name() const override { return "nghttp2"; }
};

}  // namespace http
}  // namespace mcp

#endif  // MCP_HTTP_NGHTTP2_PARSER_H