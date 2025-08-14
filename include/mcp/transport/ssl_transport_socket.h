/**
 * @file ssl_transport_socket.h
 * @brief SSL/TLS transport socket wrapper following good architecture
 *
 * This provides SSL/TLS transport layer with:
 * - Layered transport socket design (wraps any transport)
 * - Async SSL handshake in dispatcher thread
 * - Robust state machine for SSL lifecycle
 * - Non-blocking I/O operations
 * - Clean separation from application protocols
 *
 * Key design principles we follow:
 * - All SSL operations in dispatcher thread context
 * - State machine for clear lifecycle management
 * - Async handshake without blocking dispatcher
 * - Reusable with any underlying transport
 */

#ifndef MCP_TRANSPORT_SSL_TRANSPORT_SOCKET_H
#define MCP_TRANSPORT_SSL_TRANSPORT_SOCKET_H

#include <atomic>
#include <chrono>
#include <memory>
#include <queue>
#include <string>

#include "mcp/buffer.h"
#include "mcp/event/event_loop.h"
#include "mcp/network/transport_socket.h"
#include "mcp/transport/ssl_context.h"
#include "mcp/transport/ssl_state_machine.h"

// Forward declare OpenSSL types
typedef struct ssl_st SSL;
typedef struct bio_st BIO;
typedef struct x509_st X509;

namespace mcp {
namespace transport {

// Use SslSocketState from state machine

/**
 * SSL handshake callbacks interface
 * Allows upper layers to be notified of handshake events
 */
class SslHandshakeCallbacks {
 public:
  virtual ~SslHandshakeCallbacks() = default;

  /**
   * Called when SSL handshake completes successfully
   * Invoked in dispatcher thread context
   */
  virtual void onSslHandshakeComplete() = 0;

