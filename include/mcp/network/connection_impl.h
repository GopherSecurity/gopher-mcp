#ifndef MCP_NETWORK_CONNECTION_IMPL_H
#define MCP_NETWORK_CONNECTION_IMPL_H

#include <deque>
#include <memory>

#include "mcp/network/connection.h"
#include "mcp/stream_info/stream_info_impl.h"

namespace mcp {
namespace network {

/**
 * Connection implementation
 *
 * This class implements the full connection abstraction, integrating:
 * - Event loop integration for async I/O
 * - Transport socket for encryption/protocol handling
 * - Filter chain for extensibility
 * - Buffer management with watermarks
 * - Connection lifecycle management
 */
class ConnectionImpl : public ConnectionImplBase,
                       public ServerConnection,
                       public ClientConnection,
                       public std::enable_shared_from_this<ConnectionImpl> {
 public:
  /**
   * Create a server connection
   */
  static std::unique_ptr<ServerConnection> createServerConnection(
      event::Dispatcher& dispatcher,
      SocketPtr&& socket,
      TransportSocketPtr&& transport_socket,
      stream_info::StreamInfo& stream_info);

  /**
   * Create a client connection
   */
  static std::unique_ptr<ClientConnection> createClientConnection(
      event::Dispatcher& dispatcher,
      SocketPtr&& socket,
      TransportSocketPtr&& transport_socket,
      stream_info::StreamInfo& stream_info);

  ConnectionImpl(event::Dispatcher& dispatcher,
                 SocketPtr&& socket,
                 TransportSocketPtr&& transport_socket,
                 bool connected);

  ~ConnectionImpl() override;

  // Connection interface
  void close(ConnectionCloseType type) override;
  void close(ConnectionCloseType type, const std::string& details) override;
  DetectedCloseType detectedCloseType() const override {
    return detected_close_type_;
  }
  void hashKey(std::vector<uint8_t>& hash) const override;
  void noDelay(bool enable) override;
  ReadDisableStatus readDisableWithStatus(bool disable) override;
  void detectEarlyCloseWhenReadDisabled(bool should_detect) override {
    detect_early_close_ = should_detect;
  }
  bool readEnabled() const override { return read_disable_count_ == 0; }
  optional<UnixDomainSocketPeerCredentials> unixSocketPeerCredentials()
      const override;
  void setConnectionStats(const ConnectionStats& stats) override {
    stats_ = stats;
  }
  SslConnectionInfoConstSharedPtr ssl() const override;
  std::string requestedServerName() const override;
  ConnectionState state() const override { return state_; }
  bool connecting() const override { return connecting_; }
  void write(Buffer& data, bool end_stream) override;
  void setBufferLimits(uint32_t limit) override;
  uint32_t bufferLimit() const override { return buffer_limit_; }
  bool aboveHighWatermark() const override { return above_high_watermark_; }
  const SocketOptionsSharedPtr& socketOptions() const override {
    return socket_options_;
  }
  void setDelayedCloseTimeout(std::chrono::milliseconds timeout) override;
  bool startSecureTransport() override;
  optional<std::chrono::milliseconds> lastRoundTripTime() const override;
  void configureInitialCongestionWindow(uint64_t bandwidth_bits_per_sec,
                                        std::chrono::microseconds rtt) override;
  optional<uint64_t> congestionWindowInBytes() const override;

  // FilterManagerConnection interface
  Buffer& readBuffer() override { return read_buffer_; }
  Buffer& writeBuffer() override { return write_buffer_; }
  Buffer* currentWriteBuffer() override { return current_write_buffer_; }
  bool currentWriteEndStream() const override {
    return current_write_end_stream_;
  }
  bool readHalfClosed() const override { return read_half_closed_; }
  bool isClosed() const override { return state_ == ConnectionState::Closed; }
  void readDisable(bool disable) override {
    // Call the Connection version and ignore the return status
    static_cast<void>(readDisableWithStatus(disable));
  }
  bool readDisabled() const override { return read_disable_count_ > 0; }

  // TransportSocketCallbacks interface
  IoHandle& ioHandle() override { return socket_->ioHandle(); }
  const IoHandle& ioHandle() const override { return socket_->ioHandle(); }
  Connection& connection() override { return *this; }
  bool shouldDrainReadBuffer() override;
  void setTransportSocketIsReadable() override;
  void raiseEvent(ConnectionEvent event) override;
  void flushWriteBuffer() override;

  // ServerConnection interface
  void setTransportSocketConnectTimeout(
      std::chrono::milliseconds timeout) override;

  // ClientConnection interface
  void connect() override;

  // FilterManager interface (not virtual in base Connection)
  void addWriteFilter(WriteFilterSharedPtr filter);
  void addFilter(FilterSharedPtr filter);
  void addReadFilter(ReadFilterSharedPtr filter);
  void removeReadFilter(ReadFilterSharedPtr filter);
  bool initializeReadFilters();

  // Event handlers (public for stdio transport initialization)
  void onReadReady();
  void onWriteReady();

  // Get filter manager for filter chain factory
  FilterManager& filterManager() { return filter_manager_; }

 private:
  // Event handlers
  void onFileEvent(uint32_t events);

  // Connection lifecycle
  void closeSocket(ConnectionEvent close_type);
  void doConnect();
  void raiseConnectionEvent(ConnectionEvent event);
  void onConnected(); // Notify transport socket of connection completion

  // State machine integration
  void onStateChanged(ConnectionState old_state, ConnectionState new_state);
  void configureStateMachine();

  // Read path
  void doRead();
  TransportIoResult doReadFromSocket();
  void processReadBuffer();

  // Write path
  void doWrite();
  TransportIoResult doWriteToSocket();
  void handleWrite(bool all_data_sent);

  // Buffer management
  void setReadBufferReady();
  void updateReadBufferStats(uint64_t num_read, uint64_t new_size);
  void updateWriteBufferStats(uint64_t num_written, uint64_t new_size);

  // Timer callbacks
  void onDelayedCloseTimeout();
  void onConnectTimeout();

  // Helper methods
  void enableFileEvents(uint32_t events);
  void disableFileEvents(uint32_t events);
  uint32_t getReadyEvents();

  // Deferred close handling
  // closeThroughFilterManager: Safely closes connection using deferred deletion
  // pattern to prevent use-after-free when connection is destroyed during
  // method execution. This is critical for preventing segfaults when EOF is
  // detected in doRead().
  void closeThroughFilterManager(ConnectionEvent close_type);
  void scheduleDelayedClose();

  // State flags
  bool read_half_closed_{false};
  bool write_half_closed_{false};
  ConnectionEvent immediate_error_event_{ConnectionEvent::Connected}; // Use Connected as "no error"
  bool bind_error_{false};
  bool write_ready_{false};
  bool transport_wants_read_{false}; // Transport requested read resumption

  // Socket options
  SocketOptionsSharedPtr socket_options_;

  // Transport socket connect timeout (server connections)
  std::chrono::milliseconds transport_connect_timeout_{0};
  event::TimerPtr transport_connect_timer_;

  // Deferred close handling
  bool delayed_close_pending_{false};
  bool deferred_delete_{false};

  // Current file event state
  uint32_t file_event_state_{0};

  // Write scheduling callback (unused, kept for future use)
  event::SchedulableCallbackPtr write_scheduled_callback_;

  // Connection type
  bool is_server_connection_{false};

  // Connection state
  bool connected_{false};

  // Watermarks
  uint32_t high_watermark_{0};
  uint32_t low_watermark_{0};

  // Watermark callbacks
  std::list<WatermarkCallbacks*> watermark_callbacks_;

  // Current write context for filter chain processing
  // These are temporary storage used only during write() call in dispatcher
  // thread Following production pattern: all operations happen in dispatcher
  // thread
  Buffer* current_write_buffer_{nullptr};
  bool current_write_end_stream_{false};

  // Connection callbacks are stored in base class callbacks_ member
  // No need for duplicate connection_callbacks_ here
};

/**
 * Utility class for managing connection lifecycle
 */
class ConnectionUtility {
 public:
  /**
   * Update buffer stats for a connection
   */
  static void updateBufferStats(uint64_t delta,
                                uint64_t new_total,
                                uint64_t& previous_total,
                                ConnectionStats& stats);

  /**
   * Set socket options on a socket
   */
  static bool applySocketOptions(Socket& socket,
                                 const SocketOptionsSharedPtr& options);

  /**
   * Get peer credentials for Unix domain socket
   */
  static optional<Connection::UnixDomainSocketPeerCredentials>
  getUnixSocketPeerCredentials(const Socket& socket);

  /**
   * Configure socket for connection
   */
  static void configureSocket(Socket& socket, bool is_server);
};

/**
 * Connection event logger for debugging
 */
class ConnectionEventLogger {
 public:
  ConnectionEventLogger(const Connection& connection);

  void logEvent(ConnectionEvent event, const std::string& details = "");
  void logRead(size_t bytes_read, size_t buffer_size);
  void logWrite(size_t bytes_written, size_t buffer_size);
  void logError(const std::string& error);

 private:
  const Connection& connection_;
  std::string connection_id_;
};

}  // namespace network
}  // namespace mcp

#endif  // MCP_NETWORK_CONNECTION_IMPL_H