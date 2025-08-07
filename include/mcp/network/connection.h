#ifndef MCP_NETWORK_CONNECTION_H
#define MCP_NETWORK_CONNECTION_H

#include <atomic>
#include <chrono>
#include <functional>
#include <list>
#include <memory>
#include <string>

#include "mcp/buffer.h"
#include "mcp/compat.h"
#include "mcp/event/event_loop.h"
#include "mcp/network/filter.h"
#include "mcp/network/socket.h"
#include "mcp/network/transport_socket.h"
#include "mcp/stream_info/stream_info.h"
#include "mcp/stream_info/stream_info_impl.h"

namespace mcp {
namespace network {

// Forward declarations
class Connection;
class ConnectionCallbacks;
class FilterManager;

using ConnectionPtr = std::unique_ptr<Connection>;
using ConnectionSharedPtr = std::shared_ptr<Connection>;
using ConnectionWeakPtr = std::weak_ptr<Connection>;

/**
 * Connection statistics
 */
struct ConnectionStats {
  uint64_t read_total_{0};
  uint64_t read_current_{0};
  uint64_t write_total_{0};
  uint64_t write_current_{0};
  uint64_t bind_errors_{0};
  uint64_t delayed_close_timeouts_{0};
};

/**
 * Connection close types
 */
enum class DetectedCloseType { Normal, LocalReset, RemoteReset };

/**
 * Read disable status
 */
enum class ReadDisableStatus {
  NoTransition,               // No change in read state
  StillReadDisabled,          // Still disabled after this call
  TransitionedToReadEnabled,  // Transitioned from disabled to enabled
  TransitionedToReadDisabled  // Transitioned from enabled to disabled
};

/**
 * Connection state
 */
enum class ConnectionState {
  Open,     // Connection is open
  Closing,  // Connection is closing
  Closed    // Connection is closed
};

/**
 * Bytes sent callback
 */
using BytesSentCb = std::function<bool(uint64_t bytes_sent)>;

/**
 * Watermark callbacks interface
 */
class WatermarkCallbacks {
 public:
  virtual ~WatermarkCallbacks() = default;

  /**
   * Called when write buffer goes above high watermark
   */
  virtual void onAboveWriteBufferHighWatermark() = 0;

  /**
   * Called when write buffer goes below low watermark
   */
  virtual void onBelowWriteBufferLowWatermark() = 0;
};

/**
 * Connection callbacks interface
 */
class ConnectionCallbacks {
 public:
  virtual ~ConnectionCallbacks() = default;

  /**
   * Called when a connection event occurs
   */
  virtual void onEvent(ConnectionEvent event) = 0;

  /**
   * Called when the write buffer goes above its high watermark
   */
  virtual void onAboveWriteBufferHighWatermark() = 0;