  /**
   * Called when SSL handshake fails
   * Invoked in dispatcher thread context
   *
   * @param reason Failure reason string
   */
  virtual void onSslHandshakeFailed(const std::string& reason) = 0;
};

/**
 * SSL Transport Socket
 *
 * Wraps an underlying transport socket to provide SSL/TLS encryption.
 * Implements some good design patterns:
 * - Layered transport (SSL over any transport)
 * - Async operations in dispatcher thread
 * - State machine for lifecycle management
 * - Clean separation from protocols
 *
 * Thread model:
 * - All methods must be called from dispatcher thread
 * - SSL operations are non-blocking
 * - Callbacks are invoked in dispatcher thread
 */
class SslTransportSocket
    : public network::TransportSocket,
      public std::enable_shared_from_this<SslTransportSocket> {
 public:
  /**
   * Initial SSL role
   */
  enum class InitialRole {
    Client,  // Initiate SSL handshake as client
    Server   // Accept SSL handshake as server
  };

  /**
   * Create SSL transport socket
   *
   * @param inner_socket Underlying transport socket (owned)
   * @param ssl_context Shared SSL context
   * @param role Initial role (client or server)
   * @param dispatcher Event dispatcher for async operations
   *
   * Note: Takes ownership of inner_socket
   */
  SslTransportSocket(network::TransportSocketPtr inner_socket,
                     SslContextSharedPtr ssl_context,
                     InitialRole role,
                     event::Dispatcher& dispatcher);

  ~SslTransportSocket() override;

  // TransportSocket interface
  void setTransportSocketCallbacks(
      network::TransportSocketCallbacks& callbacks) override;
  std::string protocol() const override;
  std::string failureReason() const override { return failure_reason_; }
  bool canFlushClose() override;
  VoidResult connect(network::Socket& socket) override;
  void closeSocket(network::ConnectionEvent event) override;
  TransportIoResult doRead(Buffer& buffer) override;
  TransportIoResult doWrite(Buffer& buffer, bool end_stream) override;
  void onConnected() override;

  /**
   * Register handshake callbacks
   * Must be called before connect() if handshake notifications needed
   */
  void setHandshakeCallbacks(SslHandshakeCallbacks* callbacks) {
    handshake_callbacks_ = callbacks;
  }

  /**
   * Get current SSL state
   * Useful for debugging and testing
   */
  SslSocketState getState() const { return state_machine_->getCurrentState(); }

  /**
   * Get peer certificate info after handshake
   * Returns empty string if not connected or no peer cert
   */
  std::string getPeerCertificateInfo() const;

  /**
   * Get negotiated protocol (via ALPN)
   * Returns empty string if no protocol negotiated
   */
  std::string getNegotiatedProtocol() const;

  /**
   * Check if connection is secure (SSL established)
   */
  bool isSecure() const { return state_machine_->isConnected(); }

  /**
   * SSL Statistics structure
   */
  struct SslStats {
    uint64_t handshakes_started;
    uint64_t handshakes_completed;
    uint64_t handshakes_failed;
    uint64_t sessions_reused;
    uint64_t bytes_encrypted;
    uint64_t bytes_decrypted;
  };

  /**
   * Get SSL statistics
   */
  SslStats getStatistics() const;

  /**
   * Get Subject Alternative Names from peer certificate
   */
  std::vector<std::string> getSubjectAltNames() const;

  /**
   * Get cipher suite used for the connection
   */
  std::string getCipherSuite() const;

  /**
   * Get TLS version
   */
  std::string getTlsVersion() const;

 private:
  // Forward declare Stats struct for implementation
  struct Stats;

  /**
   * Initialize SSL connection
   * Creates SSL object and BIOs
   * Called when underlying connection is ready
   *
   * Flow:
   * 1. Create SSL from context
   * 2. Create memory BIOs for I/O
   * 3. Set SSL to client or server mode
   * 4. Configure SNI if client
   * 5. Transition to Handshaking state
   */
  VoidResult initializeSsl();

  /**
   * Perform SSL handshake
   * Non-blocking handshake operation
   *
   * @return PostIoAction indicating next action
   *
   * Flow:
   * 1. Call SSL_do_handshake
   * 2. Check return value:
   *    - Success: Transition to Connected
   *    - Want Read: Transition to WantRead
   *    - Want Write: Transition to WantWrite
   *    - Error: Handle error and close
   * 3. Schedule next action if needed
   */
  TransportIoResult::PostIoAction doHandshake();

  /**
   * Handle handshake completion
   * Called when handshake succeeds
   *
   * Flow:
   * 1. Verify peer certificate
   * 2. Extract connection info
   * 3. Notify callbacks
   * 4. Transition to Connected state
   */
  void onHandshakeComplete();

  /**
   * Handle handshake failure
   * Called when handshake fails
   *
   * @param reason Failure reason
   *
   * Flow:
   * 1. Extract error details from SSL
   * 2. Log error for debugging
   * 3. Notify callbacks
   * 4. Transition to Error state
   * 5. Close connection
   */
  void onHandshakeFailed(const std::string& reason);

  /**
   * Move data from socket to network BIO
   * Called during handshake and I/O operations
   *
   * @return Bytes moved
   */
  size_t moveToBio();

  /**
   * Move data from network BIO to socket
   * Called during handshake and I/O operations
   *
   * @return Bytes moved
   */
  size_t moveFromBio();

  /**
   * Schedule handshake retry
   * Used when handshake needs I/O
   */
  void scheduleHandshakeRetry();

 private:
  // Configuration and context
  network::TransportSocketPtr inner_socket_;  // Underlying transport (owned)
  SslContextSharedPtr ssl_context_;           // Shared SSL context
  InitialRole initial_role_;                  // Client or server role
  event::Dispatcher& dispatcher_;             // Event dispatcher

  // SSL objects
  SSL* ssl_{nullptr};           // SSL connection (owned)
  BIO* network_bio_{nullptr};   // Network BIO for encrypted I/O
  BIO* internal_bio_{nullptr};  // Internal BIO for plaintext

  // State machine
  std::unique_ptr<SslStateMachine> state_machine_;  // Robust state machine
  std::string failure_reason_;                      // Last error reason

  // Callbacks
  network::TransportSocketCallbacks* transport_callbacks_{nullptr};
  SslHandshakeCallbacks* handshake_callbacks_{nullptr};

  // Handshake state
  bool handshake_complete_{false};   // Handshake done flag
  event::TimerPtr handshake_timer_;  // Handshake retry timer
  std::chrono::steady_clock::time_point handshake_start_;  // Start time
  uint32_t state_listener_id_{0};  // State machine listener ID

  // I/O buffers
  std::unique_ptr<Buffer> read_buffer_;   // Temporary read buffer
  std::unique_ptr<Buffer> write_buffer_;  // Temporary write buffer

  // Statistics
  uint64_t bytes_encrypted_{0};     // Total bytes encrypted
  uint64_t bytes_decrypted_{0};     // Total bytes decrypted
  uint32_t handshake_attempts_{0};  // Handshake attempt count

  // Connection info (after handshake)
  std::string peer_cert_info_;                  // Peer certificate details
  std::string negotiated_protocol_;             // ALPN protocol
  std::string cipher_suite_;                    // Negotiated cipher
  std::string tls_version_;                     // TLS version
  std::vector<std::string> subject_alt_names_;  // SANs from peer cert

  // Flags
  bool shutdown_sent_{false};      // SSL shutdown initiated
  bool shutdown_received_{false};  // Peer shutdown received

  // Performance optimizations
  std::unique_ptr<Stats> stats_;           // SSL statistics
  event::TimerPtr handshake_retry_timer_;  // Retry timer with backoff
  uint32_t retry_count_{0};                // Retry count for backoff

  /**
   * State change handler
   * Called when state machine transitions
   */
  void onStateChanged(SslSocketState old_state, SslSocketState new_state);

  /**
   * Configure SSL for client mode
   */
  void configureClientSsl();

  /**
   * Configure SSL for server mode
   */
  void configureServerSsl();

  /**
   * Configure client state machine with appropriate actions
   */
  void configureClientStateMachine();

  /**
   * Configure server state machine with appropriate actions
   */
  void configureServerStateMachine();

  /**
   * Initiate client handshake process
   */
  void initiateClientHandshake();

  /**
   * Initiate server handshake process
   */
  void initiateServerHandshake();

  /**
   * Perform a handshake step
   */
  void performHandshakeStep();

  /**
   * Handle handshake result based on SSL error (returns PostIoAction)
   */
  TransportIoResult::PostIoAction handleHandshakeResult(int ssl_error);

  /**
   * Extract connection information after handshake
   */
  void extractConnectionInfo();

  /**
   * Verify peer certificate
   */
  bool verifyPeerCertificate();

  /**
   * Cancel ongoing handshake
   */
  void cancelHandshake();

  /**
   * Perform optimized SSL read operation
   */
  TransportIoResult performOptimizedSslRead(Buffer& buffer);

  /**
   * Perform optimized SSL write operation
   */
  TransportIoResult performOptimizedSslWrite(Buffer& buffer, bool end_stream);

  /**
   * Initiate SSL shutdown sequence
   */
  void initiateShutdown();

  /**
   * Schedule periodic check for shutdown completion
   */
  void scheduleShutdownCheck();

  /**
   * Schedule wait for I/O readiness
   */
  void scheduleIoWait(bool wait_for_read, bool wait_for_write);

  /**
   * Handle SSL error
   */
  void handleSslError(const std::string& reason);

  /**
   * Flush buffered writes after handshake
   */
  void flushBufferedWrites();

  /**
   * Extract Subject Alternative Names from certificate
   */
  void extractSubjectAltNames(X509* cert);

  /**
   * Handle handshake timeout
   */
  void onHandshakeTimeout();

  /**
   * Log final statistics for debugging
   */
  void logFinalStatistics();
};

/**
 * SSL Transport Socket Factory
 *
 * Creates SSL transport sockets with shared context.
 * Can be used for both client and server sockets.
 */
class SslTransportSocketFactory : public network::ClientTransportSocketFactory {
 public:
  /**
   * Create SSL transport socket factory
   *
   * @param inner_factory Factory for underlying transport
   * @param ssl_config SSL configuration
   * @param dispatcher Event dispatcher
   */
  SslTransportSocketFactory(
      std::unique_ptr<network::TransportSocketFactoryBase> inner_factory,
      const SslContextConfig& ssl_config,
      event::Dispatcher& dispatcher);

