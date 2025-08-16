/**
 * @file listener_impl.h
 * @brief Simplified production-grade listener following industrial best
 * practices
 *
 * Design principles from production servers:
 * - Minimal state management (enabled/disabled)
 * - Overload manager integration
 * - Per-event connection batching
 * - Global connection limit enforcement
 * - Thread-local overload state
 * - Probabilistic rejection (reject fraction)
 *
 * Thread model:
 * - All operations in dispatcher thread
 * - No explicit state machine needed
 * - Simple event-driven callbacks
 */

#ifndef MCP_NETWORK_LISTENER_IMPL_H
#define MCP_NETWORK_LISTENER_IMPL_H

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <random>
#include <string>

#include "mcp/core/result.h"
#include "mcp/event/event_loop.h"
#include "mcp/network/address.h"
#include "mcp/network/connection.h"
#include "mcp/network/filter.h"
#include "mcp/network/listener.h"  // For ListenerFilter types
#include "mcp/network/socket.h"

namespace mcp {
namespace network {

// Forward declarations
class OverloadManager;
class LoadShedPoint;

/**
 * Unit float type for reject fraction (0.0 to 1.0)
 */
class UnitFloat {
 public:
  UnitFloat(float value = 0.0f)
      : value_(std::max(0.0f, std::min(1.0f, value))) {}

  float value() const { return value_; }
  operator float() const { return value_; }

  static UnitFloat min() { return UnitFloat(0.0f); }
  static UnitFloat max() { return UnitFloat(1.0f); }

 private:
  float value_;
};

/**
 * Thread-local overload state
 */
struct ThreadLocalOverloadState {
  // Current global connection count
  std::atomic<uint64_t>* global_cx_count{nullptr};

  // Maximum global connections allowed
  uint64_t global_cx_limit{UINT64_MAX};

  // Is overload manager tracking connections
  bool track_global_cx_in_overload_manager{false};
};

using ThreadLocalOverloadStateOptRef =
    optional<std::reference_wrapper<ThreadLocalOverloadState>>;

/**
 * Extended listener callbacks with enable/disable notifications
 */
class TcpListenerCallbacks : public ListenerCallbacks {
 public:
  virtual ~TcpListenerCallbacks() = default;

  using ListenerCallbacks::onAccept;
  using ListenerCallbacks::onNewConnection;

  /**
   * Called when the listener enters/exits enabled state
   */
  virtual void onListenerEnabled() {}
  virtual void onListenerDisabled() {}
};

/**
 * Base listener implementation
 *
 * Provides common functionality for TCP/UDP listeners
 */
class BaseListenerImpl {
 public:
  BaseListenerImpl(event::Dispatcher& dispatcher, SocketSharedPtr socket);
  virtual ~BaseListenerImpl() = default;

  // Core listener interface
  virtual void disable() = 0;
  virtual void enable() = 0;
  virtual void setRejectFraction(UnitFloat reject_fraction) = 0;
  virtual void configureLoadShedPoints(LoadShedPoint& load_shed_point) {}
  virtual bool shouldBypassOverloadManager() const { return false; }

  // Accessors
  const Address::InstanceConstSharedPtr& localAddress() const {
    return local_address_;
  }
  Socket& socket() { return *socket_; }
  const Socket& socket() const { return *socket_; }

 protected:
  event::Dispatcher& dispatcher_;
  SocketSharedPtr socket_;
  Address::InstanceConstSharedPtr local_address_;
};

/**
 * TCP listener implementation
 *
 * Simple, efficient TCP listener following production patterns:
 * - No complex state machine
 * - Direct socket event handling
 * - Batched accepts per event
 * - Overload protection
 */
class TcpListenerImpl : public BaseListenerImpl {
 public:
  /**
   * Constructor
   *
   * @param dispatcher Event dispatcher
   * @param random Random generator for probabilistic rejection
   * @param socket Listen socket
   * @param cb Listener callbacks
   * @param bind_to_port Whether to bind to port (vs pre-bound socket)
   * @param ignore_global_conn_limit Bypass global connection limits
   * @param bypass_overload_manager Bypass overload manager checks
   * @param max_connections_per_event Max connections to accept per socket event
   * @param overload_state Thread-local overload state
   */
  TcpListenerImpl(event::Dispatcher& dispatcher,
                  std::mt19937& random,
                  SocketSharedPtr socket,
                  TcpListenerCallbacks& cb,
                  bool bind_to_port,
                  bool ignore_global_conn_limit,
                  bool bypass_overload_manager,
                  uint32_t max_connections_per_event,
                  ThreadLocalOverloadStateOptRef overload_state);

  ~TcpListenerImpl() override;

