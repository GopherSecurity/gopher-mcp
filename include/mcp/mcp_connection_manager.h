#ifndef MCP_MCP_CONNECTION_MANAGER_H
#define MCP_MCP_CONNECTION_MANAGER_H

#include <functional>
#include <map>
#include <memory>

#include "mcp/core/result.h"
#include "mcp/event/event_loop.h"
#include "mcp/json/json_bridge.h"
#include "mcp/message_dispatch_context.h"
#include "mcp/network/connection_manager.h"
#include "mcp/network/filter.h"
#include "mcp/transport/http_sse_transport_socket.h"
#include "mcp/transport/stdio_transport_socket.h"
#include "mcp/transport/streamable_http_client_session.h"
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
  std::map<std::string, std::string> http_headers;
  std::shared_ptr<std::map<std::string, std::string>> current_http_headers;

  // Streamable HTTP client only: the session this connection serves.
  // Shared rather than owned because a session outlives any one
  // connection — a client that reconnects keeps the one it has.
  transport::StreamableHttpClientSessionPtr streamable_client_session;
};

/**
 * Whether answering a request needs a response that can carry more than
 * one message.
 *
 * Declared rather than discovered, because the choice has to be made
 * before the handler runs and cannot be revisited afterwards: how a
 * response is framed is settled by its first byte.
 *
 *   None      one message, and nothing before it. The fast path.
 *   Optional  may report progress on the way, but does not need to. A
 *             client that cannot read a stream still gets its answer;
 *             the progress is simply dropped.
 *   Required  will ask the client something and wait for the reply. A
 *             client that cannot read a stream cannot be served at all,
 *             and must be told so rather than left holding a request
 *             whose handler is waiting on a question it never saw.
 */
enum class StreamingMode { None, Optional, Required };

/**
 * MCP protocol callbacks
 * Handles both protocol messages and connection events
 */
class McpProtocolCallbacks {
 public:
  virtual ~McpProtocolCallbacks() = default;

  /**
   * What kind of response this request will need, asked before it is
   * dispatched. Takes the whole request rather than the method so that a
   * receiver can answer per tool or per parameter, not just per method.
   */
  virtual StreamingMode streamingFor(const jsonrpc::Request& request) const {
    (void)request;
    return StreamingMode::None;
  }

  /**
   * Called when a request is received
   */
  virtual void onRequest(const jsonrpc::Request& request) = 0;

  /**
   * Called when a notification is received
   */
  virtual void onNotification(const jsonrpc::Notification& notification) = 0;

  /**
   * Context-carrying variants: the message arrives together with a
   * per-message dispatch context describing its origin (connection,
   * transport session id) and reply path. Producers that know the origin
   * call these; the defaults forward to the context-free hooks so existing
   * implementations keep working unchanged. Receivers that route replies or
   * key sessions should override these instead of the context-free forms —
   * the context makes "respond to the wrong connection" and "inherit a
   * stale session binding" unrepresentable, where ambient
   * current-connection state cannot.
   *
   * Distinct names (rather than overloads of onRequest/onNotification) keep
   * every existing implementation outside the -Woverloaded-virtual hiding
   * trap.
   */
  virtual void onRequestWithContext(const jsonrpc::Request& request,
                                    MessageDispatchContext& context) {
    (void)context;
    onRequest(request);
  }

