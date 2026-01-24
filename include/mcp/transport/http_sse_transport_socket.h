/**
 * @file http_sse_transport_socket.h
 * @brief HTTP+SSE Transport Socket following the layered architecture
 *
 * Design principles:
 * - Transport sockets handle ONLY raw I/O (read/write bytes)
 * - Filters handle ALL protocol processing (HTTP, SSE parsing)
 * - Clear separation between transport and protocol layers
 * - Thread-confined to dispatcher thread for lock-free operation
 * - Uses filter chain for extensibility
 *
 * Layer architecture:
 * 1. TransportSocket (this file) - Raw I/O only
 * 2. FilterChain - Protocol processing
 *    - HttpCodecFilter - HTTP/1.1 parsing and generation
 *    - SseCodecFilter - SSE protocol handling
 * 3. ConnectionManager - Connection lifecycle and MCP protocol
 *
 * CURRENT USAGE STATUS (as of 2024):
 * ====================================
 * HttpSseTransportSocket is NOT actively used in the main production path.
 *
 * Current Architecture:
 * - McpConnectionManager uses RawBufferTransportSocketFactory for HTTP+SSE
 * - Protocol handling is done through filter chains (HttpCodecFilter,
 * HttpSseFilter)
 * - Transport socket only handles raw buffer I/O
 *
 * Active Users:
 * 1. C API (mcp_c_api_connection.cc) - Direct instantiation for C bindings
 * 2. HttpsSseTransportFactory - Wraps with SSL for HTTPS+SSE
 * 3. Test files - Various integration and unit tests
 *
 * Production Path (McpConnectionManager):
 * - Case TransportType::HttpSse → RawBufferTransportSocketFactory
 * - Filter chain handles all HTTP/SSE protocol logic
 * - Transport socket is just a raw I/O layer
 *
 * This follows the production pattern where:
 * - Transport sockets handle only I/O operations
 * - Filters handle all protocol-specific logic
 * - Better separation of concerns and modularity
 *
 * Call Stack for processFilterManagerRead (when used):
 * 1. Event Loop → Dispatcher → ConnectionImpl::onFileEvent(Read)
 * 2. ConnectionImpl::onReadReady() → doRead()
 * 3. ConnectionImpl::doReadFromSocket() → transport_socket_->doRead()
 * 4. HttpSseTransportSocket::doRead() → processFilterManagerRead()
 * 5. processFilterManagerRead() - Currently mostly pass-through
 */

#ifndef MCP_TRANSPORT_HTTP_SSE_TRANSPORT_SOCKET_H
#define MCP_TRANSPORT_HTTP_SSE_TRANSPORT_SOCKET_H

#include <memory>
#include <string>
#include <vector>

#include "mcp/buffer.h"
#include "mcp/event/event_loop.h"
#include "mcp/network/address.h"
#include "mcp/network/connection.h"
#include "mcp/network/filter.h"
#include "mcp/network/transport_socket.h"

namespace mcp {
namespace transport {

/**
 * HTTP+SSE transport socket configuration
 *
 * This configuration is minimal - only transport-level settings.
 * Protocol configuration happens in the filter chain.
 */
struct HttpSseTransportSocketConfig {
  // Transport mode
  enum class Mode {
    CLIENT,  // Client mode - connects to server
    SERVER   // Server mode - accepts connections
  };
  Mode mode{Mode::CLIENT};

  // Underlying transport (for client mode)
  enum class UnderlyingTransport {
    TCP,   // Direct TCP connection
    SSL,   // SSL/TLS over TCP
    STDIO  // Standard I/O pipes
  };
  UnderlyingTransport underlying_transport{UnderlyingTransport::TCP};

  // Server endpoint (client mode only)
  std::string server_address;  // e.g., "127.0.0.1:8080"

  // SSL/TLS configuration (if using SSL transport)
  struct SslConfig {
    bool verify_peer{true};
    optional<std::string> ca_cert_path;
    optional<std::string> client_cert_path;
    optional<std::string> client_key_path;
    optional<std::string> sni_hostname;
    optional<std::vector<std::string>> alpn_protocols;
  };
  optional<SslConfig> ssl_config;

  // Connection timeouts
  std::chrono::milliseconds connect_timeout{30000};
  std::chrono::milliseconds idle_timeout{120000};

