#ifndef MCP_MCP_CONNECTION_MANAGER_H
#define MCP_MCP_CONNECTION_MANAGER_H

#include <functional>
#include <memory>

#include "mcp/event/event_loop.h"
#include "mcp/network/connection_manager.h"
#include "mcp/network/filter.h"
#include "mcp/transport/stdio_transport_socket.h"
#include "mcp/transport/http_sse_transport_socket.h"
#include "mcp/types.h"

namespace mcp {

/**
 * MCP transport type
 */
enum class TransportType {
  Stdio,    // Standard I/O transport
  HttpSse,  // HTTP with Server-Sent Events
  WebSocket // WebSocket transport (future)
};

/**
 * MCP connection configuration
 */
struct McpConnectionConfig {
  // Transport type
  TransportType transport_type{TransportType::Stdio};
  
  // Transport-specific configuration
  optional<transport::StdioTransportSocketConfig> stdio_config;
  optional<transport::HttpSseTransportSocketConfig> http_sse_config;
  
  // Connection settings
  uint32_t buffer_limit{1024 * 1024};  // 1MB default
  std::chrono::milliseconds connection_timeout{30000};
  
  // Message framing
  bool use_message_framing{true};  // Add message length prefix
};

/**
 * MCP message callbacks
 */
class McpMessageCallbacks {
public:
  virtual ~McpMessageCallbacks() = default;

  /**
   * Called when a request is received
   */
  virtual void onRequest(const jsonrpc::Request& request) = 0;

  /**
   * Called when a notification is received
   */
  virtual void onNotification(const jsonrpc::Notification& notification) = 0;

  /**
   * Called when a response is received
   */
  virtual void onResponse(const jsonrpc::Response& response) = 0;

  /**
   * Called on connection event
   */
  virtual void onConnectionEvent(network::ConnectionEvent event) = 0;

  /**
   * Called on connection error
   */
  virtual void onError(const Error& error) = 0;
};

/**
 * JSON-RPC message filter for MCP
 * 
 * Handles JSON-RPC message framing and parsing
 */
class JsonRpcMessageFilter : public network::NetworkFilterBase {
public:
  explicit JsonRpcMessageFilter(McpMessageCallbacks& callbacks);

  // Filter interface
  network::FilterStatus onData(Buffer& data, bool end_stream) override;
  network::FilterStatus onNewConnection() override;
  network::FilterStatus onWrite(Buffer& data, bool end_stream) override;

private:
  // Parse and dispatch messages
  void parseMessages(Buffer& data);
  bool parseMessage(const std::string& json_str);
  
  // Frame outgoing messages
  void frameMessage(Buffer& data);

  McpMessageCallbacks& callbacks_;
  std::string partial_message_;
  bool use_framing_{true};
};

/**
 * MCP connection manager
 * 
 * High-level interface for managing MCP connections
 */
class McpConnectionManager : public McpMessageCallbacks {
public:
  McpConnectionManager(event::Dispatcher& dispatcher,
                       network::SocketInterface& socket_interface,
                       const McpConnectionConfig& config);
  ~McpConnectionManager() override;

  /**
   * Connect to MCP server (client mode)
   */
  Result<void> connect();

  /**
   * Listen for MCP connections (server mode)
   */
  Result<void> listen(const network::Address::InstanceConstSharedPtr& address);

  /**
   * Send a request
   */
  Result<void> sendRequest(const jsonrpc::Request& request);

  /**
   * Send a notification
   */
  Result<void> sendNotification(const jsonrpc::Notification& notification);

  /**
   * Send a response
   */
  Result<void> sendResponse(const jsonrpc::Response& response);

  /**
   * Close the connection
   */
  void close();

  /**
   * Check if connected
   */
  bool isConnected() const;

  /**
   * Set message callbacks
   */
  void setMessageCallbacks(McpMessageCallbacks& callbacks) { message_callbacks_ = &callbacks; }

  // McpMessageCallbacks interface (default implementations)
  void onRequest(const jsonrpc::Request& request) override;
  void onNotification(const jsonrpc::Notification& notification) override;
  void onResponse(const jsonrpc::Response& response) override;
  void onConnectionEvent(network::ConnectionEvent event) override;
  void onError(const Error& error) override;

private:
  // Create transport socket factory
  network::TransportSocketFactoryPtr createTransportSocketFactory();
  
  // Create filter chain factory
  std::shared_ptr<network::FilterChainFactory> createFilterChainFactory();
  
  // Send JSON message
  Result<void> sendJsonMessage(const nlohmann::json& message);

  event::Dispatcher& dispatcher_;
  network::SocketInterface& socket_interface_;
  McpConnectionConfig config_;
  
  // Connection management
  std::unique_ptr<network::ConnectionManager> connection_manager_;
  network::ConnectionPtr active_connection_;
  
  // Message callbacks
  McpMessageCallbacks* message_callbacks_{nullptr};
  
  // State
  bool is_server_{false};
  bool connected_{false};
};

/**
 * Factory function for creating MCP connection manager
 */
inline std::unique_ptr<McpConnectionManager> 
createMcpConnectionManager(event::Dispatcher& dispatcher,
                           const McpConnectionConfig& config = {}) {
  return std::make_unique<McpConnectionManager>(
      dispatcher, 
      network::socketInterface(),
      config);
}

/**
 * Example usage:
 * 
 * // Create event loop
 * auto dispatcher = event::createPlatformDefaultDispatcherFactory()->createDispatcher("main");
 * 
 * // Configure stdio transport
 * McpConnectionConfig config;
 * config.transport_type = TransportType::Stdio;
 * 
 * // Create connection manager
 * auto mcp_manager = createMcpConnectionManager(*dispatcher, config);
 * 
 * // Set callbacks
 * mcp_manager->setMessageCallbacks(my_callbacks);
 * 
 * // Connect (for stdio, this is immediate)
 * auto result = mcp_manager->connect();
 * if (is_error(result)) {
 *   // Handle error
 * }
 * 
 * // Send initialize request
 * auto init_request = make_initialize_request("2024-11-05", 
 *     build_client_capabilities().build());
 * mcp_manager->sendRequest(init_request);
 * 
 * // Run event loop
 * dispatcher->run(event::RunType::Block);
 */

} // namespace mcp

#endif // MCP_MCP_CONNECTION_MANAGER_H