#pragma once

#include "mcp/network/filter.h"
#include "mcp/buffer.h"
#include "mcp/http/http_parser.h"
#include "mcp/core/result.h"
#include "mcp/event/event_loop.h"
#include <memory>
#include <functional>

namespace mcp {
namespace filter {

/**
 * HttpServerCodecFilter - HTTP/1.1 server codec following production architecture
 * 
 * Following production design principles:
 * - Transport sockets handle ONLY raw I/O (bytes in/out)
 * - Protocol codecs are implemented as filters
 * - Clean separation between transport and protocol layers
 * 
 * Architecture:
 * ```
 * [Network] → [RawBufferSocket] → [HttpServerCodecFilter] → [Application]
 *              (Pure I/O only)     (HTTP protocol only)      (Business logic)
 * ```
 * 
 * This filter:
 * - Parses incoming HTTP requests from raw bytes
 * - Formats outgoing HTTP responses to raw bytes
 * - Manages HTTP/1.1 protocol state machine
 * - Does NOT touch sockets or perform I/O
 */
class HttpServerCodecFilter : public network::Filter {
public:
  /**
   * HTTP request callback interface
   * Following production RequestDecoder pattern
   */
  class RequestCallbacks {
  public:
    virtual ~RequestCallbacks() = default;
    
    /**
     * Called when HTTP headers are complete
     * @param headers Map of header key-value pairs
     * @param keep_alive Whether connection should be kept alive
     */
    virtual void onHeaders(const std::map<std::string, std::string>& headers,
                          bool keep_alive) = 0;
    
    /**
     * Called when request body data is received
     * @param data Body data
     * @param end_stream True if this is the last body chunk
     */
    virtual void onBody(const std::string& data, bool end_stream) = 0;
    
    /**
     * Called when complete request is received
     */
    virtual void onMessageComplete() = 0;
    
    /**
     * Called on protocol error
     */
    virtual void onError(const std::string& error) = 0;
  };
  
  /**
   * HTTP response encoder interface
   * Following production ResponseEncoder pattern
   */
  class ResponseEncoder {
  public:
    virtual ~ResponseEncoder() = default;
    
    /**
     * Encode response headers
     * @param status_code HTTP status code
     * @param headers Response headers
     * @param end_stream Whether this completes the response
     */
    virtual void encodeHeaders(int status_code,
                              const std::map<std::string, std::string>& headers,
                              bool end_stream) = 0;
    
    /**
     * Encode response body
     * @param data Body data
     * @param end_stream Whether this completes the response
     */
    virtual void encodeData(Buffer& data, bool end_stream) = 0;
  };

  /**
   * Constructor
   * @param callbacks Request callbacks for the application layer
   * @param dispatcher Event dispatcher for async operations
   */
  HttpServerCodecFilter(RequestCallbacks& callbacks,
                        event::Dispatcher& dispatcher);
  
  ~HttpServerCodecFilter() override;

  // network::ReadFilter interface
  network::FilterStatus onNewConnection() override;
  network::FilterStatus onData(Buffer& data, bool end_stream) override;
  void initializeReadFilterCallbacks(network::ReadFilterCallbacks& callbacks) override {
    read_callbacks_ = &callbacks;
  }
  
  // network::WriteFilter interface
  network::FilterStatus onWrite(Buffer& data, bool end_stream) override;
  void initializeWriteFilterCallbacks(network::WriteFilterCallbacks& callbacks) override {
    write_callbacks_ = &callbacks;
  }
  
  /**
   * Get response encoder for sending HTTP responses
   * Following production pattern of encoder/decoder separation
   */
  ResponseEncoder& responseEncoder() { return *response_encoder_; }

private:
  // Inner class implementing ResponseEncoder
  class ResponseEncoderImpl : public ResponseEncoder {
  public:
    ResponseEncoderImpl(HttpServerCodecFilter& parent) : parent_(parent) {}
    
    void encodeHeaders(int status_code,
                       const std::map<std::string, std::string>& headers,
                       bool end_stream) override;
    void encodeData(Buffer& data, bool end_stream) override;
    
  private:
    HttpServerCodecFilter& parent_;
  };
  
  // HTTP parser callbacks following production pattern
  class ParserCallbacks : public http::HttpParserCallbacks {
  public:
    ParserCallbacks(HttpServerCodecFilter& parent) : parent_(parent) {}
    
    http::ParserCallbackResult onMessageBegin() override;
    http::ParserCallbackResult onUrl(const char* data, size_t length) override;
    http::ParserCallbackResult onStatus(const char* data, size_t length) override;
    http::ParserCallbackResult onHeaderField(const char* data, size_t length) override;
    http::ParserCallbackResult onHeaderValue(const char* data, size_t length) override;
    http::ParserCallbackResult onHeadersComplete() override;
    http::ParserCallbackResult onBody(const char* data, size_t length) override;
    http::ParserCallbackResult onMessageComplete() override;
    http::ParserCallbackResult onChunkHeader(size_t chunk_size) override;
    http::ParserCallbackResult onChunkComplete() override;
    void onError(const std::string& error) override;
    
  private:
    HttpServerCodecFilter& parent_;
    std::string current_header_field_;
    std::string current_header_value_;
  };
  
  /**
   * Process incoming HTTP data
   * Following production dispatch pattern
   */
  void dispatch(Buffer& data);
  
  /**
   * Handle parser errors
   */
  void handleParserError(const std::string& error);
  
  /**
   * Send response data through filter chain
   * Following production pattern of using connection for writes
   */
  void sendResponseData(Buffer& data);

  // State management
  enum class CodecState {
    IDLE,                    // Waiting for request
    PROCESSING_HEADERS,      // Processing request headers
    PROCESSING_BODY,         // Processing request body
    SENDING_RESPONSE,        // Sending response
    COMPLETE                 // Request/response complete
  };
  
  CodecState state_{CodecState::IDLE};
  
  // Components
  RequestCallbacks& request_callbacks_;
  event::Dispatcher& dispatcher_;
  network::ReadFilterCallbacks* read_callbacks_{nullptr};
  network::WriteFilterCallbacks* write_callbacks_{nullptr};
  
  std::unique_ptr<http::HttpParser> parser_;
  std::unique_ptr<ParserCallbacks> parser_callbacks_;
  std::unique_ptr<ResponseEncoderImpl> response_encoder_;
  
  // Request state
  std::map<std::string, std::string> current_headers_;
  std::string current_body_;
  bool keep_alive_{true};
  
  // Response buffering
  OwnedBuffer response_buffer_;
};


} // namespace filter
} // namespace mcp