  /**
   * Called when the write buffer goes below its low watermark
   */
  virtual void onBelowWriteBufferLowWatermark() = 0;
};

/**
 * Base connection interface
 *
 * This combines the connection management, filter chain, and transport socket
 */
class Connection : public event::DeferredDeletable,
                   public FilterManagerConnection,
                   public TransportSocketCallbacks {
 public:
  virtual ~Connection() = default;

  /**
   * Register connection callbacks
   */
  virtual void addConnectionCallbacks(ConnectionCallbacks& cb) = 0;

  /**
   * Remove connection callbacks
   */
  virtual void removeConnectionCallbacks(ConnectionCallbacks& cb) = 0;

  /**
   * Register callback for bytes sent events
   */
  virtual void addBytesSentCallback(BytesSentCb cb) = 0;

  /**
   * Enable/disable half-close semantics
   */
  virtual void enableHalfClose(bool enabled) = 0;

  /**
   * Check if half-close is enabled
   */
  virtual bool isHalfCloseEnabled() const = 0;

  /**
   * Close the connection
   */
  virtual void close(ConnectionCloseType type) = 0;
  virtual void close(ConnectionCloseType type, const std::string& details) = 0;

  /**
   * Get the detected close type
   */
  virtual DetectedCloseType detectedCloseType() const = 0;

  /**
   * Get the event dispatcher
   */
  virtual event::Dispatcher& dispatcher() const = 0;

  /**
   * Get the unique connection ID
   */
  virtual uint64_t id() const = 0;

  /**
   * Hash key for connection pooling
   */
  virtual void hashKey(std::vector<uint8_t>& hash) const = 0;

  /**
   * Get the negotiated application protocol
   */
  virtual std::string nextProtocol() const = 0;

  /**
   * Enable/disable Nagle's algorithm
   */
  virtual void noDelay(bool enable) = 0;

  /**
   * Disable/enable reading from the socket
   * Note: This is a different method from FilterManagerConnection::readDisable
   * Use readDisableWithStatus for the version that returns status
   */
  virtual ReadDisableStatus readDisableWithStatus(bool disable) = 0;

  /**
   * Enable/disable early close detection when read disabled
   */
  virtual void detectEarlyCloseWhenReadDisabled(bool should_detect) = 0;

  /**
   * Check if reading is enabled
   */
  virtual bool readEnabled() const = 0;

  /**
   * Get connection info setter
   */
  virtual ConnectionInfoSetter& connectionInfoSetter() = 0;
  virtual const ConnectionInfoProvider& connectionInfoProvider() const = 0;
  virtual ConnectionInfoProviderSharedPtr connectionInfoProviderSharedPtr()
      const = 0;

  /**
   * Get Unix domain socket peer credentials (if applicable)
   */
  struct UnixDomainSocketPeerCredentials {
    int32_t pid;
    uint32_t uid;
    uint32_t gid;
  };
  virtual optional<UnixDomainSocketPeerCredentials> unixSocketPeerCredentials()
      const = 0;

  /**
   * Set connection statistics
   */
  virtual void setConnectionStats(const ConnectionStats& stats) = 0;

  /**
   * Get SSL connection info
   */
  virtual SslConnectionInfoConstSharedPtr ssl() const = 0;

  /**
   * Get requested server name (SNI)
   */
  virtual std::string requestedServerName() const = 0;

  /**
   * Get connection state
   */
  virtual ConnectionState state() const = 0;

  /**
   * Check if connection is still connecting
   */
  virtual bool connecting() const = 0;

  /**
   * Write data to the connection
   */
  virtual void write(Buffer& data, bool end_stream) = 0;

  /**
   * Set buffer limits for flow control
   */
  virtual void setBufferLimits(uint32_t limit) = 0;

  /**
   * Get current buffer limit
   */
  virtual uint32_t bufferLimit() const = 0;

  /**
   * Check if above high watermark
   */
  virtual bool aboveHighWatermark() const = 0;

  /**
   * Get socket options
   */
  virtual const SocketOptionsSharedPtr& socketOptions() const = 0;

  /**
   * Get stream info
   */
  virtual stream_info::StreamInfo& streamInfo() = 0;
  virtual const stream_info::StreamInfo& streamInfo() const = 0;

  /**
   * Set delayed close timeout
   */
  virtual void setDelayedCloseTimeout(std::chrono::milliseconds timeout) = 0;

  /**
   * Get transport failure reason
   */
  virtual std::string transportFailureReason() const = 0;

  /**
   * Get local close reason
   */
  virtual std::string localCloseReason() const = 0;

  /**
   * Start secure transport (STARTTLS)
   */
  virtual bool startSecureTransport() = 0;

  /**
   * Get last round trip time
   */
  virtual optional<std::chrono::milliseconds> lastRoundTripTime() const = 0;

  /**
   * Configure initial congestion window
   */
  virtual void configureInitialCongestionWindow(
      uint64_t bandwidth_bits_per_sec, std::chrono::microseconds rtt) = 0;

  /**
   * Get congestion window in bytes
   */
  virtual optional<uint64_t> congestionWindowInBytes() const = 0;

  /**
   * Get socket for this connection
   */
  virtual Socket& socket() = 0;
  virtual const Socket& socket() const = 0;

  /**
   * Get transport socket
   */
  virtual TransportSocket& transportSocket() = 0;
  virtual const TransportSocket& transportSocket() const = 0;
};

/**
 * Server connection interface
 */
class ServerConnection : public virtual Connection {
 public:
  virtual ~ServerConnection() = default;

  /**
   * Set transport socket connect timeout
   */
  virtual void setTransportSocketConnectTimeout(
      std::chrono::milliseconds timeout) = 0;
};

/**
 * Client connection interface
 */
class ClientConnection : public virtual Connection {
 public:
  virtual ~ClientConnection() = default;

  /**
   * Connect to remote host
   */
  virtual void connect() = 0;
};

using ServerConnectionPtr = std::unique_ptr<ServerConnection>;
using ClientConnectionPtr = std::unique_ptr<ClientConnection>;

/**
 * Connection pool callbacks
 */
class ConnectionPoolCallbacks {
 public:
  virtual ~ConnectionPoolCallbacks() = default;

