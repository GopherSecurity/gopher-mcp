#include "mcp/network/connection_impl.h"

#include <algorithm>
#include <iostream>
#include <sstream>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#endif

#include "mcp/buffer.h"
#include "mcp/event/event_loop.h"
#include "mcp/network/connection_utility.h"
#include "mcp/network/socket.h"
#include "mcp/network/transport_socket.h"

namespace mcp {
namespace network {

// ConnectionImplBase implementation

ConnectionImplBase::ConnectionImplBase(event::Dispatcher& dispatcher,
                                       SocketPtr&& socket,
                                       TransportSocketPtr&& transport_socket)
    : dispatcher_(dispatcher),
      socket_(std::move(socket)),
      transport_socket_(std::move(transport_socket)),
      stream_info_(std::make_shared<stream_info::StreamInfoImpl>()),
      filter_manager_(*this, dispatcher),
      id_(next_connection_id_++),
      read_buffer_([this]() { return onReadBufferLowWatermark(); },
                   [this]() { return onReadBufferHighWatermark(); },
                   []() { return false; }),  // below overflow not used for read
      write_buffer_([this]() { return onWriteBufferLowWatermark(); },
                    [this]() { return onWriteBufferHighWatermark(); },
                    [this]() { return onWriteBufferBelowLowWatermark(); }) {
  // Initialize connection state machine
  state_machine_ = std::make_unique<ConnectionStateMachine>(dispatcher);

  // Register state change listener
  state_machine_->addStateChangeListener(
      [this](const StateTransitionContext& ctx) {
        // Map machine state to connection state
        switch (ctx.to_state) {
          // Connecting states
          case ConnectionMachineState::Connecting:
          case ConnectionMachineState::Resolving:
          case ConnectionMachineState::TcpConnected:
          case ConnectionMachineState::HandshakeInProgress:
            // During connection establishment, keep current state
            // The connecting_ flag tracks this separately
            break;

          // Open states
          case ConnectionMachineState::Connected:
          case ConnectionMachineState::Reading:
          case ConnectionMachineState::Writing:
          case ConnectionMachineState::Processing:
          case ConnectionMachineState::Idle:
            state_ = ConnectionState::Open;
            break;

          // Closing states
          case ConnectionMachineState::Closing:
          case ConnectionMachineState::Draining:
          case ConnectionMachineState::Flushing:
          case ConnectionMachineState::HalfClosedLocal:
          case ConnectionMachineState::HalfClosedRemote:
            state_ = ConnectionState::Closing;
            break;

          // Closed states
          case ConnectionMachineState::Closed:
          case ConnectionMachineState::Error:
          case ConnectionMachineState::Aborted:
            state_ = ConnectionState::Closed;
            break;

          // Initial states - don't change connection state
          case ConnectionMachineState::Uninitialized:
          case ConnectionMachineState::Initialized:
            // Keep current state
            break;

          default:
            // Keep current state for other machine states
            break;
        }
      });

  // State machine starts in appropriate initial state
}

ConnectionImplBase::~ConnectionImplBase() = default;

void ConnectionImplBase::addConnectionCallbacks(ConnectionCallbacks& cb) {
  callbacks_.push_back(&cb);
}

void ConnectionImplBase::removeConnectionCallbacks(ConnectionCallbacks& cb) {
  callbacks_.erase(std::remove(callbacks_.begin(), callbacks_.end(), &cb),
                   callbacks_.end());
}

void ConnectionImplBase::addBytesSentCallback(BytesSentCb cb) {
  bytes_sent_callbacks_.push_back(std::move(cb));
}

void ConnectionImplBase::closeConnectionImmediately() {
  if (socket_ && socket_->isOpen()) {
    socket_->close();
  }
}

void ConnectionImplBase::raiseConnectionEvent(ConnectionEvent event) {
  for (auto* cb : callbacks_) {
    cb->onEvent(event);
  }
}

void ConnectionImplBase::onReadReady() {
  // Implemented in ConnectionImpl
}

void ConnectionImplBase::onWriteReady() {
  // Implemented in ConnectionImpl
}

void ConnectionImplBase::updateReadBufferStats(uint64_t num_read,
                                               uint64_t new_size) {
  // Update read statistics
  if (stats_.has_value()) {
    stats_->read_total_ += num_read;
    stats_->read_current_ = new_size;
  }
}

void ConnectionImplBase::updateWriteBufferStats(uint64_t num_written,
                                                uint64_t new_size) {
  // Update write statistics
  if (stats_.has_value()) {
    stats_->write_total_ += num_written;
    stats_->write_current_ = new_size;
  }
}

void ConnectionImplBase::transportFailure() {
  // Set transport failure reason in transport socket
  // The actual failure reason is retrieved via transportFailureReason()
}

// Watermark callbacks
void ConnectionImplBase::onReadBufferLowWatermark() {
  // Resume reading when buffer drops below low watermark
  if (read_disable_count_ == 0 && !socket_->isOpen()) {
    return;
  }
  // Enable read events
  if (file_event_) {
    file_event_->setEnabled(static_cast<uint32_t>(event::FileReadyType::Read));
  }
}

void ConnectionImplBase::onReadBufferHighWatermark() {
  // Stop reading when buffer is full
  if (file_event_) {
    file_event_->setEnabled(
        static_cast<uint32_t>(event::FileReadyType::Write) |
        static_cast<uint32_t>(event::FileReadyType::Closed));
  }
}

void ConnectionImplBase::onWriteBufferLowWatermark() {
  // Notify when write buffer drops below low watermark
  above_high_watermark_ = false;
  for (auto* cb : callbacks_) {
    cb->onAboveWriteBufferHighWatermark();
  }
}

void ConnectionImplBase::onWriteBufferHighWatermark() {
  // Notify when write buffer goes above high watermark
  above_high_watermark_ = true;
  for (auto* cb : callbacks_) {
    cb->onBelowWriteBufferLowWatermark();
  }
}

void ConnectionImplBase::onWriteBufferBelowLowWatermark() {
  // Additional handling when buffer is below low watermark
  // Used for resuming writes
}

// Static member initialization
std::atomic<uint64_t> ConnectionImplBase::next_connection_id_{1};

// ConnectionImpl implementation

std::unique_ptr<ServerConnection> ConnectionImpl::createServerConnection(
    event::Dispatcher& dispatcher,
    SocketPtr&& socket,
    TransportSocketPtr&& transport_socket,
    stream_info::StreamInfo& stream_info) {
  auto connection = std::make_unique<ConnectionImpl>(
      dispatcher, std::move(socket), std::move(transport_socket), true);
  connection->is_server_connection_ = true;
  connection->stream_info_ = std::make_shared<stream_info::StreamInfoImpl>();
  return std::unique_ptr<ServerConnection>(std::move(connection));
}

std::unique_ptr<ClientConnection> ConnectionImpl::createClientConnection(
    event::Dispatcher& dispatcher,
    SocketPtr&& socket,
    TransportSocketPtr&& transport_socket,
    stream_info::StreamInfo& stream_info) {
  auto connection = std::make_unique<ConnectionImpl>(
      dispatcher, std::move(socket), std::move(transport_socket), false);
  connection->is_server_connection_ = false;
  connection->stream_info_ = std::make_shared<stream_info::StreamInfoImpl>();
  return std::unique_ptr<ClientConnection>(std::move(connection));
}

// Null transport socket for raw TCP connections
class RawTransportSocket : public TransportSocket {
 public:
  void setTransportSocketCallbacks(
      TransportSocketCallbacks& callbacks) override {
    callbacks_ = &callbacks;
  }
  std::string protocol() const override { return "raw"; }
  std::string failureReason() const override { return ""; }
  bool canFlushClose() override { return true; }
  void closeSocket(ConnectionEvent) override {}
  TransportIoResult doRead(Buffer& buffer) override {
    // For raw socket, read directly from the connection's socket
    if (!callbacks_) {
      return TransportIoResult::error(Error{-1, "No callbacks"});
    }
    auto result = callbacks_->ioHandle().read(buffer);
    if (!result.ok()) {
      if (result.wouldBlock()) {
        return TransportIoResult::stop();
      }
      return TransportIoResult::error(Error{result.error_code(), "Read error"});
    }
    size_t bytes = *result;
    return bytes == 0 ? TransportIoResult::close()
                      : TransportIoResult::success(bytes);
  }
  TransportIoResult doWrite(Buffer& buffer, bool) override {
    if (!callbacks_) {
      return TransportIoResult::error(Error{-1, "No callbacks"});
    }
    auto result = callbacks_->ioHandle().write(buffer);
    if (!result.ok()) {
      if (result.wouldBlock()) {
        return TransportIoResult::stop();
      }
      return TransportIoResult::error(
          Error{result.error_code(), "Write error"});
    }
    return TransportIoResult::success(*result);
  }
  void onConnected() override {
    // RawTransportSocket has no special handling for connection
    // The base TransportSocket interface requires this method
  }
  VoidResult connect(Socket&) override { return makeVoidSuccess(); }
  SslConnectionInfoConstSharedPtr ssl() const override { return nullptr; }
  bool startSecureTransport() override { return false; }

