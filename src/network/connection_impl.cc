#include "mcp/network/connection_impl.h"

#include <algorithm>
#include <iostream>
#include <sstream>

#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>

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
  void setTransportSocketCallbacks(TransportSocketCallbacks& callbacks) override {
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
    return bytes == 0 ? TransportIoResult::close() : TransportIoResult::success(bytes);
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
      return TransportIoResult::error(Error{result.error_code(), "Write error"});
    }
    return TransportIoResult::success(*result);
  }
  void onConnected() override {}
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
    : ConnectionImplBase(
          dispatcher, std::move(socket), 
          transport_socket ? std::move(transport_socket) 
                          : std::make_unique<RawTransportSocket>()) {
  // Set initial state
  connecting_ = !connected;
  state_ = connected ? ConnectionState::Open : ConnectionState::Open;

  // Set up transport socket callbacks
  if (transport_socket_) {
    transport_socket_->setTransportSocketCallbacks(
        static_cast<TransportSocketCallbacks&>(*this));
    
    // CRITICAL: For server connections, notify transport socket that connection is established
    // Server connections are already connected when created (socket was accepted)
    // Without this, HTTP+SSE transport won't initialize properly for server mode
    // Flow: Accept socket → Create connection (connected=true) → Notify transport
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
        // Use level-triggered events for all sockets to ensure we don't miss
        // data that arrives before event registration completes
        // TODO: Optimize to edge-triggered once event registration timing is fixed
        auto trigger_type = event::FileTriggerType::Level;

        // Following production pattern: create file event with Read and Write events
        // enabled from the start. The connection will manage flow control as needed.
        std::cerr << "[DEBUG] Creating file event for fd: " << socket_->ioHandle().fd() 
                  << " trigger_type: " << (trigger_type == event::FileTriggerType::Level ? "Level" : "Edge") << std::endl;
        
        uint32_t initial_events = static_cast<uint32_t>(event::FileReadyType::Read) |
                                 static_cast<uint32_t>(event::FileReadyType::Write);
        
        file_event_ = dispatcher_.createFileEvent(
            socket_->ioHandle().fd(),
            [this](uint32_t events) { onFileEvent(events); }, trigger_type,
            initial_events);
        
        std::cerr << "[DEBUG] File event created: " << (file_event_ ? "YES" : "NO") 
                  << " with initial events: " << initial_events << std::endl;
        
        // Track which events are enabled
        file_event_state_ = initial_events;
        
        // For client connections that are connecting, we might need to adjust events
        if (!connected && connecting_) {
          // Client connection in progress - will be handled by doConnect()
          std::cerr << "[DEBUG] Client connection in progress, events will be managed by doConnect" << std::endl;
        }
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
  int val = enable ? 1 : 0;
  socket_->setSocketOption(IPPROTO_TCP, TCP_NODELAY, &val, sizeof(val));
}

ReadDisableStatus ConnectionImpl::readDisableWithStatus(bool disable) {
  if (disable) {
    if (read_disable_count_ == 0) {
      // First disable
      disableFileEvents(static_cast<uint32_t>(event::FileReadyType::Read));
    }
    read_disable_count_++;
    return ReadDisableStatus::TransitionedToReadDisabled;
  } else {
    if (read_disable_count_ > 0) {
      read_disable_count_--;
      if (read_disable_count_ == 0) {
        // Re-enable reads
        enableFileEvents(static_cast<uint32_t>(event::FileReadyType::Read));
        if (read_buffer_.length() > 0) {
          // Process any buffered data
          processReadBuffer();
        }
        return ReadDisableStatus::TransitionedToReadEnabled;
      }
    }
    return ReadDisableStatus::StillReadDisabled;
  }
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
  if (state_ != ConnectionState::Open || write_half_closed_) {
    return;
  }

  if (end_stream) {
    write_half_closed_ = true;
  }

  // Filter chain processes the write
  filter_manager_.onWrite();

  // Move data to write buffer
  // CRITICAL: The move() function moves data FROM the source TO the destination
  // We need to move FROM data TO write_buffer_, so data should be the source
  // The correct call is: data.move(write_buffer_) not write_buffer_.move(data)
  data.move(write_buffer_);  // Move FROM data TO write_buffer_

  // Update stats
  updateWriteBufferStats(data.length(), write_buffer_.length());

  // Check watermarks
  if (write_buffer_.length() > high_watermark_ && !above_high_watermark_) {
    above_high_watermark_ = true;
    for (auto& cb : watermark_callbacks_) {
      cb->onAboveWriteBufferHighWatermark();
    }
  }

  // Schedule write
  if (!write_scheduled_) {
    write_scheduled_ = true;
    dispatcher_.post([this]() {
      write_scheduled_ = false;
      doWrite();
    });
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
  if (read_disable_count_ == 0) {
    setReadBufferReady();
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
  // Handle file events from event loop
  // Flow: epoll/kqueue event -> Dispatcher -> onFileEvent -> Handle
  // read/write/close All callbacks are invoked in dispatcher thread context

  std::cerr << "[DEBUG] onFileEvent called with events: " << events << std::endl;

  if (events & static_cast<uint32_t>(event::FileReadyType::Write)) {
    std::cerr << "[DEBUG] Write ready event" << std::endl;
    onWriteReady();
  }

  if (events & static_cast<uint32_t>(event::FileReadyType::Read)) {
    std::cerr << "[DEBUG] Read ready event" << std::endl;
    onReadReady();
  }

  if (events & static_cast<uint32_t>(event::FileReadyType::Closed)) {
    std::cerr << "[DEBUG] Closed event" << std::endl;
    // Remote close detected
    detected_close_type_ = DetectedCloseType::RemoteReset;
    closeSocket(ConnectionEvent::RemoteClose);
  }
}

void ConnectionImpl::onReadReady() {
  std::cerr << "[DEBUG] ConnectionImpl::onReadReady() called" << std::endl;
  
  // Notify state machine of read ready event
  if (state_machine_) {
    state_machine_->handleEvent(ConnectionStateMachineEvent::ReadReady);
  }
  doRead();
}

void ConnectionImpl::onWriteReady() {
  write_ready_ = true;

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

    raiseConnectionEvent(ConnectionEvent::Connected);

    // Enable read events
    enableFileEvents(static_cast<uint32_t>(event::FileReadyType::Read));
  } else {
    doWrite();
  }
}

void ConnectionImpl::closeSocket(ConnectionEvent close_type) {
  if (state_ == ConnectionState::Closed) {
    return;
  }

  // Transition state machine to Closed
  if (state_machine_) {
    state_machine_->handleEvent(ConnectionStateMachineEvent::SocketClosed);
  }

  state_ = ConnectionState::Closed;

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
    if (!holds_alternative<std::nullptr_t>(transport_result)) {
      // Transport socket rejected the connection - abort
      immediate_error_event_ = true;
      closeSocket(ConnectionEvent::LocalClose);
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

    // Post the event to dispatcher to ensure proper thread context
    dispatcher_.post([this]() {
      raiseConnectionEvent(ConnectionEvent::Connected);
      enableFileEvents(static_cast<uint32_t>(event::FileReadyType::Read));
    });
  } else if (!result.ok() && result.error_code() == EINPROGRESS) {
    // Connection in progress, wait for write ready
    enableFileEvents(static_cast<uint32_t>(event::FileReadyType::Write));
  } else {
    // Connection failed
    immediate_error_event_ = true;
    closeSocket(ConnectionEvent::LocalClose);
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

void ConnectionImpl::doRead() {
  // Read data from socket through transport socket abstraction
  // Flow: Socket readable -> onFileEvent -> onReadReady -> doRead ->
  // Transport::doRead All operations happen in dispatcher thread context

  std::cerr << "[DEBUG] ConnectionImpl::doRead() called" << std::endl;
  
  if (read_disable_count_ > 0 || state_ != ConnectionState::Open) {
    std::cerr << "[DEBUG] doRead skipped: read_disable_count=" << read_disable_count_ 
              << " state=" << static_cast<int>(state_) << std::endl;
    return;
  }

  while (true) {
    // Read from socket into buffer
    std::cerr << "[DEBUG] About to doReadFromSocket()" << std::endl;
    auto result = doReadFromSocket();

    // Check for errors
    if (!result.ok()) {
      // Socket error - close the connection
      closeSocket(ConnectionEvent::RemoteClose);
      return;
    }

    // Check the action to take based on the result
    if (result.action_ == TransportIoResult::CLOSE) {
      // Transport indicated connection should be closed
      closeSocket(ConnectionEvent::RemoteClose);
      return;
    }

    // Check if we got any data
    if (result.bytes_processed_ == 0) {
      // No data available right now (EAGAIN case handled by transport returning
      // stop()) or EOF (handled by transport returning endStream with
      // end_stream_read_ = true)

      if (result.end_stream_read_) {
        // This is a real EOF - the other end closed the connection
        read_half_closed_ = true;
        detected_close_type_ = DetectedCloseType::RemoteReset;
        closeSocket(ConnectionEvent::RemoteClose);
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

  // If we have a transport socket, use it
  if (transport_socket_) {
    std::cerr << "[DEBUG] Calling transport_socket_->doRead()" << std::endl;
    auto result = transport_socket_->doRead(read_buffer_);
    std::cerr << "[DEBUG] doRead result: ok=" << result.ok() 
              << " bytes=" << result.bytes_processed_ 
              << " action=" << static_cast<int>(result.action_) << std::endl;
    return result;
  }
  
  // No transport socket - read directly from socket
  std::cerr << "[DEBUG] Reading directly from socket (no transport layer)" << std::endl;
  
  // Read from socket (IoHandle will manage buffer space)
  auto io_result = socket_->ioHandle().read(read_buffer_);
  
  // Convert IoCallResult to TransportIoResult
  if (!io_result.ok()) {
    // Socket error
    std::cerr << "[DEBUG] Socket read error: " << io_result.error_code() << std::endl;
    
    // Check if it's just EAGAIN/EWOULDBLOCK
    if (io_result.wouldBlock()) {
      // No data available right now, not an error
      return TransportIoResult::stop();
    }
    
    Error err;
    err.code = io_result.error_code();
    err.message = io_result.error_info ? io_result.error_info->message : "Socket read error";
    return TransportIoResult::error(err);
  }
  
  size_t bytes_read = *io_result;
  std::cerr << "[DEBUG] Direct socket read: bytes=" << bytes_read << std::endl;
  
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
  // Write data to socket through transport socket abstraction
  // Flow: write() -> doWrite -> Transport::doWrite -> Socket write
  // All operations happen in dispatcher thread context

  if (state_ != ConnectionState::Open) {
    return;
  }

  // Let transport socket process the buffer first if we have one
  // This allows the transport to add any pending data (like HTTP headers)
  // even if write_buffer_ is initially empty
  if (transport_socket_) {
    auto result = transport_socket_->doWrite(write_buffer_, write_half_closed_);
    if (!result.ok()) {
      // Transport error
      closeSocket(ConnectionEvent::LocalClose);
      return;
    }
    if (result.action_ == TransportIoResult::CLOSE) {
      // Transport wants to close
      closeSocket(ConnectionEvent::LocalClose);
      return;
    }
  }

  // Now check if we have data to write after transport processing
  if (write_buffer_.length() == 0) {
    return;
  }

  // Keep writing while buffer has data
  while (write_buffer_.length() > 0) {
    TransportIoResult write_result;
    
    if (transport_socket_) {
      // Call transport to write - it handles socket I/O and drains buffer
      write_result = transport_socket_->doWrite(write_buffer_, write_half_closed_);
    } else {
      // No transport socket - write directly to socket
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
          err.message = io_result.error_info ? io_result.error_info->message : "Socket write error";
          write_result = TransportIoResult::error(err);
        }
      } else {
        size_t bytes_written = *io_result;
        write_result = TransportIoResult::success(bytes_written);
      }
    }

    if (!write_result.ok()) {
      closeSocket(ConnectionEvent::LocalClose);
      return;
    }

    if (write_result.action_ == TransportIoResult::CLOSE) {
      closeSocket(ConnectionEvent::LocalClose);
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

  if (write_buffer_.length() == 0 && write_half_closed_) {
    // All data written and we're closing
    if (state_ == ConnectionState::Closing) {
      closeSocket(ConnectionEvent::LocalClose);
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
  std::cerr << "[DEBUG] enableFileEvents: events=" << events 
            << " current_state=" << file_event_state_ << std::endl;
  file_event_state_ |= events;
  if (file_event_) {
    std::cerr << "[DEBUG] Setting file event enabled state to: " << file_event_state_ << std::endl;
    file_event_->setEnabled(file_event_state_);
  } else {
    std::cerr << "[ERROR] No file_event_ to enable!" << std::endl;
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

  if (write_ready_ || write_buffer_.length() > 0) {
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