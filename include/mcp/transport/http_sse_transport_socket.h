#ifndef MCP_TRANSPORT_HTTP_SSE_TRANSPORT_SOCKET_H
#define MCP_TRANSPORT_HTTP_SSE_TRANSPORT_SOCKET_H

#include <atomic>
#include <memory>
#include <queue>
#include <string>

#include "mcp/buffer.h"
#include "mcp/event/event_loop.h"
#include "mcp/http/http_parser.h"
#include "mcp/http/sse_parser.h"
#include "mcp/network/transport_socket.h"
#include "mcp/transport/http_sse_state_machine.h"

namespace mcp {
namespace transport {

/**
 * HTTP+SSE transport socket configuration
 */
struct HttpSseTransportSocketConfig {
  // HTTP endpoint URL
  std::string endpoint_url;

  // HTTP method (default POST for requests, GET for SSE stream)
  std::string request_method{"POST"};
  std::string sse_method{"GET"};

  // HTTP headers
  std::map<std::string, std::string> headers;

  // Connection settings
  std::chrono::milliseconds connect_timeout{30000};
  std::chrono::milliseconds request_timeout{60000};
  std::chrono::milliseconds keepalive_interval{30000};
  bool enable_keepalive{true};

  // HTTP parser settings
  size_t max_header_size{8192};
  size_t max_body_size{10485760};  // 10MB default

  // SSE settings
  std::string sse_endpoint_path{"/events"};   // Path for SSE stream
  std::string request_endpoint_path{"/rpc"};  // Path for JSON-RPC requests
  bool auto_reconnect{true};
  std::chrono::milliseconds reconnect_delay{3000};

  // TLS/SSL configuration (if using HTTPS)
  bool use_ssl{false};  // Auto-detected from URL, or forced
  bool verify_ssl{true};
  optional<std::string> ca_cert_path;
  optional<std::string> client_cert_path;
  optional<std::string> client_key_path;
  optional<std::string> sni_hostname;                 // Server Name Indication
  optional<std::vector<std::string>> alpn_protocols;  // ALPN protocols

  // HTTP version preference
  http::HttpVersion preferred_version{http::HttpVersion::HTTP_1_1};