  // Buffer limits
  size_t read_buffer_limit{1048576};   // 1MB
  size_t write_buffer_limit{1048576};  // 1MB
};

/**
 * HTTP+SSE Transport Socket
 *
 * This is a clean transport socket that:
 * - Handles ONLY raw I/O operations
 * - Delegates ALL protocol processing to the filter chain
 * - Manages the underlying transport (TCP, SSL, or STDIO)
 * - Provides a uniform interface regardless of underlying transport
 *
 * The transport socket does NOT:
 * - Parse HTTP or SSE protocols
 * - Manage HTTP state machines
 * - Handle reconnection logic (that's in ConnectionManager)
 */
class HttpSseTransportSocket : public network::TransportSocket {
 public:
  /**
   * Constructor
   *
   * @param config Transport configuration
   * @param dispatcher Event dispatcher for async operations
   * @param filter_chain Optional filter chain for protocol processing
   */
  HttpSseTransportSocket(
      const HttpSseTransportSocketConfig& config,
      event::Dispatcher& dispatcher,
      std::unique_ptr<network::FilterManager> filter_manager = nullptr);

  ~HttpSseTransportSocket() override;

  // ===== TransportSocket Interface =====

  /**
   * Set transport callbacks
   * Called by ConnectionImpl to register for I/O events
   */
  void setTransportSocketCallbacks(
      network::TransportSocketCallbacks& callbacks) override;

  /**
   * Get protocol name
   * @return "http+sse" for this transport
   */
  std::string protocol() const override { return "http+sse"; }

  /**
   * Get failure reason if transport failed
   */
  std::string failureReason() const override { return failure_reason_; }

  /**
   * Check if we can flush and close
   * @return true if write buffer is empty
   */
  bool canFlushClose() override;

  /**
   * Initiate connection (client mode)
   * @param socket The socket to connect with
   * @return Success or error result
   */
  VoidResult connect(network::Socket& socket) override;

  /**
   * Close the transport socket
   * @param event The close event type
   */
  void closeSocket(network::ConnectionEvent event) override;

  /**
   * Read data from underlying transport
   * @param buffer Buffer to read into
   * @return Read result with bytes read and action
   */
  TransportIoResult doRead(Buffer& buffer) override;

  /**
   * Write data to underlying transport
   * @param buffer Buffer to write from
   * @param end_stream Whether this is the last write
   * @return Write result with bytes written and action
   */
  TransportIoResult doWrite(Buffer& buffer, bool end_stream) override;

  /**
   * Called when underlying connection is established
   */
  void onConnected() override;

  /**
   * Defer Connected event if underlying transport defers it
   */
  bool defersConnectedEvent() const override {
    return underlying_transport_ &&
           underlying_transport_->defersConnectedEvent();
  }

  // ===== Additional Methods =====

  /**
   * Set the filter manager for protocol processing
   * Must be called before any I/O operations
   */
  void setFilterManager(std::unique_ptr<network::FilterManager> filter_manager);

  /**
   * Get the filter manager
   */
  network::FilterManager* filterManager() { return filter_manager_.get(); }

  /**
   * Check if transport is connected
   */
  bool isConnected() const { return connected_; }

  /**
   * Get transport statistics
   */
  struct Stats {
    uint64_t bytes_sent{0};
    uint64_t bytes_received{0};
    uint64_t connect_attempts{0};
    std::chrono::steady_clock::time_point connect_time;
  };
  const Stats& stats() const { return stats_; }

 protected:
  // ===== Protected Methods for Testing =====

  /**
   * Create underlying transport socket based on configuration
   */
  virtual std::unique_ptr<network::TransportSocket> createUnderlyingTransport();

  /**
   * Handle connection timeout
   */
  virtual void onConnectTimeout();

  /**
   * Handle idle timeout
   */
  virtual void onIdleTimeout();

 private:
  // ===== Private Implementation =====

  /**
   * Initialize the transport based on configuration
   */
  void initialize();

  /**
   * Process data through filter manager
   */
  TransportIoResult processFilterManagerRead(Buffer& buffer);
  TransportIoResult processFilterManagerWrite(Buffer& buffer, bool end_stream);

