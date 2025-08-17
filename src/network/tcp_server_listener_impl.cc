/**
 * @file tcp_listener_impl.cc
 * @brief Simplified TCP listener implementation following production patterns
 */

#include "mcp/network/server_listener_impl.h"
#include "mcp/network/connection_impl.h"
#include "mcp/network/socket_impl.h"
#include "mcp/network/io_socket_handle_impl.h"
#include "mcp/stream_info/stream_info_impl.h"

#include <errno.h>
#include <fcntl.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

namespace mcp {
namespace network {

// Static member initialization
std::atomic<uint64_t> TcpActiveListener::next_listener_tag_{1};

// Placeholder for LoadShedPoint until we implement overload manager
class LoadShedPoint {
public:
  bool shouldShed() const { return false; }
};

// =================================================================
// BaseListenerImpl
// =================================================================

BaseListenerImpl::BaseListenerImpl(event::Dispatcher& dispatcher,
                                   SocketSharedPtr socket)
    : dispatcher_(dispatcher),
      socket_(std::move(socket)) {
  if (socket_) {
    local_address_ = socket_->connectionInfoProvider().localAddress();
  }
}

// =================================================================
// TcpListenerImpl - Simple and efficient like production code
// =================================================================

TcpListenerImpl::TcpListenerImpl(event::Dispatcher& dispatcher,
                                 std::mt19937& random,
                                 SocketSharedPtr socket,
                                 TcpListenerCallbacks& cb,
                                 bool bind_to_port,
                                 bool ignore_global_conn_limit,
                                 bool bypass_overload_manager,
                                 uint32_t max_connections_per_event,
                                 ThreadLocalOverloadStateOptRef overload_state)
    : BaseListenerImpl(dispatcher, std::move(socket)),
      cb_(cb),
      random_(random),
      bind_to_port_(bind_to_port),
      ignore_global_conn_limit_(ignore_global_conn_limit),
      bypass_overload_manager_(bypass_overload_manager),
      max_connections_per_event_(max_connections_per_event),
      overload_state_(overload_state) {
  
  // Create file event for accept but don't enable yet
  // Only if we're actually bound to a port
  if (bind_to_port_ && socket_) {
    file_event_ = dispatcher_.createFileEvent(
        socket_->ioHandle().fd(),
        [this](uint32_t events) { onSocketEvent(events); },
        event::FileTriggerType::Edge,  // Edge-triggered for efficiency
        static_cast<uint32_t>(event::FileReadyType::Read));
  }
}

TcpListenerImpl::~TcpListenerImpl() {
  disable();
  if (file_event_) {
    file_event_.reset();
  }
}

void TcpListenerImpl::disable() {
  if (!enabled_) {
    return;
  }
  
  enabled_ = false;
  if (file_event_) {
    file_event_->setEnabled(0);
  }
  
  cb_.onListenerDisabled();
}

void TcpListenerImpl::enable() {
  if (enabled_) {
    return;
  }
  
  enabled_ = true;
  if (file_event_) {
    file_event_->setEnabled(static_cast<uint32_t>(event::FileReadyType::Read));
  }
  
  cb_.onListenerEnabled();
}

void TcpListenerImpl::setRejectFraction(UnitFloat reject_fraction) {
  reject_fraction_ = reject_fraction;
}

void TcpListenerImpl::configureLoadShedPoints(LoadShedPoint& load_shed_point) {
  listener_accept_ = &load_shed_point;
}

void TcpListenerImpl::onSocketEvent(uint32_t events) {
  // Only handle read events (new connections)
  if (!(events & static_cast<uint32_t>(event::FileReadyType::Read))) {
    return;
  }
  
  // Accept up to max_connections_per_event_ connections
  // This batching improves performance under high connection rates
  uint32_t connections_accepted = 0;
  
  while (connections_accepted < max_connections_per_event_) {
    if (!doAccept()) {
      // Error or would block - stop accepting for now
      break;
    }
    connections_accepted++;
  }
  
  // For edge-triggered mode, reactivate if we accepted max connections
  // (there might be more pending)
  if (connections_accepted == max_connections_per_event_ && file_event_) {
    file_event_->activate(static_cast<uint32_t>(event::FileReadyType::Read));
  }
}

bool TcpListenerImpl::doAccept() {
  // Check global connection limit first (cheapest check)
  if (!ignore_global_conn_limit_ && rejectCxOverGlobalLimit()) {
    num_rejected_connections_++;
    return true;  // Return true to continue accepting other connections
  }
  
  // Check probabilistic rejection for gradual load shedding
  if (shouldRejectProbabilistically()) {
    num_rejected_connections_++;
    return true;  // Return true to continue accepting other connections
  }
  
  // Check load shed point from overload manager
  if (listener_accept_ && listener_accept_->shouldShed()) {
    num_rejected_connections_++;
    return true;
  }
  
  // Accept the connection
  sockaddr_storage addr;
  socklen_t addr_len = sizeof(addr);
  
  // Accept new connection
  int new_fd = ::accept(socket_->ioHandle().fd(),
                        reinterpret_cast<sockaddr*>(&addr),
                        &addr_len);
  
  if (new_fd < 0) {
    // Would block or error
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      return false;  // No more connections available
    }
    if (errno == EMFILE || errno == ENFILE) {
      // Out of file descriptors - this is serious
      // TODO: Log critical error
      return false;
    }
    // Other error - log and continue
    return false;
  }
  
