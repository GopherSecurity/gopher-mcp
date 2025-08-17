/**
 * @file server_listener_manager.h
 * @brief Server listener manager for managing server-side listeners and tracking connections
 *
 * Following production patterns:
 * - Manages multiple listeners
 * - Tracks global connection count
 * - Integrates with overload manager
 * - Per-worker thread model
 */

#ifndef MCP_NETWORK_SERVER_LISTENER_MANAGER_H
#define MCP_NETWORK_SERVER_LISTENER_MANAGER_H

#include <atomic>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "mcp/event/event_loop.h"
#include "mcp/network/server_listener_impl.h"

namespace mcp {
namespace network {

// Forward declarations
class OverloadManager;

/**
 * Listener manager interface
 *
 * Manages server-side listeners and tracks accepted connections globally
 * Note: This is for server-side use only, not for client connections
 */
class ServerListenerManager {
 public:
  virtual ~ServerListenerManager() = default;

  /**
   * Get total number of connections across all listeners
   */
  virtual uint64_t numConnections() const = 0;

  /**
   * Increment connection count (called by listeners)
   */
  virtual void incNumConnections() = 0;

  /**
   * Decrement connection count (called on connection close)
   */
  virtual void decNumConnections() = 0;

  /**
   * Add a listener
   */
  virtual void addListener(ListenerConfig config,
                           ListenerCallbacks& callbacks) = 0;

  /**
   * Remove a listener by tag
   */
  virtual void removeListener(uint64_t listener_tag) = 0;

  /**
   * Stop all listeners
   */
  virtual void stopListeners() = 0;

  /**
   * Disable all listeners (stop accepting but keep sockets)
   */
  virtual void disableListeners() = 0;

  /**
   * Enable all listeners
   */
  virtual void enableListeners() = 0;

  /**
   * Set reject fraction for all listeners
   */
  virtual void setListenerRejectFraction(UnitFloat reject_fraction) = 0;

  /**
   * Get stat prefix for this handler
   */
  virtual const std::string& statPrefix() const = 0;
};

/**
 * Listener manager implementation
 *
 * Production-style connection handler that:
 * - Manages multiple listeners per worker
 * - Tracks global connection count
 * - Integrates with overload manager
 * - Supports listener enable/disable
 * - Handles reject fraction for load shedding
 */
class ServerListenerManagerImpl : public ServerListenerManager,
                              public ListenerCallbacks {
 public:
  /**
   * Constructor
   *
   * @param dispatcher Event dispatcher for this worker
   * @param worker_index Optional worker index (nullopt for main thread)
   */
  ServerListenerManagerImpl(event::Dispatcher& dispatcher,
                            optional<uint32_t> worker_index = nullopt);

  /**
   * Constructor with overload manager
   *
   * @param dispatcher Event dispatcher
   * @param worker_index Worker index
   * @param overload_manager Overload manager for load shedding
   */
  ServerListenerManagerImpl(event::Dispatcher& dispatcher,
                            optional<uint32_t> worker_index,
                            OverloadManager& overload_manager);

  ~ServerListenerManagerImpl() override;

  // ServerListenerManager interface
  uint64_t numConnections() const override { return num_connections_.load(); }
  void incNumConnections() override;
  void decNumConnections() override;
  void addListener(ListenerConfig config,
                   ListenerCallbacks& callbacks) override;
  void removeListener(uint64_t listener_tag) override;
  void stopListeners() override;
  void disableListeners() override;
  void enableListeners() override;
  void setListenerRejectFraction(UnitFloat reject_fraction) override;
  const std::string& statPrefix() const override { return stat_prefix_; }

  // ListenerCallbacks interface (for TcpActiveListener)
  void onAccept(ConnectionSocketPtr&& socket) override;
  void onNewConnection(ConnectionPtr&& connection) override;

  /**
   * Get dispatcher
   */
  event::Dispatcher& dispatcher() { return dispatcher_; }

  /**
   * Get listener by tag
   */
  TcpActiveListener* getListener(uint64_t listener_tag);

  /**
   * Get listener by address
   */
  TcpActiveListener* getListenerByAddress(const Address::Instance& address);

 private:
  /**
   * Active listener details
   */
  struct ListenerDetails {
    std::unique_ptr<TcpActiveListener> listener;
    ListenerCallbacks* parent_callbacks;
    uint64_t listener_tag;
    Address::InstanceConstSharedPtr address;
  };

  // Create thread-local overload state for listeners
  void createOverloadState();

  event::Dispatcher& dispatcher_;
  optional<uint32_t> worker_index_;
  OverloadManager* overload_manager_{nullptr};

  // Stat prefix for metrics
  std::string stat_prefix_;

  // Global connection count
  std::atomic<uint64_t> num_connections_{0};

  // Listeners by tag
  std::unordered_map<uint64_t, std::unique_ptr<ListenerDetails>> listeners_;

  // Listeners by address (for quick lookup)
  std::unordered_map<std::string, ListenerDetails*> listeners_by_address_;

  // Current state
  bool listeners_disabled_{false};
  UnitFloat listener_reject_fraction_{UnitFloat::min()};

  // Thread-local overload state
  std::unique_ptr<ThreadLocalOverloadState> overload_state_;
};

/**
 * Listener manager factory
 */
class ServerListenerManagerFactory {
 public:
  virtual ~ServerListenerManagerFactory() = default;

  /**
   * Create a listener manager
   */
  virtual std::unique_ptr<ServerListenerManager> createServerListenerManager(
      event::Dispatcher& dispatcher, optional<uint32_t> worker_index) = 0;

  /**
   * Get factory name
   */
  virtual std::string name() const = 0;
};

/**
 * Default listener manager factory
 */
class DefaultServerListenerManagerFactory : public ServerListenerManagerFactory {
 public:
  std::unique_ptr<ServerListenerManager> createServerListenerManager(
      event::Dispatcher& dispatcher, optional<uint32_t> worker_index) override {
    return std::make_unique<ServerListenerManagerImpl>(dispatcher, worker_index);
  }

  std::string name() const override { return "mcp.listener_manager.default"; }
};

}  // namespace network
}  // namespace mcp

#endif  // MCP_NETWORK_SERVER_LISTENER_MANAGER_H