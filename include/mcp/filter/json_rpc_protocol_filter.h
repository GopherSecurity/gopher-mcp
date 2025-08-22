#pragma once

#include "mcp/buffer.h"
#include "mcp/event/event_loop.h"
#include "mcp/json/json_bridge.h"
#include "mcp/network/filter.h"
#include "mcp/types.h"

namespace mcp {
namespace filter {

/**
 * MCP JSON-RPC Protocol Filter
 *
 * Following production layered architecture:
 * - This filter handles ONLY JSON-RPC protocol processing
 * - Sits above HTTP/SSE filters in the protocol stack
 * - Does NOT handle transport or HTTP concerns
 *
 * Filter Chain Architecture:
 * ```
 * [TCP Socket] → [HTTP Filter] → [SSE Filter] → [MCP JSON-RPC Filter] →
 * [Application] (Raw I/O)      (HTTP layer)    (SSE layer)     (JSON-RPC layer)
 * (Business logic)
 * ```
 *
 * Responsibilities:
 * - Parse JSON-RPC messages (requests, responses, notifications)
 * - Validate JSON-RPC protocol compliance
 * - Route messages to appropriate callbacks
 * - Generate JSON-RPC formatted responses
 * - Handle protocol-level errors
 *
 */
class JsonRpcProtocolFilter : public network::Filter {
 public:
  /**
   * Message handler for JSON-RPC protocol events
   * Application layer implements this to handle business logic
   */
  class MessageHandler {
   public:
    virtual ~MessageHandler() = default;

    /**
     * Called when a JSON-RPC request is received
     * @param request The parsed request
     */
    virtual void onRequest(const jsonrpc::Request& request) = 0;

    /**
     * Called when a JSON-RPC notification is received
     * @param notification The parsed notification
     */
    virtual void onNotification(const jsonrpc::Notification& notification) = 0;

    /**
     * Called when a JSON-RPC response is received
     * @param response The parsed response
     */
    virtual void onResponse(const jsonrpc::Response& response) = 0;

    /**
     * Called on JSON-RPC protocol error
     * @param error The error details
     */
    virtual void onProtocolError(const Error& error) = 0;
  };

  /**
   * Encoder interface for sending JSON-RPC messages
   * Used by application layer to send messages
   */
  class Encoder {
   public:
    virtual ~Encoder() = default;

    /**
     * Encode and send a JSON-RPC request
     * @param request The request to send
     * @return Success or error
     */
    virtual VoidResult encodeRequest(const jsonrpc::Request& request) = 0;

    /**
     * Encode and send a JSON-RPC notification
     * @param notification The notification to send
     * @return Success or error
     */
    virtual VoidResult encodeNotification(
        const jsonrpc::Notification& notification) = 0;

    /**
     * Encode and send a JSON-RPC response
     * @param response The response to send
     * @return Success or error
     */
    virtual VoidResult encodeResponse(const jsonrpc::Response& response) = 0;
  };

  /**
   * Constructor
   * @param handler Application message handler for protocol events
   * @param dispatcher Event dispatcher for async operations
   * @param is_server True for server mode, false for client mode
   */
  JsonRpcProtocolFilter(MessageHandler& handler,
                   event::Dispatcher& dispatcher,
                   bool is_server);

  ~JsonRpcProtocolFilter()
      override;  // Defined in .cc to avoid incomplete type issues

  // Network filter interface
  network::FilterStatus onData(Buffer& data, bool end_stream) override;
  network::FilterStatus onNewConnection() override;
  network::FilterStatus onWrite(Buffer& data, bool end_stream) override;

  // Filter initialization
  void initializeReadFilterCallbacks(
      network::ReadFilterCallbacks& callbacks) override;
  void initializeWriteFilterCallbacks(
      network::WriteFilterCallbacks& callbacks) override;

  /**
   * Get the encoder for sending messages
   * @return Reference to the encoder
   */
  Encoder& encoder();

  /**
   * Set message framing mode
   * @param use_framing True to use length-prefixed framing
   */
  void setUseFraming(bool use_framing) { use_framing_ = use_framing; }

 private:
  class EncoderImpl;
  friend class EncoderImpl;  // Allow encoder to access private members

  /**
   * Parse messages from buffer
   * @param buffer Data buffer containing JSON messages
   */
  void parseMessages(Buffer& buffer);

  /**
   * Parse a single JSON-RPC message
   * @param json_str JSON string to parse
   * @return True if parsed successfully
   */
  bool parseMessage(const std::string& json_str);

  /**
   * Frame outgoing message with length prefix if needed
   * @param data Buffer containing message to frame
   */
  void frameMessage(Buffer& data);

  MessageHandler& handler_;
  event::Dispatcher& dispatcher_;
  bool is_server_;
  bool use_framing_{false};

  std::string partial_message_;
  std::unique_ptr<EncoderImpl> encoder_;

  // Filter callbacks
  network::WriteFilterCallbacks* write_callbacks_{nullptr};

  // Statistics
  uint64_t requests_received_{0};
  uint64_t responses_received_{0};
  uint64_t notifications_received_{0};
  uint64_t protocol_errors_{0};
};

}  // namespace filter
}  // namespace mcp