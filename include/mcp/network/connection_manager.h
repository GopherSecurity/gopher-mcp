#ifndef MCP_NETWORK_CONNECTION_MANAGER_H
#define MCP_NETWORK_CONNECTION_MANAGER_H

#include <functional>
#include <memory>
#include <vector>

#include "mcp/event/event_loop.h"
#include "mcp/network/connection.h"
#include "mcp/network/listener.h"
#include "mcp/network/socket_interface.h"

namespace mcp {
namespace network {

/**
 * Connection handler interface
 *
 * Manages active connections and listeners
 */
class ConnectionHandler {
 public:
  virtual ~ConnectionHandler() = default;

  /**
   * Get the number of active connections
   */
  virtual uint64_t numConnections() const = 0;

  /**
   * Add a listener
   */
  virtual void addListener(ListenerPtr&& listener) = 0;

  /**
   * Remove a listener
   */
  virtual void removeListener(const std::string& name) = 0;

  /**
   * Stop all listeners
   */
  virtual void stopListeners() = 0;

  /**
   * Disable listeners (stop accepting new connections)
   */
  virtual void disableListeners() = 0;

  /**
   * Enable listeners
   */
  virtual void enableListeners() = 0;

  /**
   * Set listener callbacks
   */
  virtual void setListenerCallbacks(ListenerCallbacks& callbacks) = 0;
};

/**
 * Connection handler implementation per worker thread
 */
class ConnectionHandlerImpl : public ConnectionHandler,
                              public ListenerCallbacks {
 public:
  ConnectionHandlerImpl(event::Dispatcher& dispatcher,
                        SocketInterface& socket_interface);
  ~ConnectionHandlerImpl() override;

  // ConnectionHandler interface
  uint64_t numConnections() const override { return connections_.size(); }
  void addListener(ListenerPtr&& listener) override;
  void removeListener(const std::string& name) override;
  void stopListeners() override;
  void disableListeners() override;
  void enableListeners() override;
  void setListenerCallbacks(ListenerCallbacks& callbacks) override {
    listener_callbacks_ = &callbacks;
  }

  // ListenerCallbacks interface
  void onAccept(ConnectionSocketPtr&& socket) override;
  void onNewConnection(ConnectionPtr&& connection) override;

  // Remove a connection
  void removeConnection(Connection& connection);

 private:
  event::Dispatcher& dispatcher_;
  SocketInterface& socket_interface_;

  // Active listeners
  std::vector<ListenerPtr> listeners_;

  // Active connections
  std::list<ConnectionPtr> connections_;

  // Listener callbacks
  ListenerCallbacks* listener_callbacks_{nullptr};

  // Whether listeners are disabled
  bool listeners_disabled_{false};
};

/**
 * Connection manager configuration
 */
struct ConnectionManagerConfig {
  // Maximum number of connections
  optional<uint32_t> max_connections;

  // Connection timeout
  optional<std::chrono::milliseconds> connection_timeout;

  // Buffer limits
  uint32_t per_connection_buffer_limit{1024 * 1024};  // 1MB default

  // Transport socket factory
  ClientTransportSocketFactorySharedPtr client_transport_socket_factory;
  ServerTransportSocketFactorySharedPtr server_transport_socket_factory;

  // Filter chain factory
  std::shared_ptr<FilterChainFactory> filter_chain_factory;
};

/**
 * Connection manager
 *
 * High-level interface for managing connections
 */
class ConnectionManager {
 public:
  virtual ~ConnectionManager() = default;

  /**
   * Create a client connection
   */
  virtual ClientConnectionPtr createClientConnection(
      const network::Address::InstanceConstSharedPtr& address,
      const TransportSocketOptionsSharedPtr& transport_options = nullptr) = 0;

  /**
   * Create a server connection from an accepted socket
   */
  virtual ServerConnectionPtr createServerConnection(
      ConnectionSocketPtr&& socket) = 0;

  /**
   * Register connection callbacks
   */
  virtual void setConnectionCallbacks(ConnectionPoolCallbacks& callbacks) = 0;

  /**
   * Get active connection count
   */
  virtual size_t numConnections() const = 0;

  /**
   * Close all connections
   */
  virtual void closeAllConnections() = 0;
};

/**
 * Connection manager implementation
 */
class ConnectionManagerImpl : public ConnectionManager,
                              public ConnectionCallbacks {
 public:
  ConnectionManagerImpl(event::Dispatcher& dispatcher,
                        SocketInterface& socket_interface,
                        const ConnectionManagerConfig& config);
  ~ConnectionManagerImpl() override;

  // ConnectionManager interface
  ClientConnectionPtr createClientConnection(
      const network::Address::InstanceConstSharedPtr& address,
      const TransportSocketOptionsSharedPtr& transport_options =
          nullptr) override;
  ServerConnectionPtr createServerConnection(
      ConnectionSocketPtr&& socket) override;
  void setConnectionCallbacks(ConnectionPoolCallbacks& callbacks) override {
    pool_callbacks_ = &callbacks;
  }
  size_t numConnections() const override { return connections_.size(); }
  void closeAllConnections() override;

  // ConnectionCallbacks interface
  void onEvent(ConnectionEvent event) override;
  void onAboveWriteBufferHighWatermark() override {}
  void onBelowWriteBufferLowWatermark() override {}

 private:
  // Helper to track connection
  void trackConnection(Connection* connection);
  void untrackConnection(Connection* connection);

  // Create transport socket
  TransportSocketPtr createClientTransportSocket(
      const TransportSocketOptionsSharedPtr& options);
  TransportSocketPtr createServerTransportSocket();

  // Apply filter chain
  void applyFilterChain(Connection& connection);

  event::Dispatcher& dispatcher_;
  SocketInterface& socket_interface_;
  ConnectionManagerConfig config_;

  // Active connections
  std::unordered_map<Connection*, std::weak_ptr<Connection>> connections_;

  // Callbacks
  ConnectionPoolCallbacks* pool_callbacks_{nullptr};
};

/**
 * Connection pool interface
 *
 * Manages a pool of connections to a specific upstream
 */
class ConnectionPool {
 public:
  virtual ~ConnectionPool() = default;