  // TransportSocketFactoryBase interface
  bool implementsSecureTransport() const override { return true; }
  std::string name() const override { return "ssl"; }

  /**
   * Create SSL transport socket
   * Wraps socket from inner factory with SSL
   */
  network::TransportSocketPtr createTransportSocket(
      network::TransportSocketOptionsSharedPtr options) const override;

  // Additional ClientTransportSocketFactory methods
  bool supportsAlpn() const override { return true; }
  std::string defaultServerNameIndication() const override { return ""; }
  void hashKey(
      std::vector<uint8_t>& key,
      network::TransportSocketOptionsSharedPtr options) const override {
    // TODO: Hash SSL context configuration
    // TODO: In production, would hash cert paths, protocols, etc.
  }

 private:
  std::unique_ptr<network::TransportSocketFactoryBase> inner_factory_;
  SslContextSharedPtr ssl_context_;
  event::Dispatcher& dispatcher_;
  SslTransportSocket::InitialRole role_;
};

/**
 * Create SSL transport socket factory
 * Helper function for factory creation
 */
inline std::unique_ptr<SslTransportSocketFactory>
createSslTransportSocketFactory(
    std::unique_ptr<network::TransportSocketFactoryBase> inner_factory,
    const SslContextConfig& ssl_config,
    event::Dispatcher& dispatcher) {
  return std::make_unique<SslTransportSocketFactory>(std::move(inner_factory),
                                                     ssl_config, dispatcher);
}

}  // namespace transport
}  // namespace mcp

#endif  // MCP_TRANSPORT_SSL_TRANSPORT_SOCKET_H