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

// Forward declare OpenSSL types
typedef struct ssl_st SSL;
typedef struct bio_st BIO;

namespace mcp {
namespace transport {

/**
 * SSL Transport Socket State Machine
 *
 * State transitions (all in dispatcher thread):
 *
 *     Initial
 *        ↓
 *   Connecting ← (on connect() call)
 *        ↓
 *  Handshaking ← (SSL_do_handshake in progress)
 *     ↙    ↘
 * WantRead  WantWrite ← (waiting for socket I/O)
 *     ↘    ↙
 *  Handshaking ← (resume after I/O ready)
 *        ↓
 *   Connected ← (handshake complete, ready for app data)
 *        ↓
 *  ShuttingDown ← (SSL_shutdown initiated)
 *        ↓
 *     Closed
 *
 * Error transitions:
 * - Any state → Error → Closed (on fatal error)
 * - Handshaking → Initial (on retriable error with reconnect)
 */
enum class SslState {
  Initial,       // Not yet connected
  Connecting,    // TCP connection in progress
  Handshaking,   // SSL handshake in progress
  WantRead,      // Handshake waiting for readable socket
  WantWrite,     // Handshake waiting for writable socket
  Connected,     // SSL established, ready for application data
  ShuttingDown,  // SSL shutdown in progress
  Error,         // Error occurred
  Closed         // Connection closed
};

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
class SslTransportSocket : public network::TransportSocket {
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
  SslState getState() const { return state_; }

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
  bool isSecure() const { return state_ == SslState::Connected; }

 private:
  /**
   * State machine transition
   * Validates transition and updates state
   *
   * @param new_state Target state
   * @return true if transition valid, false otherwise
   *
   * Flow:
   * 1. Validate transition from current state
   * 2. Log state change for debugging
   * 3. Update state atomically
   * 4. Trigger any state-specific actions
   */
  bool transitionState(SslState new_state);

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
  network::PostIoAction doHandshake();

  /**
   * Resume handshake after I/O ready
   * Called when socket becomes readable/writable
   *
   * Flow:
   * 1. Move data between socket and BIO
   * 2. Retry SSL_do_handshake
   * 3. Handle result as in doHandshake()
   */
  void resumeHandshake();

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
   * SSL read operation
   * Read encrypted data from socket and decrypt
   *
   * @param buffer Output buffer for decrypted data
   * @return I/O result
   *
   * Flow:
   * 1. Read from socket into network BIO
   * 2. SSL_read to decrypt data
   * 3. Write decrypted data to buffer
   * 4. Handle SSL errors if any
   */
  TransportIoResult sslRead(Buffer& buffer);

  /**
   * SSL write operation
   * Encrypt data and write to socket
   *
   * @param buffer Input buffer with plaintext data
   * @param end_stream End of stream flag
   * @return I/O result
   *
   * Flow:
   * 1. SSL_write to encrypt data
   * 2. Read encrypted data from network BIO
   * 3. Write to underlying socket
   * 4. Handle SSL errors if any
   */
  TransportIoResult sslWrite(Buffer& buffer, bool end_stream);

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
   * Perform SSL shutdown
   * Graceful SSL connection termination
   *
   * Flow:
   * 1. Call SSL_shutdown
   * 2. Send close_notify alert
   * 3. Wait for peer's close_notify
   * 4. Close underlying connection
   */
  void shutdownSsl();

  /**
   * Handle SSL errors
   * Extract and log SSL error details
   *
   * @param ssl_error SSL error code
   * @param syscall_error System error code
   * @return PostIoAction for next step
   */
  network::PostIoAction handleSslError(int ssl_error, int syscall_error);

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
  std::atomic<SslState> state_{SslState::Initial};  // Current state
  std::string failure_reason_;                      // Last error reason

  // Callbacks
  network::TransportSocketCallbacks* transport_callbacks_{nullptr};
  SslHandshakeCallbacks* handshake_callbacks_{nullptr};

  // Handshake state
  bool handshake_complete_{false};   // Handshake done flag
  event::TimerPtr handshake_timer_;  // Handshake retry timer
  std::chrono::steady_clock::time_point handshake_start_;  // Start time

  // I/O buffers
  std::unique_ptr<Buffer> read_buffer_;   // Temporary read buffer
  std::unique_ptr<Buffer> write_buffer_;  // Temporary write buffer

  // Statistics
  uint64_t bytes_encrypted_{0};     // Total bytes encrypted
  uint64_t bytes_decrypted_{0};     // Total bytes decrypted
  uint32_t handshake_attempts_{0};  // Handshake attempt count

  // Connection info (after handshake)
  std::string peer_cert_info_;       // Peer certificate details
  std::string negotiated_protocol_;  // ALPN protocol
  std::string cipher_suite_;         // Negotiated cipher

  // Flags
  bool shutdown_sent_{false};      // SSL shutdown initiated
  bool shutdown_received_{false};  // Peer shutdown received
};

/**
 * SSL Transport Socket Factory
 *
 * Creates SSL transport sockets with shared context.
 * Can be used for both client and server sockets.
 */
class SslTransportSocketFactory : public network::TransportSocketFactoryBase {
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