 private:
  TransportSocketCallbacks* callbacks_{nullptr};
};

ConnectionImpl::ConnectionImpl(event::Dispatcher& dispatcher,
                               SocketPtr&& socket,
                               TransportSocketPtr&& transport_socket,
                               bool connected)
    : ConnectionImplBase(dispatcher,
                         std::move(socket),
                         transport_socket
                             ? std::move(transport_socket)
                             : std::make_unique<RawTransportSocket>()) {
  // Set initial state
  connected_ = connected;
  connecting_ = !connected;
  state_ = connected ? ConnectionState::Open : ConnectionState::Closed;

  // Set up transport socket callbacks
  if (transport_socket_) {
    transport_socket_->setTransportSocketCallbacks(
        static_cast<TransportSocketCallbacks&>(*this));

    // CRITICAL: For server connections, notify transport socket that connection
    // is established Server connections are already connected when created
    // (socket was accepted) Without this, HTTP+SSE transport won't initialize
    // properly for server mode Flow: Accept socket → Create connection
    // (connected=true) → Notify transport
    if (connected) {
      transport_socket_->onConnected();
    }
  }

  // Configure socket with optimal settings
  SocketConfigUtility::configureSocket(*socket_, is_server_connection_);

  // Apply socket options if any
  // auto socket_options = socketOptions();
  // if (socket_options) {
  //   ConnectionUtility::applySocketOptions(*socket_, socket_options_);
  // }

  // Create file event for socket I/O
  // Flow: Socket created -> Register file event -> Enable read/write based on
  // state Server connections: Enable read immediately to receive requests
  // Client connections: Enable write first for connect, then read after
  // connected
  if (socket_) {
    try {
      auto fd = socket_->ioHandle().fd();
      if (fd != INVALID_SOCKET_FD) {
        // Use edge-triggered events for both client and server
        // This avoids busy write loops.
        // Socket is always writable when TCP send buffer has space,
        // which creates busy loop with empty write_buffer_
        // Edge-triggered only fires on state transitions, not continuously
        auto trigger_type = event::PlatformDefaultTriggerType;

        // Set initial events based on connection state
        // Server connections (connected=true): Start with Read to receive
        // requests Client connections (connecting=true): Start with Write to
        // detect connection completion This follows the reference pattern for
        // event initialization

        uint32_t initial_events;
        if (connected) {
          // Server connection or already connected (e.g., stdio pipes)
          // CRITICAL FIX: Only enable Read events initially.
          // Enabling both Read and Write causes a busy loop on macOS/kqueue
          // because Write events fire continuously (socket is always writable)
          // and mask Read events. Write events should only be enabled when
          // there's actually data to send.
          initial_events = static_cast<uint32_t>(event::FileReadyType::Read);
        } else if (connecting_) {
          // Client connecting - enable write to detect connection completion
          initial_events = static_cast<uint32_t>(event::FileReadyType::Write);
        } else {
          // Not connected and not connecting - for stdio/pipes, treat as
          // already connected. Only enable Read initially; Write will be
          // enabled when there's data to send.
          initial_events = static_cast<uint32_t>(event::FileReadyType::Read);
        }

        file_event_ = dispatcher_.createFileEvent(
            socket_->ioHandle().fd(),
            [this](uint32_t events) { onFileEvent(events); }, trigger_type,
            initial_events);

        // Track which events are enabled
        file_event_state_ = initial_events;
      }
    } catch (...) {
      // Socket doesn't support file events (e.g., mock socket in tests)
    }
  }
}

ConnectionImpl::~ConnectionImpl() {
  // Ensure socket is closed
  if (state_ != ConnectionState::Closed) {
    closeSocket(ConnectionEvent::LocalClose);
  }
}

void ConnectionImpl::close(ConnectionCloseType type) { close(type, ""); }

void ConnectionImpl::close(ConnectionCloseType type,
                           const std::string& details) {
  if (state_ == ConnectionState::Closed) {
    return;
  }

  // Transition state machine to Closing
  if (state_machine_) {
    state_machine_->handleEvent(ConnectionStateMachineEvent::CloseRequested);
  }

  local_close_reason_ = std::string(details);

  switch (type) {
    case ConnectionCloseType::NoFlush:
      closeSocket(ConnectionEvent::LocalClose);
      break;

    case ConnectionCloseType::FlushWrite:
      state_ = ConnectionState::Closing;
      if (write_buffer_.length() == 0) {
        closeSocket(ConnectionEvent::LocalClose);
      } else {
        // Will close after write buffer is drained
        write_half_closed_ = true;
        doWrite();
      }
      break;

    case ConnectionCloseType::FlushWriteAndDelay:
      state_ = ConnectionState::Closing;
      write_half_closed_ = true;
      if (delayed_close_timer_ == nullptr) {
        delayed_close_timer_ =
            dispatcher_.createTimer([this]() { onDelayedCloseTimeout(); });
      }
      delayed_close_timer_->enableTimer(delayed_close_timeout_);
      doWrite();
      break;

    case ConnectionCloseType::Abort:
    case ConnectionCloseType::AbortReset:
      // Reset connection immediately
      {
        struct linger lng;
        lng.l_onoff = 1;
        lng.l_linger = 0;
        socket_->setSocketOption(SOL_SOCKET, SO_LINGER, &lng, sizeof(lng));
      }
      closeSocket(ConnectionEvent::LocalClose);
      break;
  }
}

void ConnectionImpl::hashKey(std::vector<uint8_t>& hash) const {
  // Hash local and remote addresses
  const auto local = socket_->connectionInfoProvider().localAddress();
  const auto remote = socket_->connectionInfoProvider().remoteAddress();

  if (local) {
    const auto addr_str = local->asString();
    hash.insert(hash.end(), addr_str.begin(), addr_str.end());
  }

  if (remote) {
    const auto addr_str = remote->asString();
    hash.insert(hash.end(), addr_str.begin(), addr_str.end());
  }
}

void ConnectionImpl::noDelay(bool enable) {
  // Follow reference pattern: check if socket is open first
  // This prevents errors when connection has already failed
  if (!socket_->isOpen()) {
    return;
  }

  // Don't set NODELAY for non-IP sockets (Unix domain, etc.)
  if (socket_->addressType() != Address::Type::Ip) {
    return;
  }

  int val = enable ? 1 : 0;
  socket_->setSocketOption(IPPROTO_TCP, TCP_NODELAY, &val, sizeof(val));
}

ReadDisableStatus ConnectionImpl::readDisableWithStatus(bool disable) {
  if (disable) {
    read_disable_count_++;

    if (state_ != ConnectionState::Open) {
      return ReadDisableStatus::NoTransition;
    }

    if (read_disable_count_ > 1) {
      // Already disabled
      return ReadDisableStatus::StillReadDisabled;
    }

    // First disable - keep Write enabled (even with empty buffer)
    // We'll handle the busy loop prevention in onWriteReady
    file_event_state_ = static_cast<uint32_t>(event::FileReadyType::Write);
    if (file_event_) {
      file_event_->setEnabled(file_event_state_);
    }
    return ReadDisableStatus::TransitionedToReadDisabled;
  } else {
    if (read_disable_count_ == 0) {
      return ReadDisableStatus::NoTransition;
    }

    read_disable_count_--;

    if (state_ != ConnectionState::Open) {
      return ReadDisableStatus::NoTransition;
    }

    if (read_disable_count_ == 0) {
      // Re-enable Read, and Write only if we have data to write
      // This prevents busy loop with level-triggered events
      uint32_t events_to_enable =
          static_cast<uint32_t>(event::FileReadyType::Read);
      if (write_buffer_.length() > 0) {
        events_to_enable |= static_cast<uint32_t>(event::FileReadyType::Write);
      }
      enableFileEvents(events_to_enable);

      // Always poke a read event after re-enabling to handle edge-trigger
      // races. Data might have arrived while disabled (kernel buffer non-empty)
      // but no new edge will occur, so force a read cycle now.
      if (file_event_) {
        file_event_->activate(
            static_cast<uint32_t>(event::FileReadyType::Read));
      }
      return ReadDisableStatus::TransitionedToReadEnabled;
    }

    return ReadDisableStatus::StillReadDisabled;
  }
}

bool ConnectionImpl::readEnabled() const {
  // Follow reference pattern: assert connection is open
  // Calls to readEnabled on closed socket are an error
  assert(state() == ConnectionState::Open &&
         "readEnabled called on non-open connection");
  assert(dispatcher_.isThreadSafe() &&
         "readEnabled must be called from dispatcher thread");
  return read_disable_count_ == 0;
}

optional<Connection::UnixDomainSocketPeerCredentials>
ConnectionImpl::unixSocketPeerCredentials() const {
  return ConnectionUtility::getUnixSocketPeerCredentials(*socket_);
}

SslConnectionInfoConstSharedPtr ConnectionImpl::ssl() const {
  static const SslConnectionInfoConstSharedPtr empty_ssl_info;
  return transport_socket_ ? transport_socket_->ssl() : empty_ssl_info;
}

std::string ConnectionImpl::requestedServerName() const {
  // This would be implemented based on transport socket info
  return "";
}

void ConnectionImpl::write(Buffer& data, bool end_stream) {
  /**
   * PUBLIC WRITE INTERFACE - Application entry point for sending data
   *
   * Complete flow:
   * 1. Application calls write() with data buffer
   * 2. FilterManager::onWrite() processes through write filter chain (REVERSE
   * order):
   *    - HttpSseJsonRpcProtocolFilter::onWrite() handles SSE/HTTP formatting
   *    - JsonRpcProtocolFilter::onWrite() adds JSON-RPC framing
   *    - HttpCodecFilter::onWrite() adds HTTP headers
   * 3. Filters modify buffer IN-PLACE (critical: no recursion!)
   * 4. Move processed data to write_buffer_
   * 5. Enable write events and trigger doWrite() if socket ready
   * 6. doWrite() writes to socket, continues until buffer empty or EAGAIN
   *
   * Thread safety: All operations must be in dispatcher thread
   * Buffer ownership: data is moved to write_buffer_ after processing
   */

  // Thread safety: all writes must happen in dispatcher thread
  assert(dispatcher_.isThreadSafe() &&
         "write() must be called from dispatcher thread");

  if (state_ != ConnectionState::Open || write_half_closed_) {
    return;
  }

  if (end_stream) {
    write_half_closed_ = true;
  }

  // Set current write context for filter chain processing
  // This is safe because we're in the dispatcher thread
  current_write_buffer_ = &data;
  current_write_end_stream_ = end_stream;

  // Process through write filters - they modify data in-place
  FilterStatus status = filter_manager_.onWrite();

  // Clear current write context
  current_write_buffer_ = nullptr;
  current_write_end_stream_ = false;

  if (status == FilterStatus::StopIteration) {
    return;
  }

  // Move processed data to write buffer
  size_t bytes_to_write = data.length();
  data.move(write_buffer_);

  // Update stats (use saved length since data is now empty after move)
  updateWriteBufferStats(bytes_to_write, write_buffer_.length());

  // Check watermarks
  if (write_buffer_.length() > high_watermark_ && !above_high_watermark_) {
    above_high_watermark_ = true;
    for (auto& cb : watermark_callbacks_) {
      cb->onAboveWriteBufferHighWatermark();
    }
  }

  // Enable write events and trigger write
  if (write_buffer_.length() > 0) {
#ifndef NDEBUG
    std::cerr << "[CONN] write(): buffer_len=" << write_buffer_.length()
              << " write_ready_=" << write_ready_ << std::endl;
#endif
    // Enable write events for future writes
    enableFileEvents(static_cast<uint32_t>(event::FileReadyType::Write));

    const bool transport_allows_immediate_write =
        transport_socket_ && transport_socket_->protocol() == "stdio";

    // If socket is already write-ready, or the transport guarantees that writes
    // never block (stdio pipes), flush immediately. Otherwise wait for the
    // dispatcher to signal write readiness.
    if (write_ready_ || transport_allows_immediate_write) {
#ifndef NDEBUG
      std::cerr << "[CONN] write(): calling doWrite()" << std::endl;
#endif
      doWrite();
    } else {
#ifndef NDEBUG
      std::cerr << "[CONN] write(): waiting for Write event" << std::endl;
#endif
    }
  }
}

void ConnectionImpl::setBufferLimits(uint32_t limit) {
  buffer_limit_ = limit;
  high_watermark_ = limit;
  low_watermark_ = limit / 2;
}

void ConnectionImpl::setDelayedCloseTimeout(std::chrono::milliseconds timeout) {
  delayed_close_timeout_ = timeout;
}

bool ConnectionImpl::startSecureTransport() {
  return transport_socket_ ? transport_socket_->startSecureTransport() : false;
}

optional<std::chrono::milliseconds> ConnectionImpl::lastRoundTripTime() const {
  // TODO: lastRoundTripTime not implemented in Socket
  return nullopt;
}

void ConnectionImpl::configureInitialCongestionWindow(
    uint64_t bandwidth_bits_per_sec, std::chrono::microseconds rtt) {
  if (transport_socket_) {
    transport_socket_->configureInitialCongestionWindow(bandwidth_bits_per_sec,
                                                        rtt);
  }
}

optional<uint64_t> ConnectionImpl::congestionWindowInBytes() const {
  // This would query the socket for TCP info
  return nullopt;
}

bool ConnectionImpl::shouldDrainReadBuffer() {
  return read_disable_count_ == 0;
}

void ConnectionImpl::setTransportSocketIsReadable() {
  // Remember that transport requested read resumption
  // This follows the reference pattern for handling transport read requests
  transport_wants_read_ = true;

  // Only activate read if not read disabled
  if (read_disable_count_ == 0 && file_event_) {
    file_event_->activate(static_cast<uint32_t>(event::FileReadyType::Read));
  }
}

void ConnectionImpl::raiseEvent(ConnectionEvent event) {
  raiseConnectionEvent(event);
}

void ConnectionImpl::flushWriteBuffer() {
  // Flush any pending data in write buffer
  // Called by transport socket when it needs to send data immediately
  // Flow: Transport has data -> flushWriteBuffer -> doWrite -> Transport adds
  // data -> Socket write Zero-copy: Transport manipulates write_buffer_
  // directly, no intermediate allocation

  // Simply trigger a write, which will call transport's doWrite to process the
  // buffer The transport will add any pending data during the doWrite call
  doWrite();
}

void ConnectionImpl::setTransportSocketConnectTimeout(
    std::chrono::milliseconds timeout) {
  transport_connect_timeout_ = timeout;
}

void ConnectionImpl::connect() {
  connecting_ = true;

  // Transition state machine to Connecting
  if (state_machine_) {
    state_machine_->handleEvent(
        ConnectionStateMachineEvent::ConnectionRequested);
  }

  doConnect();
}

void ConnectionImpl::addWriteFilter(WriteFilterSharedPtr filter) {
  filter_manager_.addWriteFilter(filter);
}

void ConnectionImpl::addFilter(FilterSharedPtr filter) {
  filter_manager_.addFilter(filter);
}

void ConnectionImpl::addReadFilter(ReadFilterSharedPtr filter) {
  filter_manager_.addReadFilter(filter);
}

void ConnectionImpl::removeReadFilter(ReadFilterSharedPtr filter) {
  filter_manager_.removeReadFilter(filter);
}

bool ConnectionImpl::initializeReadFilters() {
  return filter_manager_.initializeReadFilters();
}

// Private methods

void ConnectionImpl::onFileEvent(uint32_t events) {
  // Handle file events
  /**
   * FILE EVENT HANDLER - Core of async I/O
   *
   * Called by dispatcher when socket has events (read ready, write ready,
   * error) Flow: epoll/kqueue/select → Dispatcher → FileEventImpl →
   * onFileEvent()
   *
   * All callbacks are invoked in dispatcher thread context - thread-safe by
   * design
   *
   * Event types:
   * - Read: Socket has data available → onReadReady() → doRead()
   * - Write: Socket buffer has space → onWriteReady() → doWrite()
   * - Closed: Socket closed/error → closeSocket()
   */

  // Check for immediate error first (following reference pattern)
  if (immediate_error_event_ == ConnectionEvent::LocalClose ||
      immediate_error_event_ == ConnectionEvent::RemoteClose) {
    closeSocket(immediate_error_event_);
    return;
  }

  if (events & static_cast<uint32_t>(event::FileReadyType::Closed)) {
    // Remote close detected
    detected_close_type_ = DetectedCloseType::RemoteReset;
    closeSocket(ConnectionEvent::RemoteClose);
    return;
  }

  if (events & static_cast<uint32_t>(event::FileReadyType::Write)) {
    onWriteReady();
  }

  // Check if socket is still open after write handling
  if (socket_->isOpen() &&
      (events & static_cast<uint32_t>(event::FileReadyType::Read))) {
    // Process read event
    onReadReady();
  }
}

void ConnectionImpl::onReadReady() {
  /**
   * READ READY HANDLER
   *
   * Socket has data available for reading
   * Flow: onFileEvent(Read) → onReadReady() → doRead() →
   * filter_manager_.onData()
   *
   * Steps:
   * 1. Read from socket into read_buffer_
   * 2. Pass data through read filter chain
   * 3. Filters parse protocols (HTTP, SSE, JSON-RPC)
   * 4. Callbacks deliver parsed messages to application
   */

  // Notify state machine of read ready event
  if (state_machine_) {
    state_machine_->handleEvent(ConnectionStateMachineEvent::ReadReady);
  }
  doRead();
}

void ConnectionImpl::onWriteReady() {
  /**
   * WRITE READY HANDLER
   *
   * Socket buffer has space, can write data
   * Flow: onFileEvent(Write) → onWriteReady() → doWrite()
   *
   * Two cases:
   * 1. Connection in progress: Complete connection, enable read/write events
   * 2. Connected: Flush any pending data in write_buffer_ to socket
   */
  write_ready_ = true;
  write_event_count_++;  // Track write events for debugging

  // Prevent busy loop: if we have no data to write and we're already connected,
  // just return without processing. However, for stdio connections that start
  // as connected, we need to allow at least one initial write to properly
  // initialize the transport.
  bool is_stdio = transport_socket_ && transport_socket_->protocol() == "stdio";
  if (!connecting_ && write_buffer_.length() == 0 && !write_half_closed_ &&
      initial_write_done_ && !is_stdio) {
    // For non-stdio connections, prevent busy loop
    return;
  } else if (!connecting_ && write_buffer_.length() == 0 &&
             !write_half_closed_ && is_stdio && initial_write_done_) {
    // For stdio connections, also prevent busy loop after initial write
    return;
  }

  // Notify state machine of write ready event
  if (state_machine_) {
    state_machine_->handleEvent(ConnectionStateMachineEvent::WriteReady);
  }

  if (connecting_) {
    // Connection completed
    connecting_ = false;
    connected_ = true;
    state_ = ConnectionState::Open;

    // Notify state machine of connection success
    if (state_machine_) {
      state_machine_->handleEvent(ConnectionStateMachineEvent::SocketConnected);
    }

    // Notify transport socket (reference pattern)
    onConnected();

    raiseConnectionEvent(ConnectionEvent::Connected);

    // Flush any pending write data (reference pattern)
    // Transport may have queued data during handshake
    flushWriteBuffer();

    // Enable read events, write events only if there's data to write
    // This prevents busy loop with level-triggered events when write buffer is
    // empty
    uint32_t events_to_enable =
        static_cast<uint32_t>(event::FileReadyType::Read);
    if (write_buffer_.length() > 0) {
      events_to_enable |= static_cast<uint32_t>(event::FileReadyType::Write);
    }
    enableFileEvents(events_to_enable);
  } else {
    doWrite();
    initial_write_done_ = true;  // Mark that we've done at least one write
    // After writing, only keep write events enabled if there's more data to
    // write This prevents busy loop with level-triggered events
    uint32_t events_to_enable =
        static_cast<uint32_t>(event::FileReadyType::Read);
    if (write_buffer_.length() > 0) {
      events_to_enable |= static_cast<uint32_t>(event::FileReadyType::Write);
    }
    enableFileEvents(events_to_enable);
  }
}

void ConnectionImpl::closeThroughFilterManager(ConnectionEvent close_type) {
  if (state_ == ConnectionState::Closed) {
    return;
  }

  // Process any pending data in read buffer before closing
  // This ensures filters see all data before connection close
  if (read_buffer_.length() > 0) {
    processReadBuffer();
  }

  // CRITICAL: Use deferred deletion pattern to prevent use-after-free
  // Problem: When EOF is detected in doRead(), calling closeSocket() directly
  //          can cause the ConnectionImpl (managed by unique_ptr) to be
  //          destroyed while still executing doRead(), causing segfault on
  //          return.
  // Solution: Defer the actual close to the next event loop iteration using a
  //          0-delay timer. This ensures the object remains valid throughout
  //          the current call stack.
  // Flow: EOF detected → closeThroughFilterManager → disable events →
  //       schedule timer → return safely → timer fires → closeSocket
  if (!deferred_delete_) {
    deferred_delete_ = true;

    // Step 1: Disable file events immediately to prevent further I/O events
    // This stops new read/write events from being processed while close is
    // pending
    if (file_event_) {
      file_event_->setEnabled(0);
    }

    // Step 2: Schedule the actual close for the next event loop iteration
    // The timer with 0 delay ensures closeSocket runs after current stack
    // unwinds
    auto close_timer = dispatcher_.createTimer(
        [this, close_type]() { closeSocket(close_type); });
    close_timer->enableTimer(std::chrono::milliseconds(0));
  }
}

void ConnectionImpl::closeSocket(ConnectionEvent close_type) {
  if (state_ == ConnectionState::Closed) {
    return;
  }

  // Set state to Closed immediately to prevent re-entrancy
  state_ = ConnectionState::Closed;

  // Transition state machine to Closed
  if (state_machine_) {
    state_machine_->handleEvent(ConnectionStateMachineEvent::SocketClosed);
  }

  // Disable all file events
  file_event_.reset();

  // Cancel timers
  if (delayed_close_timer_) {
    delayed_close_timer_->disableTimer();
  }
  if (transport_connect_timer_) {
    transport_connect_timer_->disableTimer();
  }

  // Close transport socket
  if (transport_socket_) {
    transport_socket_->closeSocket(close_type);
  }

  // Drain buffers (reference pattern)
  // This prevents buffer fragments from outliving the connection
  write_buffer_.drain(write_buffer_.length());
  read_buffer_.drain(read_buffer_.length());

  // Close actual socket
  socket_->close();

  // Raise close event
  raiseConnectionEvent(close_type);
}

void ConnectionImpl::doConnect() {
  // CRITICAL FIX: Notify transport socket about connection attempt BEFORE TCP
  // connect Flow: Transport prepare → TCP connect → Connection events →
  // Transport onConnected Why: The transport socket (e.g.,
  // HttpSseTransportSocket) has its own state machine that must be initialized
  // before the TCP connection. Without this, the transport remains in
  // Initialized state when onConnected() is called, causing invalid state
  // transitions and crashes. This fix ensures proper sequencing:
  //   1. Transport transitions from Initialized → TcpConnecting
  //   2. TCP connection establishes
  //   3. onConnected() transitions from TcpConnecting → TcpConnected
  if (transport_socket_) {
    auto transport_result = transport_socket_->connect(*socket_);
    // Check if transport rejected the connection (returns Error instead of
    // nullptr) VoidResult is variant<nullptr_t, Error> where nullptr = success
    if (!mcp::holds_alternative<std::nullptr_t>(transport_result)) {
      // Transport socket rejected the connection - abort
      immediate_error_event_ = ConnectionEvent::LocalClose;
      // Activate write event to trigger error handling on next loop
      if (file_event_) {
        file_event_->activate(
            static_cast<uint32_t>(event::FileReadyType::Write));
      }
      return;
    }
  }

  // Now proceed with actual TCP connection
  auto result =
      socket_->connect(socket_->connectionInfoProvider().remoteAddress());

  if (result.ok() && *result == 0) {
    // Immediate connection success (rare for TCP but can happen with local
    // connections) Schedule the Connected event to be handled in the next
    // dispatcher iteration This ensures all callbacks are invoked in proper
    // dispatcher thread context
    connecting_ = false;
    connected_ = true;
    state_ = ConnectionState::Open;
    write_ready_ = true;  // Socket is immediately ready for writing

    // We're already in the dispatcher thread, just call directly

    // Notify state machine of connection success - this cancels connect timer
    if (state_machine_) {
      state_machine_->handleEvent(ConnectionStateMachineEvent::SocketConnected);
    }

    // Notify transport socket (must be before raising event)
    onConnected();

    raiseConnectionEvent(ConnectionEvent::Connected);
    // CRITICAL FIX: Only enable Read events initially.
    // Write events should only be enabled when there's data to send.
    // Enabling both causes busy loop on macOS/kqueue.
    enableFileEvents(static_cast<uint32_t>(event::FileReadyType::Read));
  } else if (!result.ok() && result.error_code() == EINPROGRESS) {
    // Connection in progress, wait for write ready
    // Note: Only Write needed here since connection isn't established yet
    enableFileEvents(static_cast<uint32_t>(event::FileReadyType::Write));
  } else {
    // Connection failed
    immediate_error_event_ = ConnectionEvent::RemoteClose;
    connecting_ = false;
    // Activate write event to trigger error handling on next loop
    if (file_event_) {
      file_event_->activate(static_cast<uint32_t>(event::FileReadyType::Write));
    }
  }
}

void ConnectionImpl::raiseConnectionEvent(ConnectionEvent event) {
  // Use base class callbacks_ member for connection callbacks
  // This consolidates callback management in one place

  // SAFETY FIX: Safely iterate over callbacks with null check
  // Flow: Iterate callbacks → Check null → Invoke onEvent
  // Why: During destruction or error handling, callbacks_ vector may contain
  // null entries or be partially destroyed. The null check prevents crashes
  // from dereferencing invalid pointers. This was causing segfaults when
  // connection errors occurred during shutdown.
  for (auto* cb : callbacks_) {
    if (cb) {
      cb->onEvent(event);
    }
  }

  filter_manager_.onConnectionEvent(event);
}

void ConnectionImpl::onConnected() {
  // Notify transport socket of connection completion
  // This follows reference pattern for connection lifecycle
  if (transport_socket_) {
    transport_socket_->onConnected();
  }
}

void ConnectionImpl::doRead() {
  /**
   * CORE READ FUNCTION - Reads data from socket and processes through filters
   *
   * Flow:
   * 1. doReadFromSocket() - Read from socket/transport into read_buffer_
   * 2. processReadBuffer() - Pass through filter chain (HTTP, SSE, JSON-RPC)
   * 3. Filters invoke callbacks to deliver parsed messages to application
   *
   * Continues reading in loop until:
   * - Socket buffer is empty (EAGAIN)
   * - EOF detected (connection closed)
   * - Error occurs
   *
   * Thread safety: All operations in dispatcher thread
   */

  if (read_disable_count_ > 0 || state_ != ConnectionState::Open) {
    // Don't clear transport_wants_read_ when returning early
    return;
  }

  // Clear transport wants read just before reading (reference pattern)
  transport_wants_read_ = false;

  while (true) {
    // Read from socket into buffer
    auto result = doReadFromSocket();

    // Check for errors
    if (!result.ok()) {
      // Socket error - use deferred close for safety
      closeThroughFilterManager(ConnectionEvent::RemoteClose);
      return;
    }

    // Check the action to take based on the result
    if (result.action_ == TransportIoResult::CLOSE) {
      // Transport indicated connection should be closed
      // Use deferred close to prevent use-after-free
      closeThroughFilterManager(ConnectionEvent::RemoteClose);
      return;
    }

    // Check if we got any data
    if (result.bytes_processed_ == 0) {
      // No data available right now (EAGAIN case handled by transport returning
      // stop()) or EOF (handled by transport returning endStream with
      // end_stream_read_ = true)
      if (result.end_stream_read_) {
        // EOF detected - the remote end has closed the connection
        read_half_closed_ = true;
        detected_close_type_ = DetectedCloseType::RemoteReset;

        // SAFETY: Must use closeThroughFilterManager instead of closeSocket
        // to avoid use-after-free when ConnectionImpl is destroyed during close
        closeThroughFilterManager(ConnectionEvent::RemoteClose);
        return;
      }

      // No data available, but not EOF - just stop reading for now
      // The event loop will trigger another read when data is available
      break;
    }

    // Update stats
    updateReadBufferStats(result.bytes_processed_, read_buffer_.length());

    // Process through filter chain
    processReadBuffer();

    if (read_disable_count_ > 0) {
      // Reading was disabled during processing
      break;
    }
  }
}

TransportIoResult ConnectionImpl::doReadFromSocket() {
  // Read from transport socket or directly from socket

  // Use transport socket for reading if available
  // TODO: Fix transport socket implementation for HTTP/SSE
  // For now, check if this is a real transport socket (not RawTransportSocket)
  if (transport_socket_ &&
      dynamic_cast<RawTransportSocket*>(transport_socket_.get()) == nullptr) {
    auto result = transport_socket_->doRead(read_buffer_);
    return result;
  }

  // Read directly from socket (working path)

  // Read from socket (IoHandle will manage buffer space)
  auto io_result = socket_->ioHandle().read(read_buffer_);

  // Convert IoCallResult to TransportIoResult
  if (!io_result.ok()) {
    // Socket error

    // Check if it's just EAGAIN/EWOULDBLOCK
    if (io_result.wouldBlock()) {
      // No data available right now, not an error
      return TransportIoResult::stop();
    }

    Error err;
    err.code = io_result.error_code();
    err.message = io_result.error_info ? io_result.error_info->message
                                       : "Socket read error";
    return TransportIoResult::error(err);
  }

  size_t bytes_read = *io_result;

  // Check for EOF
  if (bytes_read == 0) {
    // EOF - connection closed
    return TransportIoResult::close();
  }

  return TransportIoResult::success(bytes_read);
}

void ConnectionImpl::processReadBuffer() {
  if (read_buffer_.length() > 0) {
    filter_manager_.onRead();
  }
}

void ConnectionImpl::doWrite() {
  /**
   * CORE WRITE FUNCTION - Writes data from write_buffer_ to socket
   *
   * Flow:
   * 1. Check transport socket for any pending data (e.g., TLS handshake)
   * 2. Write from write_buffer_ to socket
   * 3. Continue writing until buffer empty or socket buffer full (EAGAIN)
   *
   * Called from:
   * - onWriteReady() when socket becomes writable
   * - write() when new data is added and socket is ready
   *
   * Thread safety: All operations in dispatcher thread
   */
  if (state_ != ConnectionState::Open) {
#ifndef NDEBUG
    std::cerr << "[CONN] doWrite(): state != Open, returning" << std::endl;
#endif
    return;
  }

#ifndef NDEBUG
  std::cerr << "[CONN] doWrite(): starting, buffer_len=" << write_buffer_.length()
            << " transport_socket_=" << (transport_socket_ ? "yes" : "no") << std::endl;
#endif

  // Use transport socket for initial processing if available
  // This is essential for stdio transport which manages pipe bridging
  // TODO: Fix transport socket implementation for HTTP/SSE
  if (transport_socket_ &&
      dynamic_cast<RawTransportSocket*>(transport_socket_.get()) == nullptr) {
#ifndef NDEBUG
    std::cerr << "[CONN] doWrite(): using transport socket, protocol="
              << transport_socket_->protocol() << std::endl;
#endif
    // Let transport process any pending operations
    // For stdio, this ensures the bridge threads are active
    auto result = transport_socket_->doWrite(write_buffer_, write_half_closed_);
    if (!result.ok()) {
#ifndef NDEBUG
      std::cerr << "[CONN] doWrite(): transport error, closing" << std::endl;
#endif
      closeSocket(ConnectionEvent::LocalClose);
      return;
    }
    if (result.action_ == TransportIoResult::CLOSE) {
#ifndef NDEBUG
      std::cerr << "[CONN] doWrite(): transport requested close" << std::endl;
#endif
      closeSocket(ConnectionEvent::LocalClose);
      return;
    }
    // If transport handled all data, we're done
    if (write_buffer_.length() == 0) {
#ifndef NDEBUG
      std::cerr << "[CONN] doWrite(): transport handled all data, enabling Read"
                << std::endl;
#endif
      // CRITICAL FIX: Must enable Read events before returning!
      // Otherwise the caller (write()) left us with only Write events enabled.
      enableFileEvents(static_cast<uint32_t>(event::FileReadyType::Read));
#ifndef NDEBUG
      // Debug: Check if socket has pending data
      if (socket_) {
        int bytes_available = 0;
        if (ioctl(socket_->ioHandle().fd(), FIONREAD, &bytes_available) == 0) {
          std::cerr << "[CONN] doWrite(): socket has " << bytes_available
                    << " bytes pending" << std::endl << std::flush;
        }
      }
#endif
      return;
    }
  }

  // Now check if we have data to write after transport processing
  if (write_buffer_.length() == 0) {
#ifndef NDEBUG
    std::cerr << "[CONN] doWrite(): buffer empty after transport, enabling Read"
              << std::endl;
#endif
    // CRITICAL FIX: Must enable Read events before returning!
    enableFileEvents(static_cast<uint32_t>(event::FileReadyType::Read));
    return;
  }

  // Keep writing while buffer has data
  while (write_buffer_.length() > 0) {
    TransportIoResult write_result;

    // Try to write through transport socket if available
    // For stdio connections, the transport manages the pipe bridging
    // TODO: Fix transport socket implementation for HTTP/SSE
    if (transport_socket_ &&
        dynamic_cast<RawTransportSocket*>(transport_socket_.get()) == nullptr) {
      write_result =
          transport_socket_->doWrite(write_buffer_, write_half_closed_);
    } else {
      // Write directly to socket
      auto io_result = socket_->ioHandle().write(write_buffer_);

      // Convert IoCallResult to TransportIoResult
      if (!io_result.ok()) {
        // Socket error
        if (io_result.wouldBlock()) {
          // Can't write more right now, not an error
          write_result = TransportIoResult::stop();
        } else {
          Error err;
          err.code = io_result.error_code();
          err.message = io_result.error_info ? io_result.error_info->message
                                             : "Socket write error";
          write_result = TransportIoResult::error(err);
        }
      } else {
        size_t bytes_written = *io_result;
        write_result = TransportIoResult::success(bytes_written);
      }
    }

    if (!write_result.ok()) {
      closeThroughFilterManager(ConnectionEvent::LocalClose);
      return;
    }

    if (write_result.action_ == TransportIoResult::CLOSE) {
      closeThroughFilterManager(ConnectionEvent::LocalClose);
      return;
    }

    // Check if transport couldn't write (would block)
    if (write_result.bytes_processed_ == 0 &&
        write_result.action_ == TransportIoResult::CONTINUE) {
      // Socket would block, enable write events
      enableFileEvents(static_cast<uint32_t>(event::FileReadyType::Write));
      break;
    }

    // Update stats based on what transport wrote
    updateWriteBufferStats(write_result.bytes_processed_,
                           write_buffer_.length());

    // Check watermarks
    if (above_high_watermark_ && write_buffer_.length() < low_watermark_) {
      above_high_watermark_ = false;
      for (auto& cb : watermark_callbacks_) {
        cb->onBelowWriteBufferLowWatermark();
      }
    }

    // If transport wrote everything, we're done
    if (write_buffer_.length() == 0) {
      break;
    }

    // Continue loop to write more if buffer still has data
  }

  // Keep both Read and Write events enabled after writing
  // This ensures proper event handling for both client and server
#ifndef NDEBUG
  std::cerr << "[CONN] doWrite(): done, buffer_len=" << write_buffer_.length()
            << std::endl;
#endif
  if (write_buffer_.length() == 0) {
#ifndef NDEBUG
    std::cerr << "[CONN] doWrite(): enabling Read events" << std::endl;
#endif
    // Finished writing current data - only enable read events
    // Write events will be enabled when new data arrives to prevent busy loop
    enableFileEvents(static_cast<uint32_t>(event::FileReadyType::Read));

    // CRITICAL: Handle edge-triggered race condition
    // With edge-triggered events, if data arrives while we're in the process of
    // enabling Read events, we miss the edge and never get a Read event
    // notification.
    //
    // The problem is a race condition with two possible scenarios:
    //
    // Scenario 1:
    // 1. Client writes request
    // 2. Client enables Read events
    // 3. Server processes request and sends response
    // 4. Response arrives at client TCP buffer
    // 5. But no edge transition because Read was already enabled
    //
    // Scenario 2:
    // 1. Client writes request
    // 2. Server processes and sends response quickly
    // 3. Response arrives at client TCP buffer
    // 4. Client enables Read events
    // 5. No edge because data was already there
    //
    // Solution: Activate a read event to check for any data that may have
    // arrived This forces the event loop to check the socket for available data
    // NOTE: Disabled for level-triggered events on macOS to avoid race
    // conditions Level-triggered events will naturally fire when data is
    // available For edge-triggered, we need to check for data that may have
    // arrived
    if (file_event_ &&
        event::PlatformDefaultTriggerType == event::FileTriggerType::Edge) {
      // Activate read to check for any data that may have arrived during the
      // race window
      file_event_->activate(static_cast<uint32_t>(event::FileReadyType::Read));
    }
  }

  if (write_buffer_.length() == 0 && write_half_closed_) {
    // All data written and we're closing
    if (state_ == ConnectionState::Closing) {
      closeThroughFilterManager(ConnectionEvent::LocalClose);
    }
  }
}

// doWriteToSocket removed - doWrite now handles socket write directly for
// zero-copy

void ConnectionImpl::handleWrite(bool all_data_sent) {
  if (all_data_sent) {
    disableFileEvents(static_cast<uint32_t>(event::FileReadyType::Write));
  }
}

void ConnectionImpl::setReadBufferReady() {
  dispatcher_.post([this]() {
    if (state_ == ConnectionState::Open && read_disable_count_ == 0) {
      processReadBuffer();
    }
  });
}

void ConnectionImpl::updateReadBufferStats(uint64_t num_read,
                                           uint64_t new_size) {
  if (stats_.has_value()) {
    ConnectionStats& stats = *stats_;
    ConnectionUtility::updateBufferStats(num_read, new_size,
                                         last_read_buffer_size_, stats);
  }
  last_read_buffer_size_ = new_size;
}

void ConnectionImpl::updateWriteBufferStats(uint64_t num_written,
                                            uint64_t new_size) {
  if (stats_.has_value()) {
    ConnectionStats& stats = *stats_;
    ConnectionUtility::updateBufferStats(num_written, new_size,
                                         last_write_buffer_size_, stats);
  }
  last_write_buffer_size_ = new_size;
}

void ConnectionImpl::onDelayedCloseTimeout() {
  delayed_close_pending_ = false;
  closeSocket(ConnectionEvent::LocalClose);
}

void ConnectionImpl::onConnectTimeout() {
  closeSocket(ConnectionEvent::LocalClose);
}

void ConnectionImpl::enableFileEvents(uint32_t events) {
  // CRITICAL FIX: Set events directly instead of OR-ing them.
  // The previous code used |= which would ADD events but never REMOVE them.
  // This caused Write events to stay enabled even when we only wanted Read,
  // which on macOS/kqueue with level-triggered events causes a busy loop
  // where continuous Write events mask Read events.
  //
  // The fix: Directly set the events we want, replacing the previous state.
  // Callers should specify all events they want enabled (e.g., Read | Write
  // if both are needed, or just Read if only Read is needed).
  file_event_state_ = events;
  if (file_event_) {
    file_event_->setEnabled(file_event_state_);
  }
}

void ConnectionImpl::disableFileEvents(uint32_t events) {
  file_event_state_ &= ~events;
  if (file_event_) {
    file_event_->setEnabled(file_event_state_);
  }
}

uint32_t ConnectionImpl::getReadyEvents() {
  uint32_t events = 0;

  // Only report write readiness when we have data to write
  // This follows the level-triggered event model where we only
  // signal when action is needed, preventing busy loops
  if (write_buffer_.length() > 0) {
    events |= static_cast<uint32_t>(event::FileReadyType::Write);
  }

  if (read_buffer_.length() > 0) {
    events |= static_cast<uint32_t>(event::FileReadyType::Read);
  }

  return events;
}

// ConnectionUtility implementation

void ConnectionUtility::updateBufferStats(uint64_t delta,
                                          uint64_t new_total,
                                          uint64_t& previous_total,
                                          ConnectionStats& stats) {
  if (new_total > previous_total) {
    stats.read_total_ += (new_total - previous_total);
    stats.read_current_ = new_total;
  } else if (new_total < previous_total) {
    stats.write_total_ += (previous_total - new_total);
    stats.write_current_ = new_total;
  }
}

bool ConnectionUtility::applySocketOptions(
    Socket& socket, const SocketOptionsSharedPtr& options) {
  if (!options) {
    return true;
  }

  for (const auto& option : *options) {
    if (!option->setOption(socket)) {
      return false;
    }
  }

  return true;
}

optional<Connection::UnixDomainSocketPeerCredentials>
ConnectionUtility::getUnixSocketPeerCredentials(const Socket& socket) {
  // This would use platform-specific APIs to get peer credentials
  // For now, return empty
  return nullopt;
}

void ConnectionUtility::configureSocket(Socket& socket, bool is_server) {
  // Set socket to non-blocking mode
  socket.setBlocking(false);

  // Enable TCP keep-alive
  int val = 1;
  socket.setSocketOption(SOL_SOCKET, SO_KEEPALIVE, &val, sizeof(val));

  // Disable Nagle's algorithm for low latency
  socket.setSocketOption(IPPROTO_TCP, TCP_NODELAY, &val, sizeof(val));

  if (is_server) {
    // Server-specific socket options
    socket.setSocketOption(SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));
  }
}

// ConnectionEventLogger implementation

ConnectionEventLogger::ConnectionEventLogger(const Connection& connection)
    : connection_(connection) {
  // Generate connection ID for logging
  std::vector<uint8_t> hash;
  connection.hashKey(hash);

  std::stringstream ss;
  for (auto byte : hash) {
    ss << std::hex << static_cast<int>(byte);
  }
  connection_id_ = ss.str();
}

void ConnectionEventLogger::logEvent(ConnectionEvent event,
                                     const std::string& details) {
  // Log connection events
  const char* event_name = nullptr;
  switch (event) {
    case ConnectionEvent::Connected:
      event_name = "Connected";
      break;
    case ConnectionEvent::RemoteClose:
      event_name = "RemoteClose";
      break;
    case ConnectionEvent::LocalClose:
      event_name = "LocalClose";
      break;
    case ConnectionEvent::ConnectedZeroRtt:
      event_name = "ConnectedZeroRtt";
      break;
  }

  if (event_name) {
    // Would log: [connection_id_] Event: event_name details
  }
}

void ConnectionEventLogger::logRead(size_t bytes_read, size_t buffer_size) {
  // Would log: [connection_id_] Read: bytes_read bytes, buffer size:
  // buffer_size
}

void ConnectionEventLogger::logWrite(size_t bytes_written, size_t buffer_size) {
  // Would log: [connection_id_] Write: bytes_written bytes, buffer size:
  // buffer_size
}

void ConnectionEventLogger::logError(const std::string& error) {
  // Would log: [connection_id_] Error: error
}

// ConnectionImpl state machine integration

void ConnectionImpl::onStateChanged(ConnectionState old_state,
                                    ConnectionState new_state) {
  // This function is no longer needed as state changes are handled in the
  // lambda registered with the state machine
}

void ConnectionImpl::configureStateMachine() {
  // Configure state machine behavior
  // Called during initialization

  if (!state_machine_) {
    return;
  }

  // State machine is configured via the lambda registered in constructor
}

}  // namespace network
}  // namespace mcp
