#ifndef MCP_NETWORK_LISTENER_H
#define MCP_NETWORK_LISTENER_H

#include <memory>
#include <string>

#include "mcp/event/event_loop.h"
#include "mcp/network/filter.h"
#include "mcp/network/socket.h"
#include "mcp/network/socket_impl.h"
#include "mcp/network/socket_interface.h"
#include "mcp/network/transport_socket.h"

namespace mcp {
namespace network {

// Forward declarations
class Connection;
class Listener;
class ListenerCallbacks;
class ListenerFilter;
class ListenerFilterCallbacks;
class ConnectionSocket;
class SocketInterface;

using ConnectionPtr = std::unique_ptr<Connection>;
using ListenerPtr = std::unique_ptr<Listener>;
using ListenerFilterPtr = std::unique_ptr<ListenerFilter>;
using ConnectionSocketPtr = std::unique_ptr<ConnectionSocket>;

/**
 * Listener filter status
 */
enum class ListenerFilterStatus {
  Continue,      // Continue to next filter
  StopIteration  // Stop processing, wait for resumption
};

/**
 * Listener filter callbacks
 */
class ListenerFilterCallbacks {
 public:
  virtual ~ListenerFilterCallbacks() = default;

  /**
   * Get the connection socket
   */
  virtual ConnectionSocket& socket() = 0;

  /**
   * Get the dispatcher
   */
  virtual event::Dispatcher& dispatcher() = 0;

  /**
   * Continue filter chain iteration
   */
  virtual void continueFilterChain(bool success) = 0;
};

/**
 * Listener filter interface
 *
 * Filters that run before the connection is fully accepted
 * (e.g., for SNI inspection, PROXY protocol, etc.)
 */
class ListenerFilter {
 public:
  virtual ~ListenerFilter() = default;

  /**
   * Called when a new connection is accepted
   *
   * @param cb Callbacks for interacting with the listener
   * @return Status indicating whether to continue
   */
  virtual ListenerFilterStatus onAccept(ListenerFilterCallbacks& cb) = 0;

  /**
   * Called when the filter should be destroyed
   */
  virtual void onDestroy() {}
};

// Note: ConnectionSocketImpl is defined in socket_impl.h

/**
 * Listener callbacks
 */
class ListenerCallbacks {
 public:
  virtual ~ListenerCallbacks() = default;

  /**
   * Called when a socket is accepted
   *
   * The socket may be passed through listener filters before
   * becoming a full connection
   */
  virtual void onAccept(ConnectionSocketPtr&& socket) = 0;

  /**
   * Called when a new connection is ready
   *
   * This is called after all listener filters have run
   */
  virtual void onNewConnection(ConnectionPtr&& connection) = 0;
};

/**
 * Listener configuration
 */
struct ListenerConfig {
  // Listener name
  std::string name;

  // Bind address
  network::Address::InstanceConstSharedPtr address;

  // Socket options
  SocketOptionsSharedPtr socket_options;

  // Whether to bind to port (vs using existing FD)
  bool bind_to_port{true};

  // Whether to enable SO_REUSEPORT
  bool enable_reuse_port{false};

  // Backlog size
  int backlog{128};

  // Per-connection buffer limit
  uint32_t per_connection_buffer_limit{1024 * 1024};

  // Listener filters
  std::vector<ListenerFilterPtr> listener_filters;

  // Transport socket factory
  ServerTransportSocketFactorySharedPtr transport_socket_factory;

  // Filter chain factory
  std::shared_ptr<FilterChainFactory> filter_chain_factory;
};

/**
 * Listener interface
 *
 * Accepts new connections and creates connection objects
 */
class Listener {
 public:
  virtual ~Listener() = default;

  /**
   * Get the listener name
   */
  virtual const std::string& name() const = 0;

  /**
   * Start listening
   */
  virtual VoidResult listen() = 0;

