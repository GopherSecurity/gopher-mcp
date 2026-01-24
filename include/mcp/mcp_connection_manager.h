#ifndef MCP_MCP_CONNECTION_MANAGER_H
#define MCP_MCP_CONNECTION_MANAGER_H

#include <functional>
#include <memory>

#include "mcp/core/result.h"
#include "mcp/event/event_loop.h"
#include "mcp/json/json_bridge.h"
#include "mcp/network/connection_manager.h"
#include "mcp/network/filter.h"
#include "mcp/transport/http_sse_transport_socket.h"
#include "mcp/transport/stdio_transport_socket.h"
#include "mcp/types.h"

namespace mcp {

/**
 * MCP transport type
 */
enum class TransportType {
  Stdio,           // Standard I/O transport
  HttpSse,         // HTTP with Server-Sent Events
  StreamableHttp,  // Streamable HTTP (simple POST request/response)
  WebSocket        // WebSocket transport (future)
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

  // Protocol detection
  bool use_protocol_detection{false};  // Enable automatic protocol detection

  // HTTP endpoint configuration (for HTTP/SSE transport)
  std::string http_path{"/rpc"};  // Request path (e.g., /sse, /mcp)
  std::string
      http_host;  // Host header value (auto-set from server_address if empty)
};

/**
 * MCP protocol callbacks
 * Handles both protocol messages and connection events
 */
class McpProtocolCallbacks {
 public:
  virtual ~McpProtocolCallbacks() = default;

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

  /**
   * Called when SSE endpoint is received (HTTP/SSE transport only)
   * The endpoint is the URL to POST JSON-RPC messages to
   */
  virtual void onMessageEndpoint(const std::string& endpoint) {
    (void)endpoint;  // Default implementation does nothing
  }

  /**
   * Send a POST request to the message endpoint
   * Used by HTTP/SSE transport to send messages on a separate connection
   * Returns true if the POST was initiated successfully
   */
  virtual bool sendHttpPost(const std::string& json_body) {
    (void)json_body;  // Default implementation does nothing
    return false;
  }
};

/**
 * MCP connection manager
 *
 * High-level interface for managing MCP connections
 */
class McpConnectionManager : public McpProtocolCallbacks,
                             public network::ListenerCallbacks,
                             public network::ConnectionCallbacks {
 public:
  McpConnectionManager(event::Dispatcher& dispatcher,
                       network::SocketInterface& socket_interface,
                       const McpConnectionConfig& config);
  ~McpConnectionManager() override;

  /**
   * Connect to MCP server (client mode)
   */
  VoidResult connect();

  /**
   * Listen for MCP connections (server mode)
   */
  VoidResult listen(const network::Address::InstanceConstSharedPtr& address);

  /**
   * Send a request
   */
  VoidResult sendRequest(const jsonrpc::Request& request);

  /**
   * Send a notification
   */
  VoidResult sendNotification(const jsonrpc::Notification& notification);

  /**
   * Send a response
   */
  VoidResult sendResponse(const jsonrpc::Response& response);

  /**
   * Close the connection
   */
  void close();

  /**
   * Check if connected
   */
  bool isConnected() const;

  /**
   * Set protocol callbacks
   */
  void setProtocolCallbacks(McpProtocolCallbacks& callbacks) {
    protocol_callbacks_ = &callbacks;
  }

  // McpProtocolCallbacks interface (default implementations)
  void onRequest(const jsonrpc::Request& request) override;
  void onNotification(const jsonrpc::Notification& notification) override;
  void onResponse(const jsonrpc::Response& response) override;
  void onConnectionEvent(network::ConnectionEvent event) override;
  void onError(const Error& error) override;
  void onMessageEndpoint(const std::string& endpoint) override;
  bool sendHttpPost(const std::string& json_body) override;

  // ListenerCallbacks interface
  void onAccept(network::ConnectionSocketPtr&& socket) override;
  void onNewConnection(network::ConnectionPtr&& connection) override;

  // ConnectionCallbacks interface
  void onEvent(network::ConnectionEvent event) override {
    onConnectionEvent(event);
  }
  void onAboveWriteBufferHighWatermark() override {}
  void onBelowWriteBufferLowWatermark() override {}

 private:
  // Create transport socket factory
  std::unique_ptr<network::TransportSocketFactoryBase>
  createTransportSocketFactory();

  // Create filter chain factory
  std::shared_ptr<network::FilterChainFactory> createFilterChainFactory();

  // Send JSON message
  VoidResult sendJsonMessage(const json::JsonValue& message);

  event::Dispatcher& dispatcher_;
  network::SocketInterface& socket_interface_;
  McpConnectionConfig config_;

  // Connection management
  std::unique_ptr<network::ConnectionManager> connection_manager_;
  network::ConnectionPtr active_connection_;

  // Server listener management
  // Must keep listener manager alive for the lifetime of the server
  std::unique_ptr<network::ListenerManager> listener_manager_;

  // Protocol callbacks
  McpProtocolCallbacks* protocol_callbacks_{nullptr};

  // State
  bool is_server_{false};
  bool connected_{false};
  bool processing_connected_event_{false};  // Guard against re-entrancy

  // HTTP/SSE POST connection support
  std::string
      message_endpoint_;  // URL for POST requests (from SSE endpoint event)
  bool has_message_endpoint_{false};

  // Active POST connection (for sending messages in HTTP/SSE mode)
  std::unique_ptr<network::ClientConnection> post_connection_;
  std::unique_ptr<network::ConnectionCallbacks> post_callbacks_;
};

/**
 * Factory function for creating MCP connection manager
 */
inline std::unique_ptr<McpConnectionManager> createMcpConnectionManager(
    event::Dispatcher& dispatcher, const McpConnectionConfig& config = {}) {
  return std::make_unique<McpConnectionManager>(
      dispatcher, network::socketInterface(), config);
}

/**
 * Example usage:
 *
 * // Create event loop
 * auto dispatcher =
 * event::createPlatformDefaultDispatcherFactory()->createDispatcher("main");
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
 *     make<ClientCapabilities>().build());
 * mcp_manager->sendRequest(init_request);
 *
 * // Run event loop
 * dispatcher->run(event::RunType::Block);
 */

}  // namespace mcp

#endif  // MCP_MCP_CONNECTION_MANAGER_H