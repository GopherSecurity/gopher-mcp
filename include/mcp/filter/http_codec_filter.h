#pragma once

#include <functional>
#include <memory>

#include "mcp/buffer.h"
#include "mcp/core/result.h"
#include "mcp/event/event_loop.h"
#include "mcp/filter/filter_context.h"
#include "mcp/filter/http_codec_state_machine.h"
#include "mcp/filter/http_stream_context.h"
#include "mcp/http/http_parser.h"
#include "mcp/network/filter.h"

namespace mcp {
namespace filter {

/**
 * HttpCodecFilter - HTTP/1.1 codec supporting both client and server modes
 *
 * Following production design principles:
 * - Transport sockets handle ONLY raw I/O (bytes in/out)
 * - Protocol codecs are implemented as filters
 * - Clean separation between transport and protocol layers
 *
 * Architecture:
 * ```
 * [Network] → [RawBufferSocket] → [HttpCodecFilter] → [Application]
 *              (Pure I/O only)     (HTTP protocol only)    (Business logic)
 * ```
 *
 * This filter:
 * - Server mode: Parses incoming requests, formats outgoing responses
 * - Client mode: Formats outgoing requests, parses incoming responses
 * - Manages HTTP/1.1 protocol state machine for both modes
 * - Does NOT touch sockets or perform I/O
 */
class HttpCodecFilter : public network::Filter {
 public:
  /**
   * HTTP message callback interface
   * Supports both client and server modes
   */
  class MessageCallbacks {
   public:
    virtual ~MessageCallbacks() = default;

    /**
     * Called when HTTP headers are complete
     * Server mode: request headers received
     * Client mode: response headers received
     * @param headers Map of header key-value pairs
     * @param keep_alive Whether connection should be kept alive
     */
    virtual void onHeaders(const std::map<std::string, std::string>& headers,
                           bool keep_alive) = 0;

    /**
     * Called when message body data is received
     * Server mode: request body data
     * Client mode: response body data
     * @param data Body data
     * @param end_stream True if this is the last body chunk
     */
    virtual void onBody(const std::string& data, bool end_stream) = 0;

    /**
     * Called when complete message is received
     * Server mode: complete request received
     * Client mode: complete response received
     */
    virtual void onMessageComplete() = 0;

    /**
     * Called on protocol error
     */
    virtual void onError(const std::string& error) = 0;
  };

  /**
   * HTTP message encoder interface
   * Supports both request and response encoding
   */
  class MessageEncoder {
   public:
    virtual ~MessageEncoder() = default;

    /**
     * Encode HTTP headers
     * Server mode: response headers with status code
     * Client mode: request headers with method and path
     * @param status_code_or_method Status code (server) or HTTP method (client)
     * @param headers HTTP headers
     * @param end_stream Whether this completes the message
     * @param path Request path (client mode only, ignored in server mode)
     */
    virtual void encodeHeaders(
        const std::string& status_code_or_method,
        const std::map<std::string, std::string>& headers,
        bool end_stream,
        const std::string& path = "") = 0;

    /**
     * Encode message body
     * @param data Body data
     * @param end_stream Whether this completes the message
     */
    virtual void encodeData(Buffer& data, bool end_stream) = 0;
  };

  /**
   * Constructor with FilterCreationContext for config-driven filter chains
   * @param context Filter creation context containing dispatcher, callbacks,
   * and transport metadata
   * @param config Filter-specific configuration
   */
  HttpCodecFilter(const filter::FilterCreationContext& context,
                  const json::JsonValue& config);

  /**
   * Constructor (legacy)
   * @param callbacks Message callbacks for the application layer
   * @param dispatcher Event dispatcher for async operations
   * @param is_server True for server mode, false for client mode
   */
  HttpCodecFilter(MessageCallbacks& callbacks,
                  event::Dispatcher& dispatcher,
                  bool is_server = true);

  ~HttpCodecFilter() override;

  // network::ReadFilter interface
  network::FilterStatus onNewConnection() override;
  network::FilterStatus onData(Buffer& data, bool end_stream) override;
  void initializeReadFilterCallbacks(
      network::ReadFilterCallbacks& callbacks) override {
    read_callbacks_ = &callbacks;
  }

  // network::WriteFilter interface
  network::FilterStatus onWrite(Buffer& data, bool end_stream) override;
  void initializeWriteFilterCallbacks(
      network::WriteFilterCallbacks& callbacks) override {
    write_callbacks_ = &callbacks;
  }

  /**
   * Get message encoder for sending HTTP messages
   * Server mode: response encoder
   * Client mode: request encoder
   */
  MessageEncoder& messageEncoder() { return *message_encoder_; }

  /**
   * Set HTTP client endpoint (client mode only)
   * @param path HTTP request path (e.g., "/sse", "/mcp")
   * @param host HTTP Host header value
   */
  void setClientEndpoint(const std::string& path, const std::string& host) {
    client_path_ = path;
    client_host_ = host;
  }

  /**
   * Set the message endpoint for POST requests (client mode only)
   * Called after receiving endpoint event from SSE stream
   * @param endpoint The URL path for sending JSON-RPC messages
   */
  void setMessageEndpoint(const std::string& endpoint) {
    message_endpoint_ = endpoint;
    has_message_endpoint_ = true;
  }