  /**
   * Called when a connection is created
   */
  virtual void onConnectionCreate(Connection& connection) = 0;

  /**
   * Called when a connection event occurs
   */
  virtual void onConnectionEvent(Connection& connection,
                                 ConnectionEvent event) = 0;
};

/**
 * Base implementation for connection
 */
class ConnectionImplBase : public virtual Connection {
 public:
  ConnectionImplBase(event::Dispatcher& dispatcher,
                     SocketPtr&& socket,
                     TransportSocketPtr&& transport_socket);
  ~ConnectionImplBase() override;

  // Connection interface (partial implementation)
  void addConnectionCallbacks(ConnectionCallbacks& cb) override;
  void removeConnectionCallbacks(ConnectionCallbacks& cb) override;
  void addBytesSentCallback(BytesSentCb cb) override;
  void enableHalfClose(bool enabled) override { enable_half_close_ = enabled; }
  bool isHalfCloseEnabled() const override { return enable_half_close_; }
  event::Dispatcher& dispatcher() const override { return dispatcher_; }
  uint64_t id() const override { return id_; }
  std::string nextProtocol() const override {
    return transport_socket_->protocol();
  }
  ConnectionInfoSetter& connectionInfoSetter() override {
    return socket_->connectionInfoProvider();
  }
  const ConnectionInfoProvider& connectionInfoProvider() const override {
    return socket_->connectionInfoProvider();
  }
  ConnectionInfoProviderSharedPtr connectionInfoProviderSharedPtr()
      const override {
    return socket_->connectionInfoProviderSharedPtr();
  }
  Socket& socket() override { return *socket_; }
  const Socket& socket() const override { return *socket_; }
  TransportSocket& transportSocket() override { return *transport_socket_; }
  const TransportSocket& transportSocket() const override {
    return *transport_socket_;
  }
  stream_info::StreamInfo& streamInfo() override { return *stream_info_; }
  const stream_info::StreamInfo& streamInfo() const override {
    return *stream_info_;
  }
  std::string transportFailureReason() const override {
    return transport_socket_->failureReason();
  }
  std::string localCloseReason() const override { return local_close_reason_; }

  // Get filter manager for filter chain setup
  FilterManagerImpl& filterManager() { return filter_manager_; }

  // Note: TransportSocketCallbacks interface methods are implemented
  // separately in ConnectionImpl to avoid conflicts

 protected:
  // Internal helper methods
  void closeConnectionImmediately();
  void raiseConnectionEvent(ConnectionEvent event);
  void onReadReady();
  void onWriteReady();
  void updateReadBufferStats(uint64_t num_read, uint64_t new_size);
  void updateWriteBufferStats(uint64_t num_written, uint64_t new_size);
  void transportFailure();
  void setLocalCloseReason(const std::string& reason) {
    local_close_reason_ = std::string(reason);
  }

  // Watermark callbacks
  void onReadBufferLowWatermark();
  void onReadBufferHighWatermark();
  void onWriteBufferLowWatermark();
  void onWriteBufferHighWatermark();
  void onWriteBufferBelowLowWatermark();

  // Member variables
  event::Dispatcher& dispatcher_;
  SocketPtr socket_;
  TransportSocketPtr transport_socket_;
  stream_info::StreamInfoSharedPtr stream_info_;
  FilterManagerImpl filter_manager_;

  // Callbacks
  std::list<ConnectionCallbacks*> callbacks_;
  std::list<BytesSentCb> bytes_sent_callbacks_;

  // State
  const uint64_t id_;
  ConnectionState state_{ConnectionState::Open};
  bool enable_half_close_{false};
  bool connecting_{false};
  uint32_t read_disable_count_{0};
  bool detect_early_close_{true};
  bool above_high_watermark_{false};

  // Buffers with watermarks
  WatermarkBuffer read_buffer_;
  WatermarkBuffer write_buffer_;

  // Stats
  optional<ConnectionStats> stats_;
  uint64_t last_read_buffer_size_{0};
  uint64_t last_write_buffer_size_{0};

  // Configuration
  uint32_t buffer_limit_{0};
  std::chrono::milliseconds delayed_close_timeout_{0};
  event::TimerPtr delayed_close_timer_;

  // Close reasons
  std::string local_close_reason_;
  DetectedCloseType detected_close_type_{DetectedCloseType::Normal};

  // File events
  event::FileEventPtr file_event_;

  // Connection ID generator
  static std::atomic<uint64_t> next_connection_id_;
};

}  // namespace network
}  // namespace mcp

#endif  // MCP_NETWORK_CONNECTION_H