  // Parser factory (allows custom parser implementation)
  std::shared_ptr<http::HttpParserFactory> parser_factory;
};

/**
 * HTTP+SSE transport socket with llhttp integration
 *
 * Implements bidirectional JSON-RPC over HTTP with Server-Sent Events
 * using proper HTTP parsing via llhttp and event-driven architecture
 */
class HttpSseTransportSocket : public network::TransportSocket,
                               public http::HttpParserCallbacks,
                               public http::SseParserCallbacks {
 public:
  explicit HttpSseTransportSocket(const HttpSseTransportSocketConfig& config,
                                  event::Dispatcher& dispatcher,
                                  bool is_server_mode = false);
  ~HttpSseTransportSocket() override;

  // TransportSocket interface
  void setTransportSocketCallbacks(
      network::TransportSocketCallbacks& callbacks) override;
  std::string protocol() const override;
  std::string failureReason() const override;
  bool canFlushClose() override;
  VoidResult connect(network::Socket& socket) override;
  void closeSocket(network::ConnectionEvent event) override;
  TransportIoResult doRead(Buffer& buffer) override;
  TransportIoResult doWrite(Buffer& buffer, bool end_stream) override;
  void onConnected() override;

  // HttpParserCallbacks interface
  http::ParserCallbackResult onMessageBegin() override;
  http::ParserCallbackResult onUrl(const char* data, size_t length) override;
  http::ParserCallbackResult onStatus(const char* data, size_t length) override;
  http::ParserCallbackResult onHeaderField(const char* data,
                                           size_t length) override;
  http::ParserCallbackResult onHeaderValue(const char* data,
                                           size_t length) override;
  http::ParserCallbackResult onHeadersComplete() override;
  http::ParserCallbackResult onBody(const char* data, size_t length) override;
  http::ParserCallbackResult onMessageComplete() override;
  http::ParserCallbackResult onChunkHeader(size_t length) override;
  http::ParserCallbackResult onChunkComplete() override;
  void onError(const std::string& error) override;

  // SseParserCallbacks interface
  void onSseEvent(const http::SseEvent& event) override;
  void onSseComment(const std::string& comment) override;
  void onSseError(const std::string& error) override;

 private:
  // Request context
  struct PendingRequest {
    std::string id;
    std::string body;
    event::TimerPtr timeout_timer;
    std::chrono::steady_clock::time_point sent_time;
  };

  // State machine configuration
  void configureStateMachine();
  void configureStateEntryActions();
  void configureStateExitActions();
  void configureStateValidators();
  void configureStateTimeouts();
  void configureReconnectionStrategy();

  // Buffer management
  void initializeBuffers();
  void initializeParsers();

  // Data processing
  void processReceivedData();
  void processHttpData();
  void processSseData();

  // HTTP protocol handling
  void initiateHttpHandshake();
  void sendInitialHttpRequest();
  void sendHttpRequest(const std::string& body, const std::string& path);
  void handleHttpResponse();
  void handleSuccessfulHttpResponse();
  void handleErrorHttpResponse();
  void handleHttpRequest();
  void processHttpResponse();
  void processHttpRequest();
  void processIncomingRequest();
  void prepareHttpResponse();
  void prepareSseResponse();
  void prepareRegularHttpResponse();
  std::string buildHttpRequest(const std::string& method,
                               const std::string& path,
                               const std::string& body);

  // SSE stream handling
  void establishSseStream();
  void handleSseStreamActive();
  void notifySseStreamEstablished();
  void sendSseConnectRequest();
  void sendHttpUpgradeRequest();
  void sendSseResponse();
  void handleSseEvent(const http::SseEvent& event);

  // State handling
  void onStateChanged(HttpSseState old_state, HttpSseState new_state);
  void handleTcpConnected();
  void handleHttpRequestSent();
  void handleErrorState();
  void handleClosedState();
  void updateState(HttpSseState new_state);

  // Timer management
  void startConnectTimer();
  void cancelConnectTimer();
  void startKeepAliveTimer();
  void cancelKeepAliveTimer();
  void startRequestTimer(const std::string& request_id);
  void scheduleReconnectTimer();
  void sendKeepAlive();

  // Error handling
  void handleConnectionError(const std::string& error);
  void handleParseError(const std::string& error);
  void handleConnectTimeout();
  void handleRequestTimeout(const std::string& request_id);

  // Reconnection logic
  void scheduleReconnect();
  void attemptReconnect();

  // Request management
  void flushPendingRequests();

  // Helper methods
  bool isInCriticalOperation() const;
  TransportIoResult::PostIoAction determineReadAction() const;
  TransportIoResult::PostIoAction determineWriteAction() const;
  void handleEndStream();
  void deliverHttpResponse();
  void cleanupActiveStreams();
  void notifyStateChange(HttpSseState old_state, HttpSseState new_state);
  static std::string extractHostFromUrl(const std::string& url);
  void logStateTransition(HttpSseState old_state, HttpSseState new_state);
  void logRequestTimeout(const std::string& request_id);

  // Configuration
  HttpSseTransportSocketConfig config_;

  // Event loop and dispatcher
  event::Dispatcher& dispatcher_;

  // State machine
  std::unique_ptr<HttpSseStateMachine> state_machine_;
  std::unique_ptr<HttpSseTransitionCoordinator> transition_coordinator_;

  // State callbacks
  network::TransportSocketCallbacks* callbacks_{nullptr};
  std::string failure_reason_;
  
  // Connection state flags for managing complex lifecycle
  bool is_server_mode_{false};           // Track if this is a server-side socket (set once in constructor)
  std::atomic<bool> shutting_down_{false}; // THREAD-SAFE: Prevents callbacks during destruction
                                          // Set atomically in destructor (any thread) to avoid pure virtual calls
                                          // Read in dispatcher thread during callback execution
  std::atomic<bool> on_connected_called_{false}; // THREAD-SAFE IDEMPOTENCY: Ensures onConnected() is called once
                                                  // Prevents duplicate state transitions from multiple threads/layers
                                                  // (McpConnectionManager, stdio, server accept, worker threads)

  // HTTP parsers
  std::unique_ptr<http::HttpParser> request_parser_;
  std::unique_ptr<http::HttpParser> response_parser_;
  std::unique_ptr<http::SseParser> sse_parser_;

  // Current HTTP message being parsed (simplified for callback-based parsing)
  std::string current_header_field_;
  std::string current_header_value_;
  std::string accumulated_url_;  // Accumulate URL during parsing
  bool processing_headers_{false};

  // Request data (server mode)
  std::map<std::string, std::string> current_request_headers_;
  std::string current_request_body_;
  std::string current_request_method_;
  std::string current_request_url_;

  // Response data (client mode)
  std::map<std::string, std::string> current_response_headers_;
  std::string current_response_body_;
  int current_response_status_{0};

  // Buffers
  std::unique_ptr<Buffer> read_buffer_;
  std::unique_ptr<Buffer> write_buffer_;
  std::unique_ptr<Buffer> sse_buffer_;

  // Request tracking
  std::queue<PendingRequest> pending_requests_;
  std::map<std::string, PendingRequest> active_requests_;
  uint64_t next_request_id_{1};

  // Pending write data queue for immediate sends
  std::queue<std::string> pending_write_data_;

  // Timers
  event::TimerPtr keepalive_timer_;
  event::TimerPtr reconnect_timer_;

  // Metrics
  uint64_t bytes_sent_{0};
  uint64_t bytes_received_{0};
  uint64_t requests_sent_{0};
  uint64_t responses_received_{0};
  uint64_t requests_received_{0};
  uint64_t sse_events_received_{0};

  // Connection info
  bool sse_stream_active_{false};
  std::string session_id_;
  std::chrono::steady_clock::time_point connect_time_;
  network::ConnectionEvent connection_close_event_;

  // Watermark tracking
  size_t read_buffer_low_watermark_;
  size_t read_buffer_high_watermark_;
  size_t write_buffer_low_watermark_;
  size_t write_buffer_high_watermark_;

  // Additional timers
  event::TimerPtr connect_timer_;
  std::chrono::milliseconds connect_timeout_;
  std::chrono::milliseconds request_timeout_;
  std::chrono::milliseconds keepalive_interval_;
};

/**
 * HTTP+SSE transport socket factory with llhttp
 */
class HttpSseTransportSocketFactory
    : public network::ClientTransportSocketFactory,
      public network::ServerTransportSocketFactory {
 public:
  HttpSseTransportSocketFactory(const HttpSseTransportSocketConfig& config,
                                event::Dispatcher& dispatcher);

  // TransportSocketFactoryBase interface
  bool implementsSecureTransport() const override;
  std::string name() const override { return "http+sse"; }

  // ClientTransportSocketFactory interface
  network::TransportSocketPtr createTransportSocket(
      network::TransportSocketOptionsSharedPtr options) const override;
  bool supportsAlpn() const override { return true; }
  std::string defaultServerNameIndication() const override;
  void hashKey(std::vector<uint8_t>& key,
               network::TransportSocketOptionsSharedPtr options) const override;

  // ServerTransportSocketFactory interface
  network::TransportSocketPtr createTransportSocket() const override;

 private:
  HttpSseTransportSocketConfig config_;
  event::Dispatcher& dispatcher_;
};

/**
 * Create an HTTP+SSE transport socket factory with llhttp
 */
inline std::unique_ptr<network::TransportSocketFactoryBase>
createHttpSseTransportSocketFactory(const HttpSseTransportSocketConfig& config,
                                    event::Dispatcher& dispatcher) {
  return std::make_unique<HttpSseTransportSocketFactory>(config, dispatcher);
}

}  // namespace transport
}  // namespace mcp

#endif  // MCP_TRANSPORT_HTTP_SSE_TRANSPORT_SOCKET_H