  /**
   * Disable the listener (stop accepting new connections)
   */
  virtual void disable() = 0;

  /**
   * Enable the listener
   */
  virtual void enable() = 0;

  /**
   * Check if the listener is enabled
   */
  virtual bool isEnabled() const = 0;

  /**
   * Get the socket
   */
  virtual Socket& socket() = 0;
  virtual const Socket& socket() const = 0;

  /**
   * Get listener tags for stats/logging
   */
  virtual const std::vector<std::string>& tags() const = 0;

  /**
   * Get the number of accepted connections
   */
  virtual uint64_t numConnections() const = 0;
};

/**
 * Active listener implementation
 *
 * Handles accept() and connection creation
 */
class ActiveListener : public Listener, public ListenerCallbacks {
 public:
  ActiveListener(event::Dispatcher& dispatcher,
                 SocketInterface& socket_interface,
                 ListenerCallbacks& parent_callbacks,
                 ListenerConfig&& config);
  ~ActiveListener() override;

  // Listener interface
  const std::string& name() const override { return config_.name; }
  VoidResult listen() override;
  void disable() override;
  void enable() override;
  bool isEnabled() const override { return enabled_; }
  Socket& socket() override { return *socket_; }
  const Socket& socket() const override { return *socket_; }
  const std::vector<std::string>& tags() const override { return tags_; }
  uint64_t numConnections() const override { return num_connections_; }

  // ListenerCallbacks interface
  void onAccept(ConnectionSocketPtr&& socket) override;
  void onNewConnection(ConnectionPtr&& connection) override;

 private:
  // Accept handler
  void onSocketEvent(uint32_t events);
  void doAccept();

  // Create connection from accepted socket
  void createConnection(ConnectionSocketPtr&& socket);

  // Run listener filters
  void runListenerFilters(ConnectionSocketPtr&& socket);

  event::Dispatcher& dispatcher_;
  SocketInterface& socket_interface_;
  ListenerCallbacks& parent_callbacks_;
  ListenerConfig config_;

  // Listen socket
  SocketPtr socket_;

  // File event for accept
  event::FileEventPtr file_event_;

  // State
  bool enabled_{true};
  uint64_t num_connections_{0};

  // Tags for stats/logging
  std::vector<std::string> tags_;
};

/**
 * Listener manager interface
 *
 * Manages all listeners in the system
 */
class ListenerManager {
 public:
  virtual ~ListenerManager() = default;

  /**
   * Add a listener
   */
  virtual VoidResult addListener(ListenerConfig&& config,
                                 ListenerCallbacks& callbacks) = 0;

  /**
   * Remove a listener
   */
  virtual void removeListener(const std::string& name) = 0;

  /**
   * Get a listener by name
   */
  virtual Listener* getListener(const std::string& name) = 0;

  /**
   * Get all listeners
   */
  virtual std::vector<std::reference_wrapper<Listener>> getAllListeners() = 0;

  /**
   * Stop all listeners
   */
  virtual void stopListeners() = 0;
};

/**
 * Listener manager implementation
 */
class ListenerManagerImpl : public ListenerManager {
 public:
  ListenerManagerImpl(event::Dispatcher& dispatcher,
                      SocketInterface& socket_interface);
  ~ListenerManagerImpl() override;

  // ListenerManager interface
  VoidResult addListener(ListenerConfig&& config,
                         ListenerCallbacks& callbacks) override;
  void removeListener(const std::string& name) override;
  Listener* getListener(const std::string& name) override;
  std::vector<std::reference_wrapper<Listener>> getAllListeners() override;
  void stopListeners() override;

 private:
  event::Dispatcher& dispatcher_;
  SocketInterface& socket_interface_;

  // Active listeners by name
  std::map<std::string, std::unique_ptr<ActiveListener>> listeners_;
};

}  // namespace network
}  // namespace mcp

#endif  // MCP_NETWORK_LISTENER_H