  virtual void onNotificationWithContext(
      const jsonrpc::Notification& notification,
      MessageDispatchContext& context) {
    (void)context;
    onNotification(notification);
  }

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
   * The HTTP status a response carried, and the request it answered
   * (Streamable HTTP client only).
   *
   * A status is not a JSON-RPC message: it carries no id, and the body
   * behind a refusal carries none either. So what a 404 means — this
   * session is gone, start another and send that request again — cannot
   * be worked out from the message layer alone. The request is named
   * here by the order the answers arrive in, and what to do about it is
   * decided a layer up, where the requests are.
   *
   * @param status_code HTTP status of the response
   * @param request_id Request the response answered, empty for a message
   *        that had no id or a response nobody asked for
   * @param detail Body of a refusal, for the error a caller is given
   */
  virtual void onTransportStatus(int status_code,
                                 const optional<RequestId>& request_id,
                                 const std::string& detail) {
    (void)status_code;
    (void)request_id;
    (void)detail;
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
   * Streamable HTTP client: give the session back, by asking the server
   * to end it. Best effort and answerless — a server that does not allow
   * clients to end sessions says 405, one that has already forgotten
   * this one says 404, and neither changes what happens next here.
   *
   * Returns false when there is no session to give back or nothing to
   * write it on. Dispatcher thread.
   */
  bool sendSessionDelete();

  /**
   * Send a request
   */
  VoidResult sendRequest(const jsonrpc::Request& request);
  VoidResult sendRequest(
      const jsonrpc::Request& request,
      const std::map<std::string, std::string>& http_headers);

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
   * True when this manager's transport owns the given connection. Session-
   * targeted server pushes use this to route a connection-keyed session
   * (stdio: the dispatch context keys the session on the pipe connection)
   * back through the manager that can frame and write on that transport.
   */
  bool ownsConnection(const network::Connection* connection) const {
    return connection != nullptr && active_connection_.get() == connection;
  }

  /**
   * Set protocol callbacks
   */
  void setProtocolCallbacks(McpProtocolCallbacks& callbacks) {
    protocol_callbacks_ = &callbacks;
  }

  /**
   * Resolve a possibly-relative SSE callback endpoint against the server
   * address the connection was configured with.
   *
   * The SSE "endpoint" event may carry a relative callback URL — the server
   * announces "callback/{id}" unless an operator configured an external
   * URL — but sendHttpPost() requires an absolute URL. Relative forms
   * resolve to the same scheme and host:port the SSE stream itself uses,
   * with the path made absolute. Already-absolute endpoints pass through
   * untouched, as does everything when server_address is empty (there is
   * nothing to resolve against; sendHttpPost will report the bad URL).
   *
   * Static so the resolution rules are unit-testable without a live
   * connection.
   */
  static std::string resolveEndpointUrl(const std::string& endpoint,
                                        const std::string& server_address,
                                        bool use_ssl);

  /**
   * Clear protocol callbacks before callback owner teardown.
   */
  void clearProtocolCallbacks() { protocol_callbacks_ = nullptr; }

  // McpProtocolCallbacks interface (default implementations)
  void onRequest(const jsonrpc::Request& request) override;
  void onNotification(const jsonrpc::Notification& notification) override;
  void onResponse(const jsonrpc::Response& response) override;
  // Forward the per-message origin alongside the message so the application
  // layer can key sessions and route replies without ambient state.
  void onRequestWithContext(const jsonrpc::Request& request,
                            MessageDispatchContext& context) override;
  void onNotificationWithContext(const jsonrpc::Notification& notification,
                                 MessageDispatchContext& context) override;
  void onConnectionEvent(network::ConnectionEvent event) override;
  void onError(const Error& error) override;
  void onMessageEndpoint(const std::string& endpoint) override;
  void onTransportStatus(int status_code,
                         const optional<RequestId>& request_id,
                         const std::string& detail) override;
  bool sendHttpPost(const std::string& json_body) override;
  bool sendHttpPost(const std::string& json_body,
                    const std::map<std::string, std::string>& http_headers);

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

  // Send JSON message. The id, where the message has one, is noted
  // against the response it will draw — see recordSent(), and note that
  // this happens inside the posted write and not here, because the order
  // the writes are posted in is the order the answers come back in.
  VoidResult sendJsonMessage(const json::JsonValue& message);
  VoidResult sendJsonMessage(
      const json::JsonValue& message,
      const std::map<std::string, std::string>& http_headers);
  VoidResult sendJsonMessage(
      const json::JsonValue& message,
      const std::map<std::string, std::string>& http_headers,
      const optional<RequestId>& correlate);

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
 * auto init_request = make_initialize_request(
 *     protocol::kDefaultProtocolVersion,
 *     make<ClientCapabilities>().build());
 * mcp_manager->sendRequest(init_request);
 *
 * // Run event loop
 * dispatcher->run(event::RunType::Block);
 */

}  // namespace mcp

#endif  // MCP_MCP_CONNECTION_MANAGER_H