  /**
   * Check if we have a message endpoint for POST requests
   */
  bool hasMessageEndpoint() const { return has_message_endpoint_; }

  /**
   * Get the message endpoint
   */
  const std::string& getMessageEndpoint() const { return message_endpoint_; }

  /**
   * Set whether to use GET for initial SSE connection (client mode only)
   */
  void setUseSseGet(bool use_sse_get) { use_sse_get_ = use_sse_get; }

  /**
   * Check if initial SSE GET request has been sent
   */
  bool hasSentSseGetRequest() const { return sse_get_sent_; }

  /**
   * Mark SSE GET request as sent
   */
  void markSseGetSent() { sse_get_sent_ = true; }

  /**
   * Get and clear the message buffer
   * Used after encodeHeaders/encodeData to get the built message
   */
  void getMessageBuffer(Buffer& buffer) { buffer.move(message_buffer_); }

  /**
   * Get read filter callbacks for data injection
   * Used by callback bridges to forward data to next filter
   */
  network::ReadFilterCallbacks* getReadFilterCallbacks() const {
    return read_callbacks_;
  }

 private:
  // Inner class implementing MessageEncoder
  class MessageEncoderImpl : public MessageEncoder {
   public:
    MessageEncoderImpl(HttpCodecFilter& parent) : parent_(parent) {}

    void encodeHeaders(const std::string& status_code_or_method,
                       const std::map<std::string, std::string>& headers,
                       bool end_stream,
                       const std::string& path = "") override;
    void encodeData(Buffer& data, bool end_stream) override;

   private:
    HttpCodecFilter& parent_;
  };

  // HTTP parser callbacks following production pattern
  class ParserCallbacks : public http::HttpParserCallbacks {
   public:
    ParserCallbacks(HttpCodecFilter& parent) : parent_(parent) {}

    http::ParserCallbackResult onMessageBegin() override;
    http::ParserCallbackResult onUrl(const char* data, size_t length) override;
    http::ParserCallbackResult onStatus(const char* data,
                                        size_t length) override;
    http::ParserCallbackResult onHeaderField(const char* data,
                                             size_t length) override;
    http::ParserCallbackResult onHeaderValue(const char* data,
                                             size_t length) override;
    http::ParserCallbackResult onHeadersComplete() override;
    http::ParserCallbackResult onBody(const char* data, size_t length) override;
    http::ParserCallbackResult onMessageComplete() override;
    http::ParserCallbackResult onChunkHeader(size_t chunk_size) override;
    http::ParserCallbackResult onChunkComplete() override;
    void onError(const std::string& error) override;

   private:
    HttpCodecFilter& parent_;
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
   * Send message data through filter chain
   * Server mode: response data
   * Client mode: request data
   */
  void sendMessageData(Buffer& data);

  /**
   * Handle state machine state changes
   */
  void onCodecStateChange(const HttpCodecStateTransitionContext& context);

  /**
   * Handle state machine errors
   */
  void onCodecError(const std::string& error);

  // Components
  MessageCallbacks* message_callbacks_;
  event::Dispatcher& dispatcher_;
  bool is_server_;
  std::string client_path_{"/rpc"};    // HTTP request path for client mode
  std::string client_host_{"localhost"};  // HTTP Host header for client mode
  std::string message_endpoint_;  // Endpoint for POST requests (from SSE endpoint event)
  bool has_message_endpoint_{false};  // Whether we have received the message endpoint
  bool use_sse_get_{false};  // Whether to use GET for initial SSE connection
  bool sse_get_sent_{false};  // Whether the initial SSE GET has been sent
  network::ReadFilterCallbacks* read_callbacks_{nullptr};
  network::WriteFilterCallbacks* write_callbacks_{nullptr};

  // Filter chain bridge for context-based construction
  class HttpFilterChainBridge : public MessageCallbacks {
   public:
    explicit HttpFilterChainBridge(HttpCodecFilter& parent_filter);

    // MessageCallbacks implementation
    void onHeaders(const std::map<std::string, std::string>& headers,
                   bool keep_alive) override;
    void onBody(const std::string& data, bool end_stream) override;
    void onMessageComplete() override;
    void onError(const std::string& error) override;

   private:
    void forwardBodyToNextFilter(const std::string& body, bool end_stream);

    HttpCodecFilter& parent_filter_;
  };

  std::unique_ptr<HttpFilterChainBridge> filter_bridge_;

  std::unique_ptr<http::HttpParser> parser_;
  std::unique_ptr<ParserCallbacks> parser_callbacks_;
  std::unique_ptr<MessageEncoderImpl> message_encoder_;
  std::unique_ptr<HttpCodecStateMachine> state_machine_;

  // Stream context management for stateless filter design
  // Per-request state is stored in HttpStreamContext, not in the filter
  HttpStreamContextManager stream_manager_;
  HttpStreamContextPtr current_stream_;  // Current stream being processed

  // Message buffering (connection-level, not per-request)
  OwnedBuffer message_buffer_;
};

}  // namespace filter
}  // namespace mcp
