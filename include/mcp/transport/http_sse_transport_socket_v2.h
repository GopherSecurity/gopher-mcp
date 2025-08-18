/**
 * @file http_sse_transport_socket_v2.h
 * @brief HTTP+SSE Transport Socket following Envoy's layered architecture
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
 */

#ifndef MCP_TRANSPORT_HTTP_SSE_TRANSPORT_SOCKET_V2_H
#define MCP_TRANSPORT_HTTP_SSE_TRANSPORT_SOCKET_V2_H

#include <memory>
#include <string>
#include <vector>

#include "mcp/buffer.h"
#include "mcp/event/event_loop.h"
#include "mcp/network/transport_socket.h"
#include "mcp/network/filter.h"
#include "mcp/network/connection.h"

namespace mcp {
namespace transport {

/**
 * HTTP+SSE transport socket configuration
 * 
 * This configuration is minimal - only transport-level settings.
 * Protocol configuration happens in the filter chain.
 */
struct HttpSseTransportSocketConfigV2 {
  // Transport mode
  enum class Mode {
    CLIENT,      // Client mode - connects to server
    SERVER       // Server mode - accepts connections
  };
  Mode mode{Mode::CLIENT};
  
  // Underlying transport (for client mode)
  enum class UnderlyingTransport {
    TCP,         // Direct TCP connection
    SSL,         // SSL/TLS over TCP
    STDIO        // Standard I/O pipes
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
 * HTTP+SSE Transport Socket V2
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
class HttpSseTransportSocketV2 : public network::TransportSocket {
public:
  /**
   * Constructor
   * 
   * @param config Transport configuration
   * @param dispatcher Event dispatcher for async operations
   * @param filter_chain Optional filter chain for protocol processing
   */
  HttpSseTransportSocketV2(
      const HttpSseTransportSocketConfigV2& config,
      event::Dispatcher& dispatcher,
      std::unique_ptr<network::FilterChain> filter_chain = nullptr);
  
  ~HttpSseTransportSocketV2() override;
  
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
  
  // ===== Additional Methods =====
  
  /**
   * Set the filter chain for protocol processing
   * Must be called before any I/O operations
   */
  void setFilterChain(std::unique_ptr<network::FilterChain> filter_chain);
  
  /**
   * Get the filter chain
   */
  network::FilterChain* filterChain() { return filter_chain_.get(); }
  
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
  virtual std::unique_ptr<network::TransportSocket> 
  createUnderlyingTransport();
  
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
   * Process data through filter chain
   */
  TransportIoResult processFilterChainRead(Buffer& buffer);
  TransportIoResult processFilterChainWrite(Buffer& buffer, bool end_stream);
  
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
  HttpSseTransportSocketConfigV2 config_;
  
  // Event dispatcher
  event::Dispatcher& dispatcher_;
  
  // Filter chain for protocol processing
  std::unique_ptr<network::FilterChain> filter_chain_;
  
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
 * HTTP+SSE Transport Socket Factory V2
 * 
 * Creates transport sockets with appropriate filter chains
 */
class HttpSseTransportSocketFactoryV2 
    : public network::TransportSocketFactoryBase {
public:
  /**
   * Constructor
   * 
   * @param config Transport configuration
   * @param dispatcher Event dispatcher
   * @param filter_factory Factory for creating filter chains
   */
  HttpSseTransportSocketFactoryV2(
      const HttpSseTransportSocketConfigV2& config,
      event::Dispatcher& dispatcher,
      std::shared_ptr<network::FilterChainFactory> filter_factory = nullptr);
  
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
  HttpSseTransportSocketConfigV2 config_;
  event::Dispatcher& dispatcher_;
  std::shared_ptr<network::FilterChainFactory> filter_factory_;
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
  HttpSseTransportBuilder& withMode(HttpSseTransportSocketConfigV2::Mode mode);
  HttpSseTransportBuilder& withServerAddress(const std::string& address);
  HttpSseTransportBuilder& withSsl(const HttpSseTransportSocketConfigV2::SslConfig& ssl);
  HttpSseTransportBuilder& withConnectTimeout(std::chrono::milliseconds timeout);
  HttpSseTransportBuilder& withIdleTimeout(std::chrono::milliseconds timeout);
  HttpSseTransportBuilder& withHttpFilter(bool is_server);
  HttpSseTransportBuilder& withSseFilter(bool is_server);
  HttpSseTransportBuilder& withCustomFilter(std::shared_ptr<network::FilterFactory> factory);
  
  /**
   * Build the transport socket with configured filter chain
   */
  std::unique_ptr<HttpSseTransportSocketV2> build();
  
  /**
   * Build a transport socket factory
   */
  std::unique_ptr<HttpSseTransportSocketFactoryV2> buildFactory();
  
private:
  event::Dispatcher& dispatcher_;
  HttpSseTransportSocketConfigV2 config_;
  std::vector<std::shared_ptr<network::FilterFactory>> filter_factories_;
};

} // namespace transport
} // namespace mcp

#endif // MCP_TRANSPORT_HTTP_SSE_TRANSPORT_SOCKET_V2_H