  // Create IO handle for accepted socket
  auto io_handle = std::make_unique<IoSocketHandleImpl>(new_fd);
  
  // Set non-blocking mode immediately
  int flags = fcntl(new_fd, F_GETFL, 0);
  if (flags >= 0) {
    fcntl(new_fd, F_SETFL, flags | O_NONBLOCK);
  }
  
  // Set close-on-exec
  fcntl(new_fd, F_SETFD, FD_CLOEXEC);
  
  // Create address from sockaddr
  auto remote_address = Address::addressFromSockAddr(addr, addr_len);
  if (!remote_address) {
    ::close(new_fd);
    return true;  // Continue accepting
  }
  
  // Create connection socket with proper addresses
  auto connection_socket = std::make_unique<ConnectionSocketImpl>(
      std::move(io_handle), local_address_, remote_address);
  
  // Apply socket options to new connection
  if (socket_) {
    // TCP_NODELAY is commonly set for low latency
    int val = 1;
    connection_socket->setSocketOption(IPPROTO_TCP, TCP_NODELAY, &val, sizeof(val));
  }
  
  // Update metrics
  num_connections_++;
  
  // Hand off to callback
  // The callback will handle filter chains and connection creation
  cb_.onAccept(std::move(connection_socket));
  
  return true;
}

bool TcpListenerImpl::rejectCxOverGlobalLimit() const {
  // Check thread-local overload state if available
  if (overload_state_.has_value()) {
    auto& state = overload_state_.value().get();
    if (state.global_cx_count && 
        state.global_cx_count->load() >= state.global_cx_limit) {
      return true;
    }
  }
  return false;
}

bool TcpListenerImpl::shouldRejectProbabilistically() {
  if (reject_fraction_ == UnitFloat::min()) {
    return false;  // No rejection
  }
  
  if (reject_fraction_ == UnitFloat::max()) {
    return true;  // Reject all
  }
  
  // Generate random float between 0 and 1
  std::uniform_real_distribution<float> dist(0.0f, 1.0f);
  return dist(random_) < reject_fraction_.value();
}

// =================================================================
// TcpActiveListener - Manages filter chains and connection creation
// =================================================================

// Filter chain context for async filter processing
struct TcpActiveListener::FilterChainContext : public ListenerFilterCallbacks {
  TcpActiveListener& parent;
  ConnectionSocketPtr socket_ptr;
  size_t current_filter_index{0};
  
  FilterChainContext(TcpActiveListener& p, ConnectionSocketPtr s)
      : parent(p), socket_ptr(std::move(s)) {}
  