  // BaseListenerImpl interface
  void disable() override;
  void enable() override;
  void setRejectFraction(UnitFloat reject_fraction) override;
  void configureLoadShedPoints(LoadShedPoint& load_shed_point) override;
  bool shouldBypassOverloadManager() const override {
    return bypass_overload_manager_;
  }

  // Statistics
  uint64_t numConnections() const { return num_connections_; }

 private:
  /**
   * Handle socket event (accept connections)
   *
   * @param events Socket events (read ready)
   */
  void onSocketEvent(uint32_t events);

  /**
   * Accept a single connection
   *
   * @return True if connection was accepted, false on error/limit
   */
  bool doAccept();

  /**
   * Check if connection should be rejected due to global limit
   *
   * @return True if connection should be rejected
   */
  bool rejectCxOverGlobalLimit() const;

  /**
   * Check if connection should be rejected probabilistically
   *
   * @return True if connection should be rejected
   */
  bool shouldRejectProbabilistically();

  // Core components
  TcpListenerCallbacks& cb_;
  std::mt19937& random_;

  // Configuration
  const bool bind_to_port_;
  const bool ignore_global_conn_limit_;
  const bool bypass_overload_manager_;
  const uint32_t max_connections_per_event_;

  // State (minimal!)
  bool enabled_{false};
  UnitFloat reject_fraction_{UnitFloat::min()};

  // File event for accept
  event::FileEventPtr file_event_;

  // Overload management
  ThreadLocalOverloadStateOptRef overload_state_;
  LoadShedPoint* listener_accept_{nullptr};

  // Metrics
  std::atomic<uint64_t> num_connections_{0};
  std::atomic<uint64_t> num_rejected_connections_{0};
};

/**
 * Extended listener configuration for TCP
 */
struct TcpListenerConfig : public ListenerConfig {
  // Connection limits
  uint32_t max_connections_per_event{1};
  bool ignore_global_conn_limit{false};

  // Overload configuration
  bool bypass_overload_manager{false};
  float initial_reject_fraction{0.0f};

  // Socket for pre-bound listeners
  SocketSharedPtr socket;
};

/**
 * Factory for creating listeners
 */
class ListenerFactory {
 public:
  /**
   * Create a TCP listener
   *
   * @param dispatcher Event dispatcher
   * @param config Listener configuration
   * @param cb Listener callbacks
   * @param overload_state Thread-local overload state
   * @return Created listener
   */
  static std::unique_ptr<TcpListenerImpl> createTcpListener(
      event::Dispatcher& dispatcher,
      const TcpListenerConfig& config,
      TcpListenerCallbacks& cb,
      ThreadLocalOverloadStateOptRef overload_state = nullopt);
};

/**
 * TCP Active listener wrapper
 *
 * Manages listener lifecycle and filter chains
 * This is what ConnectionHandler actually manages
 */
class TcpActiveListener : public TcpListenerCallbacks {
 public:
  TcpActiveListener(event::Dispatcher& dispatcher,
                    TcpListenerConfig config,
                    ListenerCallbacks& parent_cb);

  ~TcpActiveListener();

  // Control interface
  void enable();
  void disable();
  void pause() { disable(); }
  void resume() { enable(); }

  // Configuration
  void setRejectFraction(UnitFloat fraction);
  void configureLoadShedPoints(LoadShedPoint& load_shed_point);

  // ListenerCallbacks interface
  void onAccept(ConnectionSocketPtr&& socket) override;
  void onNewConnection(ConnectionPtr&& connection) override;

  // TcpListenerCallbacks interface
  void onListenerEnabled() override {}
  void onListenerDisabled() override {}

  // Accessors
  const std::string& name() const { return config_.name; }
  uint64_t listenerTag() const { return listener_tag_; }
  TcpListenerImpl* listener() { return listener_.get(); }

 private:
  /**
   * Run filter chain on accepted socket
   */
  void runFilterChain(ConnectionSocketPtr&& socket);

  /**
   * Create connection from filtered socket
   */
  void createConnection(ConnectionSocketPtr&& socket);

  event::Dispatcher& dispatcher_;
  TcpListenerConfig config_;
  ListenerCallbacks& parent_cb_;

  // The actual listener
  std::unique_ptr<TcpListenerImpl> listener_;

  // Random generator for this listener
  std::mt19937 random_;

  // Listener tag for identification
  uint64_t listener_tag_;
  static std::atomic<uint64_t> next_listener_tag_;

  // Filter chain context (if filters enabled)
  struct FilterChainContext;
  std::vector<std::unique_ptr<FilterChainContext>> pending_filter_contexts_;

  // Filter processing helpers
  void processNextFilter(FilterChainContext* context);
  void removeFilterContext(FilterChainContext* context);
};

}  // namespace network
}  // namespace mcp

#endif  // MCP_NETWORK_LISTENER_IMPL_H