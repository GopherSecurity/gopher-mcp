#ifndef MCP_NETWORK_TRANSPORT_SOCKET_H
#define MCP_NETWORK_TRANSPORT_SOCKET_H

#include <memory>
#include <string>
#include <vector>

#include "mcp/buffer.h"
#include "mcp/network/io_handle.h"
#include "mcp/network/socket.h"
#include "mcp/optional.h"
#include "mcp/result.h"
#include "mcp/types.h"

namespace mcp {
namespace network {

// Forward declarations
class Connection;
class TransportSocket;
class TransportSocketFactory;
class TransportSocketOptions;
class TransportSocketCallbacks;

using TransportSocketPtr = std::unique_ptr<TransportSocket>;
using TransportSocketSharedPtr = std::shared_ptr<TransportSocket>;
using TransportSocketFactoryPtr = std::unique_ptr<TransportSocketFactory>;
using TransportSocketFactorySharedPtr = std::shared_ptr<TransportSocketFactory>;
using TransportSocketOptionsSharedPtr = std::shared_ptr<const TransportSocketOptions>;

/**
 * Post-I/O action to take after transport socket operations
 */
enum class PostIoAction {
  Continue,      // Continue with further I/O operations
  StopIteration, // Stop current iteration, wait for more data
  Close          // Close the connection
};

/**
 * Result of transport socket I/O operations
 */
struct IoResult {
  PostIoAction action_;
  uint64_t bytes_processed_;
  bool end_stream_read_;
  optional<Error> error_;

  IoResult(PostIoAction action, uint64_t bytes_processed, bool end_stream_read)
      : action_(action), bytes_processed_(bytes_processed), end_stream_read_(end_stream_read) {}

  IoResult(PostIoAction action, uint64_t bytes_processed, bool end_stream_read, const Error& error)
      : action_(action), bytes_processed_(bytes_processed), end_stream_read_(end_stream_read), error_(error) {}

  static IoResult success(uint64_t bytes_processed = 0, bool end_stream = false) {
    return IoResult(PostIoAction::Continue, bytes_processed, end_stream);
  }

  static IoResult stop() {
    return IoResult(PostIoAction::StopIteration, 0, false);
  }

  static IoResult close() {
    return IoResult(PostIoAction::Close, 0, false);
  }

  static IoResult error(const Error& err) {
    return IoResult(PostIoAction::Close, 0, false, err);
  }
};

/**
 * Connection close types
 */
enum class ConnectionCloseType {
  FlushWrite,         // Flush pending write data before closing
  NoFlush,            // Close immediately without flushing
  FlushWriteAndDelay, // Flush and delay close
  Abort,              // Abort immediately
  AbortReset          // Abort with RST
};

/**
 * Connection events
 */
enum class ConnectionEvent {
  RemoteClose,
  LocalClose,
  Connected,
  ConnectedZeroRtt
};

/**
 * Transport socket callbacks interface
 * 
 * Provides access to connection resources and event handling
 */
class TransportSocketCallbacks {
public:
  virtual ~TransportSocketCallbacks() = default;

  /**
   * Get the I/O handle for the underlying socket
   */
  virtual IoHandle& ioHandle() = 0;
  virtual const IoHandle& ioHandle() const = 0;

  /**
   * Get the connection that owns this transport socket
   */
  virtual Connection& connection() = 0;

  /**
   * Check if the read buffer should be drained for flow control
   */
  virtual bool shouldDrainReadBuffer() = 0;

  /**
   * Mark the transport socket as readable (edge-triggered mode)
   */
  virtual void setTransportSocketIsReadable() = 0;

  /**
   * Raise a connection event
   */
  virtual void raiseEvent(ConnectionEvent event) = 0;

  /**
   * Flush the write buffer if not empty
   */
  virtual void flushWriteBuffer() = 0;
};

/**
 * SSL/TLS connection information
 */
struct SslConnectionInfo {
  std::string protocol;           // TLS version (e.g., "TLSv1.3")
  std::string cipher_suite;       // Cipher suite name
  std::string peer_certificate;   // PEM-encoded peer certificate
  std::string alpn_protocol;      // Negotiated ALPN protocol
  std::string server_name;        // SNI server name
  bool session_reused{false};     // Whether session was reused
  uint64_t session_id{0};         // Session ID for resumption
};

using SslConnectionInfoConstSharedPtr = std::shared_ptr<const SslConnectionInfo>;

/**
 * Transport socket interface
 * 
 * Abstracts the actual network I/O and security layer (TLS, plaintext, etc.)
 */
class TransportSocket {
public:
  virtual ~TransportSocket() = default;