  // ListenerFilterCallbacks interface
  ConnectionSocket& socket() override { return *socket_ptr; }
  event::Dispatcher& dispatcher() override { return parent.dispatcher_; }
  
  void continueFilterChain(bool success) override {
    if (!success) {
      // Filter rejected the connection
      // Clean up this context
      parent.removeFilterContext(this);
      return;
    }
    
    // Continue processing filters
    current_filter_index++;
    parent.processNextFilter(this);
  }
};

TcpActiveListener::TcpActiveListener(event::Dispatcher& dispatcher,
                               TcpListenerConfig config,
                               ListenerCallbacks& parent_cb)
    : dispatcher_(dispatcher),
      config_(std::move(config)),
      parent_cb_(parent_cb),
      random_(std::random_device{}()),
      listener_tag_(next_listener_tag_++) {
  
  // Create socket if not provided
  if (!config_.socket && config_.address) {
    // Create and bind socket
    auto socket_result = createListenSocket(
        config_.address,
        SocketCreationOptions{
            .non_blocking = true,
            .close_on_exec = true,
            .reuse_address = true
        },
        config_.bind_to_port);
    
    if (socket_result) {
      config_.socket = std::move(socket_result);
      
      // Listen on the socket
      if (config_.bind_to_port) {
        static_cast<ListenSocketImpl*>(config_.socket.get())->listen(config_.backlog);
      }
    }
  }
  
  // Create the actual TCP listener
  if (config_.socket) {
    listener_ = std::make_unique<TcpListenerImpl>(
        dispatcher_,
        random_,
        config_.socket,
        *this,  // We are the callbacks
        config_.bind_to_port,
        config_.ignore_global_conn_limit,
        config_.bypass_overload_manager,
        config_.max_connections_per_event,
        nullopt  // Overload state would come from ListenerManager
    );
    
    // Set initial reject fraction
    listener_->setRejectFraction(UnitFloat(config_.initial_reject_fraction));
  }
}

TcpActiveListener::~TcpActiveListener() {
  disable();
  // Clean up any pending filter contexts
  pending_filter_contexts_.clear();
}

void TcpActiveListener::enable() {
  if (listener_) {
    listener_->enable();
  }
}

void TcpActiveListener::disable() {
  if (listener_) {
    listener_->disable();
  }
}

void TcpActiveListener::setRejectFraction(UnitFloat fraction) {
  if (listener_) {
    listener_->setRejectFraction(fraction);
  }
}

void TcpActiveListener::configureLoadShedPoints(LoadShedPoint& load_shed_point) {
  if (listener_) {
    listener_->configureLoadShedPoints(load_shed_point);
  }
}

void TcpActiveListener::onAccept(ConnectionSocketPtr&& socket) {
  // If we have filters, run them
  if (!config_.listener_filters.empty()) {
    runFilterChain(std::move(socket));
  } else {
    // No filters, create connection directly
    createConnection(std::move(socket));
  }
}

void TcpActiveListener::onNewConnection(ConnectionPtr&& connection) {
  // Forward to parent callbacks
  parent_cb_.onNewConnection(std::move(connection));
}

void TcpActiveListener::runFilterChain(ConnectionSocketPtr&& socket) {
  // Create filter context
  auto context = std::make_unique<FilterChainContext>(*this, std::move(socket));
  auto context_ptr = context.get();
  
  // Store the context
  pending_filter_contexts_.push_back(std::move(context));
  
  // Start processing filters
  processNextFilter(context_ptr);
}

void TcpActiveListener::processNextFilter(FilterChainContext* context) {
  // Check if we've processed all filters
  if (context->current_filter_index >= config_.listener_filters.size()) {
    // All filters passed, create connection
    auto socket = std::move(context->socket_ptr);
    removeFilterContext(context);
    createConnection(std::move(socket));
    return;
  }
  
  // Process current filter
  auto& filter = config_.listener_filters[context->current_filter_index];
  auto status = filter->onAccept(*context);
  
  if (status == ListenerFilterStatus::Continue) {
    // Filter passed synchronously, continue to next
    context->current_filter_index++;
    processNextFilter(context);
  }
  // If StopIteration, wait for continueFilterChain() to be called
}

void TcpActiveListener::removeFilterContext(FilterChainContext* context) {
  // Remove this context from pending list
  pending_filter_contexts_.erase(
      std::remove_if(pending_filter_contexts_.begin(),
                     pending_filter_contexts_.end(),
                     [context](const std::unique_ptr<FilterChainContext>& ctx) {
                       return ctx.get() == context;
                     }),
      pending_filter_contexts_.end());
}

void TcpActiveListener::createConnection(ConnectionSocketPtr&& socket) {
  // In production, this would:
  // 1. Select the appropriate filter chain based on SNI/ALPN
  // 2. Create transport socket (TLS, plaintext, etc.)
  // 3. Create connection with proper filter chain
  // 4. Initialize the connection
  // 5. Hand off to connection manager
  
  // For now, create a basic connection
  if (config_.transport_socket_factory) {
    // Create transport socket
    auto transport_socket = config_.transport_socket_factory->createTransportSocket();
    
    // Create stream info
    auto stream_info = stream_info::StreamInfoImpl::create();
    
    // Create server connection
    auto connection = ConnectionImpl::createServerConnection(
        dispatcher_,
        std::move(socket),
        std::move(transport_socket),
        *stream_info);
    
    // Set buffer limits
    connection->setBufferLimits(config_.per_connection_buffer_limit);
    
    // Apply filter chain if configured
    if (config_.filter_chain_factory) {
      auto* conn_impl = dynamic_cast<ConnectionImplBase*>(connection.get());
      if (conn_impl) {
        config_.filter_chain_factory->createFilterChain(conn_impl->filterManager());
        conn_impl->filterManager().initializeReadFilters();
      }
    }
    
    // Hand off to parent (usually ListenerManager)
    parent_cb_.onNewConnection(std::move(connection));
  } else {
    // No transport socket factory configured
    // Just pass the socket to parent
    parent_cb_.onAccept(std::move(socket));
  }
}

// =================================================================
// ListenerFactory
// =================================================================

std::unique_ptr<TcpListenerImpl> ListenerFactory::createTcpListener(
    event::Dispatcher& dispatcher,
    const TcpListenerConfig& config,
    TcpListenerCallbacks& cb,
    ThreadLocalOverloadStateOptRef overload_state) {
  
  // Create socket if needed
  SocketSharedPtr socket = config.socket;
  if (!socket && config.address) {
    // Create and bind socket
    auto socket_result = createListenSocket(
        config.address,
        SocketCreationOptions{
            .non_blocking = true,
            .close_on_exec = true,
            .reuse_address = true
        },
        config.bind_to_port);
    
    if (socket_result) {
      socket = std::move(socket_result);
      
      if (config.bind_to_port) {
        // Listen on the socket
        static_cast<ListenSocketImpl*>(socket.get())->listen(config.backlog);
      }
    }
  }
  
  if (!socket) {
    return nullptr;
  }
  
  // Apply socket options
  if (config.socket_options) {
    for (const auto& option : *config.socket_options) {
      option->setOption(*socket);
    }
  }
  
  // Enable SO_REUSEPORT if requested
  if (config.enable_reuse_port) {
#ifdef SO_REUSEPORT
    int val = 1;
    socket->setSocketOption(SOL_SOCKET, SO_REUSEPORT, &val, sizeof(val));
#endif
  }
  
  // Create random generator for this listener
  std::mt19937 random(std::random_device{}());
  
  return std::make_unique<TcpListenerImpl>(
      dispatcher,
      random,
      socket,
      cb,
      config.bind_to_port,
      config.ignore_global_conn_limit,
      config.bypass_overload_manager,
      config.max_connections_per_event,
      overload_state);
}

}  // namespace network
}  // namespace mcp