  /**
   * Connection pool failure reasons
   */
  enum class PoolFailureReason {
    Overflow,      // Connection limit reached
    Timeout,       // Connection timeout
    LocalFailure,  // Local connection failure
    RemoteFailure  // Remote connection failure
  };

  /**
   * Pool callbacks for stream lifetime events
   */
  class Callbacks {
   public:
    virtual ~Callbacks() = default;

    /**
     * Called when a connection is available
     */
    virtual void onPoolReady(ClientConnection& connection,
                             const std::string& protocol) = 0;

    /**
     * Called when pool fails to create connection
     */
    virtual void onPoolFailure(ConnectionPool::PoolFailureReason reason,
                               const std::string& failure_reason) = 0;
  };

  // Enum moved above

  /**
   * Create a new connection or return existing one
   */
  virtual void newConnection(Callbacks& callbacks) = 0;

  /**
   * Get active connection count
   */
  virtual size_t numActiveConnections() const = 0;

  /**
   * Get pending connection count
   */
  virtual size_t numPendingConnections() const = 0;

  /**
   * Get idle connection count
   */
  virtual size_t numIdleConnections() const = 0;

  /**
   * Close all connections
   */
  virtual void closeConnections() = 0;

  /**
   * Add idle callback
   */
  using IdleCb = std::function<void()>;
  virtual void addIdleCallback(IdleCb cb) = 0;

  /**
   * Check if the pool is idle
   */
  virtual bool isIdle() const = 0;

  /**
   * Drain connections
   */
  virtual void drainConnections() = 0;
};

using ConnectionPoolPtr = std::unique_ptr<ConnectionPool>;

/**
 * Factory for creating connection pools
 */
class ConnectionPoolFactory {
 public:
  virtual ~ConnectionPoolFactory() = default;

  /**
   * Create a connection pool
   */
  virtual ConnectionPoolPtr createConnectionPool(
      event::Dispatcher& dispatcher,
      const network::Address::InstanceConstSharedPtr& address,
      const ConnectionManagerConfig& config) = 0;
};

/**
 * Connection pool implementation
 */
class ConnectionPoolImpl : public ConnectionPool, public ConnectionCallbacks {
 public:
  ConnectionPoolImpl(event::Dispatcher& dispatcher,
                     const network::Address::InstanceConstSharedPtr& address,
                     const ConnectionManagerConfig& config,
                     ConnectionManager& connection_manager);
  ~ConnectionPoolImpl() override;

  // ConnectionPool interface
  void newConnection(Callbacks& callbacks) override;
  size_t numActiveConnections() const override {
    return active_connections_.size();
  }
  size_t numPendingConnections() const override {
    return pending_connections_.size();
  }
  size_t numIdleConnections() const override {
    return idle_connections_.size();
  }
  void closeConnections() override;
  void addIdleCallback(IdleCb cb) override { idle_callbacks_.push_back(cb); }
  bool isIdle() const override;
  void drainConnections() override;

  // ConnectionCallbacks interface
  void onEvent(ConnectionEvent event) override;
  void onAboveWriteBufferHighWatermark() override {}
  void onBelowWriteBufferLowWatermark() override {}

 private:
  // Connection states
  struct PendingConnection {
    ClientConnectionPtr connection;
    Callbacks* callbacks;
    event::TimerPtr timeout_timer;
  };

  struct ActiveConnection {
    ClientConnectionPtr connection;
    bool busy{false};
  };

  // Helper methods
  void createNewConnection(Callbacks& callbacks);
  void assignConnection(ClientConnection& connection, Callbacks& callbacks);
  void onConnectionTimeout(PendingConnection& pending);
  void checkIdleCallbacks();
  void movePendingToIdle(PendingConnection& pending);

  event::Dispatcher& dispatcher_;
  network::Address::InstanceConstSharedPtr address_;
  ConnectionManagerConfig config_;
  ConnectionManager& connection_manager_;

  // Connection pools
  std::list<PendingConnection> pending_connections_;
  std::list<ActiveConnection> active_connections_;
  std::list<ClientConnectionPtr> idle_connections_;

  // Callbacks
  std::vector<IdleCb> idle_callbacks_;

  // State
  bool draining_{false};
};

}  // namespace network
}  // namespace mcp

#endif  // MCP_NETWORK_CONNECTION_MANAGER_H