  /**
   * Set the callbacks used to interact with the connection
   * Called once during initialization
   */
  virtual void setTransportSocketCallbacks(TransportSocketCallbacks& callbacks) = 0;

  /**
   * Get the negotiated application protocol (e.g., from ALPN)
   */
  virtual std::string protocol() const = 0;

  /**
   * Get the failure reason if the transport socket failed
   */
  virtual absl::string_view failureReason() const = 0;

  /**
   * Check if the socket can flush data and close
   */
  virtual bool canFlushClose() = 0;

  /**
   * Establish connection (client mode)
   */
  virtual Result<void> connect(Socket& socket) = 0;

  /**
   * Close the transport socket
   */
  virtual void closeSocket(ConnectionEvent event) = 0;

  /**
   * Read data from the transport
   * Buffer is filled with decrypted/processed data
   */
  virtual IoResult doRead(Buffer& buffer) = 0;

  /**
   * Write data to the transport
   * Buffer contains plaintext data to be encrypted/processed
   */
  virtual IoResult doWrite(Buffer& buffer, bool end_stream) = 0;

  /**
   * Called when the underlying transport is connected
   */
  virtual void onConnected() = 0;

  /**
   * Get SSL connection information (if applicable)
   */
  virtual SslConnectionInfoConstSharedPtr ssl() const { return nullptr; }

  /**
   * Start secure transport (e.g., STARTTLS)
   */
  virtual bool startSecureTransport() { return false; }

  /**
   * Configure initial congestion window
   */
  virtual void configureInitialCongestionWindow(uint64_t bandwidth_bits_per_sec,
                                                std::chrono::microseconds rtt) {
    (void)bandwidth_bits_per_sec;
    (void)rtt;
  }

  /**
   * Enable/disable TCP keep-alive
   */
  virtual void enableTcpKeepalive() {}
};

/**
 * Transport socket options
 * 
 * Configuration passed during transport socket creation
 */
class TransportSocketOptions {
public:
  virtual ~TransportSocketOptions() = default;

  /**
   * Server name override (e.g., SNI)
   */
  virtual const optional<std::string>& serverNameOverride() const = 0;

  /**
   * Subject alternative name list override for verification
   */
  virtual const std::vector<std::string>& verifySubjectAltNameListOverride() const = 0;

  /**
   * Application protocol list override (ALPN)
   */
  virtual const std::vector<std::string>& applicationProtocolListOverride() const = 0;

  /**
   * Application protocol fallback
   */
  virtual const std::vector<std::string>& applicationProtocolFallback() const = 0;

  /**
   * Additional socket options
   */
  virtual const SocketOptionsSharedPtr& socketOptions() const = 0;
};

/**
 * Default implementation of transport socket options
 */
class TransportSocketOptionsImpl : public TransportSocketOptions {
public:
  TransportSocketOptionsImpl() = default;

  // Builder pattern
  TransportSocketOptionsImpl& setServerNameOverride(const std::string& name) {
    server_name_override_ = name;
    return *this;
  }

  TransportSocketOptionsImpl& setVerifySubjectAltNameListOverride(const std::vector<std::string>& names) {
    verify_san_list_override_ = names;
    return *this;
  }

  TransportSocketOptionsImpl& setApplicationProtocolListOverride(const std::vector<std::string>& protocols) {
    alpn_list_override_ = protocols;
    return *this;
  }

  TransportSocketOptionsImpl& setApplicationProtocolFallback(const std::vector<std::string>& protocols) {
    alpn_fallback_ = protocols;
    return *this;
  }

  TransportSocketOptionsImpl& setSocketOptions(SocketOptionsSharedPtr options) {
    socket_options_ = options;
    return *this;
  }