  /**
   * Handle underlying transport events
   */
  void handleUnderlyingConnect();
  void handleUnderlyingClose(network::ConnectionEvent event);
  void handleUnderlyingError(const std::string& error);

  /**
   * Timer management
   */
  void startConnectTimer();
  void cancelConnectTimer();
  void startIdleTimer();
  void resetIdleTimer();
  void cancelIdleTimer();

  /**
   * Assert we're in dispatcher thread
   */
  void assertInDispatcherThread() const {
    // TODO: Implement when dispatcher supports thread checking
  }

  // ===== Member Variables =====

  // Configuration
  HttpSseTransportSocketConfig config_;

  // Event dispatcher
  event::Dispatcher& dispatcher_;

  // Filter manager for protocol processing
  std::unique_ptr<network::FilterManager> filter_manager_;

  // Underlying transport socket (TCP, SSL, or STDIO)
  std::unique_ptr<network::TransportSocket> underlying_transport_;

  // Callbacks from ConnectionImpl
  network::TransportSocketCallbacks* callbacks_{nullptr};

  // Connection state
  bool connected_{false};
  bool connecting_{false};
  bool closing_{false};
  std::string failure_reason_;

  // Buffers
  OwnedBuffer read_buffer_;
  OwnedBuffer write_buffer_;

  // Timers
  event::TimerPtr connect_timer_;
  event::TimerPtr idle_timer_;

  // Statistics
  Stats stats_;

  // Last activity time for idle timeout
  std::chrono::steady_clock::time_point last_activity_time_;
};

/**
 * HTTP+SSE Transport Socket Factory
 *
 * Creates transport sockets with appropriate filter chains
 */
class HttpSseTransportSocketFactory
    : public network::TransportSocketFactoryBase {
 public:
  /**
   * Constructor
   *
   * @param config Transport configuration
   * @param dispatcher Event dispatcher
   */
  HttpSseTransportSocketFactory(const HttpSseTransportSocketConfig& config,
                                event::Dispatcher& dispatcher);

  // ===== TransportSocketFactoryBase Interface =====

  bool implementsSecureTransport() const override;
  std::string name() const override { return "http+sse-v2"; }

  /**
   * Create a transport socket
   *
   * @return New transport socket with filter chain
   */
  network::TransportSocketPtr createTransportSocket() const;

  /**
   * Create a transport socket with options
   *
   * @param options Transport socket options
   * @return New transport socket with filter chain
   */
  network::TransportSocketPtr createTransportSocket(
      network::TransportSocketOptionsSharedPtr options) const;

 private:
  HttpSseTransportSocketConfig config_;
  event::Dispatcher& dispatcher_;
};

/**
 * Builder for HTTP+SSE transport with filter chain
 *
 * This builder creates a properly configured transport socket
 * with the appropriate filter chain for HTTP+SSE processing.
 */
class HttpSseTransportBuilder {
 public:
  HttpSseTransportBuilder(event::Dispatcher& dispatcher)
      : dispatcher_(dispatcher) {}

  // Configuration methods
  HttpSseTransportBuilder& withMode(HttpSseTransportSocketConfig::Mode mode);
  HttpSseTransportBuilder& withServerAddress(const std::string& address);
  HttpSseTransportBuilder& withSsl(
      const HttpSseTransportSocketConfig::SslConfig& ssl);
  HttpSseTransportBuilder& withConnectTimeout(
      std::chrono::milliseconds timeout);
  HttpSseTransportBuilder& withIdleTimeout(std::chrono::milliseconds timeout);
  HttpSseTransportBuilder& withHttpFilter(bool is_server);
  HttpSseTransportBuilder& withSseFilter(bool is_server);

  /**
   * Build the transport socket with configured filter chain
   */
  std::unique_ptr<HttpSseTransportSocket> build();

  /**
   * Build a transport socket factory
   */
  std::unique_ptr<HttpSseTransportSocketFactory> buildFactory();

 private:
  event::Dispatcher& dispatcher_;
  HttpSseTransportSocketConfig config_;
  bool add_http_filter_{false};
  bool add_sse_filter_{false};
  bool is_server_{false};
};

}  // namespace transport
}  // namespace mcp

#endif  // MCP_TRANSPORT_HTTP_SSE_TRANSPORT_SOCKET_H