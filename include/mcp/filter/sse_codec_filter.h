#pragma once

#include <functional>
#include <memory>

#include "mcp/buffer.h"
#include "mcp/event/event_loop.h"
#include "mcp/filter/filter_context.h"
#include "mcp/filter/sse_codec_state_machine.h"
#include "mcp/http/sse_parser.h"
#include "mcp/network/filter.h"

namespace mcp {
namespace filter {

/**
 * SseCodecFilter - Server-Sent Events codec following production architecture
 *
 * Following production design principles:
 * - Filters handle protocol-specific logic
 * - Clean separation from transport layer
 * - Composable with other filters in chain
 *
 * Architecture (following production codec pattern):
 * ```
 * [HttpCodec] → [SseCodec] → [JsonRpc] → [Application]
 *   (HTTP)       (SSE)      (RPC layer)  (Business)
 * ```
 *
 * This filter:
 * - Parses SSE events from HTTP response body
 * - Formats outgoing messages as SSE events
 * - Manages SSE protocol state
 * - Does NOT handle HTTP or transport concerns
 */
class SseCodecFilter : public network::Filter {
 public:
  /**
   * SSE event callback interface
   * Following production decoder callback pattern
   */
  class EventCallbacks {
   public:
    virtual ~EventCallbacks() = default;

    /**
     * Called when an SSE event is received
     * @param event Event type (empty for default)
     * @param data Event data
     * @param id Optional event ID
     */
    virtual void onEvent(const std::string& event,
                         const std::string& data,
                         const optional<std::string>& id) = 0;

    /**
     * Called when SSE comment is received
     * @param comment Comment text
     */
    virtual void onComment(const std::string& comment) = 0;

    /**
     * Called on SSE protocol error
     */
    virtual void onError(const std::string& error) = 0;
  };

  /**
   * SSE event encoder interface
   * Following production encoder pattern
   */
  class EventEncoder {
   public:
    virtual ~EventEncoder() = default;

    /**
     * Encode an SSE event
     * @param event Event type (empty for default)
     * @param data Event data (can be multi-line)
     * @param id Optional event ID
     */
    virtual void encodeEvent(const std::string& event,
                             const std::string& data,
                             const optional<std::string>& id = nullopt) = 0;

    /**
     * Encode an SSE comment (for keep-alive)
     * @param comment Comment text
     */
    virtual void encodeComment(const std::string& comment) = 0;

    /**
     * Send retry timeout to client
     * @param retry_ms Retry timeout in milliseconds
     */
    virtual void encodeRetry(uint32_t retry_ms) = 0;
  };

  /**
   * Constructor with FilterCreationContext for config-driven filter chains
   * @param context Filter creation context containing dispatcher, callbacks,
   * and transport metadata
   * @param config Filter-specific configuration
   */
  SseCodecFilter(const filter::FilterCreationContext& context,
                 const json::JsonValue& config);

  /**
   * Constructor (legacy)
   * @param callbacks Event callbacks for application layer
   * @param dispatcher Event dispatcher for async operations
   * @param is_server True for server mode (encoding), false for client
   * (decoding)
   */
  SseCodecFilter(EventCallbacks& callbacks,
                 event::Dispatcher& dispatcher,
                 bool is_server);

  ~SseCodecFilter() override;

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
   * Get event encoder for sending SSE events
   */
  EventEncoder& eventEncoder() { return *event_encoder_; }

  /**
   * Start SSE stream by sending headers
   * This should be called after HTTP headers are sent
   */
  void startEventStream();

 private:
  // Inner class implementing EventEncoder
  class EventEncoderImpl : public EventEncoder {
   public:
    EventEncoderImpl(SseCodecFilter& parent) : parent_(parent) {}

    void encodeEvent(const std::string& event,
                     const std::string& data,
                     const optional<std::string>& id) override;
    void encodeComment(const std::string& comment) override;
    void encodeRetry(uint32_t retry_ms) override;

   private:
    SseCodecFilter& parent_;
  };

  // SSE parser callbacks
  class ParserCallbacks : public http::SseParserCallbacks {
   public:
    ParserCallbacks(SseCodecFilter& parent) : parent_(parent) {}

    void onSseEvent(const http::SseEvent& event) override;
    void onSseComment(const std::string& comment) override;
    void onSseError(const std::string& error) override;

   private:
    SseCodecFilter& parent_;
  };

  /**
   * Process incoming SSE data
   */
  void dispatch(Buffer& data);

  /**
   * Send SSE data through filter chain
   */
  void sendEventData(Buffer& data);

  /**
   * Format SSE field
   * Following SSE specification format
   */
  static void formatSseField(Buffer& buffer,
                             const std::string& field,
                             const std::string& value);

  /**
   * Handle state machine state changes
   */
  void onCodecStateChange(const SseCodecStateTransitionContext& context);

  /**
   * Handle state machine errors
   */
  void onCodecError(const std::string& error);

  /**
   * Send keep-alive comment
   */
  void sendKeepAliveComment();

  // Components
  EventCallbacks* event_callbacks_;
  event::Dispatcher& dispatcher_;
  bool is_server_;  // True for server mode (encoding)

  network::ReadFilterCallbacks* read_callbacks_{nullptr};
  network::WriteFilterCallbacks* write_callbacks_{nullptr};

  // Filter chain bridge for context-based construction
  class SseFilterChainBridge : public EventCallbacks {
   public:
    explicit SseFilterChainBridge(SseCodecFilter& parent_filter);

    // EventCallbacks implementation
    void onEvent(const std::string& event,
                 const std::string& data,
                 const optional<std::string>& id) override;
    void onComment(const std::string& comment) override;
    void onError(const std::string& error) override;

   private:
    void forwardJsonRpcToNextFilter(const std::string& payload,
                                    bool end_stream);

    SseCodecFilter& parent_filter_;
  };

  std::unique_ptr<SseFilterChainBridge> filter_bridge_;

  std::unique_ptr<http::SseParser> parser_;
  std::unique_ptr<ParserCallbacks> parser_callbacks_;
  std::unique_ptr<EventEncoderImpl> event_encoder_;
  std::unique_ptr<SseCodecStateMachine> state_machine_;

  // Event buffering
  OwnedBuffer event_buffer_;
};

}  // namespace filter
}  // namespace mcp