  // TransportSocketOptions interface
  const optional<std::string>& serverNameOverride() const override { return server_name_override_; }
  const std::vector<std::string>& verifySubjectAltNameListOverride() const override { return verify_san_list_override_; }
  const std::vector<std::string>& applicationProtocolListOverride() const override { return alpn_list_override_; }
  const std::vector<std::string>& applicationProtocolFallback() const override { return alpn_fallback_; }
  const SocketOptionsSharedPtr& socketOptions() const override { return socket_options_; }

private:
  optional<std::string> server_name_override_;
  std::vector<std::string> verify_san_list_override_;
  std::vector<std::string> alpn_list_override_;
  std::vector<std::string> alpn_fallback_;
  SocketOptionsSharedPtr socket_options_;
};

/**
 * Transport socket factory configuration
 */
struct TransportSocketFactoryConfig {
  std::string name;
  bool implements_secure_transport{false};
};

/**
 * Base class for transport socket factories
 */
class TransportSocketFactoryBase {
public:
  virtual ~TransportSocketFactoryBase() = default;

  /**
   * Check if this factory creates secure transport sockets
   */
  virtual bool implementsSecureTransport() const = 0;

  /**
   * Get the factory name
   */
  virtual absl::string_view name() const = 0;
};

/**
 * Factory for creating client transport sockets
 */
class ClientTransportSocketFactory : public virtual TransportSocketFactoryBase {
public:
  virtual ~ClientTransportSocketFactory() = default;

  /**
   * Create a transport socket for client connections
   */
  virtual TransportSocketPtr createTransportSocket(
      TransportSocketOptionsSharedPtr options) const = 0;

  /**
   * Check if the factory supports ALPN
   */
  virtual bool supportsAlpn() const { return false; }

  /**
   * Get the default server name indication
   */
  virtual absl::string_view defaultServerNameIndication() const { return ""; }

  /**
   * Hash key for connection pooling
   */
  virtual void hashKey(std::vector<uint8_t>& key,
                       TransportSocketOptionsSharedPtr options) const = 0;
};

/**
 * Factory for creating server transport sockets
 */
class ServerTransportSocketFactory : public virtual TransportSocketFactoryBase {
public:
  virtual ~ServerTransportSocketFactory() = default;

  /**
   * Create a transport socket for server connections
   */
  virtual TransportSocketPtr createTransportSocket() const = 0;
};

/**
 * Factory that can create both client and server sockets
 */
class UniversalTransportSocketFactory : public ClientTransportSocketFactory,
                                        public ServerTransportSocketFactory {
public:
  virtual ~UniversalTransportSocketFactory() = default;
};

/**
 * Raw buffer transport socket (no encryption)
 */
class RawBufferTransportSocket : public TransportSocket {
public:
  RawBufferTransportSocket();
  ~RawBufferTransportSocket() override;

  // TransportSocket interface
  void setTransportSocketCallbacks(TransportSocketCallbacks& callbacks) override;
  std::string protocol() const override { return ""; }
  absl::string_view failureReason() const override { return failure_reason_; }
  bool canFlushClose() override { return true; }
  Result<void> connect(Socket& socket) override;
  void closeSocket(ConnectionEvent event) override;
  IoResult doRead(Buffer& buffer) override;
  IoResult doWrite(Buffer& buffer, bool end_stream) override;
  void onConnected() override;

private:
  TransportSocketCallbacks* callbacks_{nullptr};
  std::string failure_reason_;
  bool connected_{false};
  bool shutdown_read_{false};
  bool shutdown_write_{false};
};

/**
 * Raw buffer transport socket factory
 */
class RawBufferTransportSocketFactory : public UniversalTransportSocketFactory {
public:
  RawBufferTransportSocketFactory() = default;

  // TransportSocketFactoryBase interface
  bool implementsSecureTransport() const override { return false; }
  absl::string_view name() const override { return "raw_buffer"; }

  // ClientTransportSocketFactory interface
  TransportSocketPtr createTransportSocket(
      TransportSocketOptionsSharedPtr options) const override;
  void hashKey(std::vector<uint8_t>& key,
               TransportSocketOptionsSharedPtr options) const override;

  // ServerTransportSocketFactory interface
  TransportSocketPtr createTransportSocket() const override;
};

/**
 * Create a raw buffer transport socket factory
 */
inline TransportSocketFactoryPtr createRawBufferTransportSocketFactory() {
  return std::make_unique<RawBufferTransportSocketFactory>();
}

} // namespace network
} // namespace mcp

#endif // MCP_NETWORK_TRANSPORT_SOCKET_H