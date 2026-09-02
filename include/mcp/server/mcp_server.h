/**
 * @file mcp_server.h
 * @brief Enterprise-grade MCP server with production features
 *
 * This provides a production-ready MCP server with:
 * - Multi-transport support (stdio, HTTP+SSE, WebSocket)
 * - Worker thread model for scalability
 * - Flow control with watermark-based backpressure
 * - Request/notification handler registration
 * - Comprehensive metrics and monitoring
 * - Graceful shutdown handling
 * - Filter chain architecture for extensibility
 * - Resource management with subscription support
 * - Tool registration and execution
 * - Prompt management
 */

#ifndef MCP_SERVER_H
#define MCP_SERVER_H

#include <atomic>
#include <chrono>
#include <functional>
#include <future>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <queue>
#include <set>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "mcp/buffer.h"
#include "mcp/builders.h"
#include "mcp/core/request_id_key.h"
#include "mcp/event/event_loop.h"
#include "mcp/filter/filter_chain_callbacks.h"
#include "mcp/filter/filter_chain_event_hub.h"
#include "mcp/filter/http_sse_filter_chain_factory.h"
#include "mcp/filter/metrics_filter.h"
#include "mcp/json/json_bridge.h"
#include "mcp/logging/log_macros.h"
#include "mcp/mcp_application_base.h"  // TODO: Migrate to mcp_application_base_refactored.h
#include "mcp/mcp_connection_manager.h"
#include "mcp/network/filter.h"
#include "mcp/protocol/designated_params.h"
#include "mcp/protocol/mrtr.h"
#include "mcp/server/listen_registry.h"
#include "mcp/transport/streamable_http_config.h"
#include "mcp/types.h"

// Define log component for this file
#undef GOPHER_LOG_COMPONENT
#define GOPHER_LOG_COMPONENT "server"

namespace mcp {

// Forward declarations from config layer
namespace config {
struct ListenerConfig;
}  // namespace config

// Forward declarations from network layer
namespace network {
class ListenerCallbacks;
class TcpActiveListener;
}  // namespace network

namespace server {

// Forward declarations
class RequestHandler;
class ResourceManager;
class ToolRegistry;
class PromptRegistry;
class SessionManager;
class SessionContext;

/**
 * Server configuration
 */
struct McpServerConfig : public application::ApplicationBase::Config {
  // Protocol configuration
  std::string protocol_version = protocol::kDefaultProtocolVersion;
  std::string server_name = "mcp-cpp-server";
  std::string server_version = "1.0.0";
  std::string instructions;  // Optional server instructions
  std::function<std::string(const jsonrpc::Request&, SessionContext&)>
      instructions_provider;

  // Whether the tools capability advertises listChanged. Default false, which
  // is what a server whose tool set is fixed at startup should say.
  //
  // A server that discovers tools after initialize -- an aggregator whose
  // backends come online later, say -- must advertise true, or clients have
  // no reason to re-list and will show whatever was available at connect
  // time forever. Such a server is expected to actually send
  // notifications/tools/list_changed when its set changes.
  bool tools_list_changed = false;

  // Transport configuration
  std::vector<TransportType> supported_transports = {TransportType::Stdio,
                                                     TransportType::HttpSse};

  // Optional override for the stdio transport's socket configuration.
  // Defaults to the process's real stdin/stdout; embedders and tests point
  // it at pipe fds instead (with use_bridge=false) to drive a stdio server
  // in-process.
  optional<transport::StdioTransportSocketConfig> stdio_transport_config;

  // HTTP/SSE specific configuration
  // Path for JSON-RPC over HTTP. Matches the streamable HTTP endpoint
  // below; "/rpc" stays routable as an alias for older deployments.
  std::string http_rpc_path = "/mcp";
  std::string http_sse_path = "/sse";        // Path for SSE event stream
  std::string http_health_path = "/health";  // Path for health check endpoint
  // Absolute URL the server is reachable at from the client's perspective
  // (scheme + host + port + optional path prefix). Used to build the
  // endpoint-event callback URL advertised on GET /sse. Leave empty to
  // have the server derive a URL from the incoming Host header; set
  // explicitly when the server sits behind a reverse proxy that rewrites
  // scheme or path so clients don't try to POST back to an internal URL.
  std::string external_url;

  // Streamable HTTP endpoint settings: path, session and stream policy,
  // and the protocol revisions this server can actually serve.
  transport::StreamableHttpConfig streamable_http;

  // Session management
  size_t max_sessions = 100;
  std::chrono::milliseconds session_timeout{300000};  // 5 minutes
  bool allow_concurrent_sessions = true;

  // Per-connection idle-read timeout applied to every accepted connection.
  // If no bytes arrive within the window the connection is closed with
  // FlushWrite so an in-flight response can still drain. Zero disables
  // the feature, which is the default so existing deployments see no
  // behavior change.
  std::chrono::milliseconds idle_read_timeout{0};

  // Request processing
  size_t request_queue_size = 1000;
  std::chrono::milliseconds request_processing_timeout{60000};
  bool enable_request_validation = true;

  // Resource management
  bool enable_resource_subscriptions = true;
  size_t max_subscriptions_per_session = 100;
  std::chrono::milliseconds resource_update_debounce{100};

  // Capabilities
  ServerCapabilities capabilities;

  // Filter chain configuration (optional)
  // If provided, uses ConfigurableFilterChainFactory instead of hardcoded
  // factories
  optional<json::JsonValue> filter_chain_config;

  // Filter factories for HTTP-level processing (optional)
  // These factories are invoked during chain creation to add filters
  // that run before protocol filters (HTTP/SSE/JSON-RPC).
  // Useful for authentication, logging, or other cross-cutting concerns.
  // This follows the existing FilterFactoryCb pattern used throughout
  // gopher-mcp. Example: Add an OAuth auth filter factory to validate tokens
  // before processing
  std::vector<network::FilterFactoryCb> filter_factories;

  // Callback for registering custom HTTP routes (optional)
  // Called when filter chain is created, allowing registration of custom
  // endpoints like OAuth discovery (/.well-known/oauth-protected-resource).
  // Example: registerOAuthEndpoints(router, config);
  filter::HttpRouteRegistrationCallback route_registration_callback;
};

/**
 * Server statistics
 */
struct McpServerStats : public application::ApplicationStats {
  // Session metrics
  std::atomic<uint64_t> sessions_total{0};
  std::atomic<uint64_t> sessions_active{0};
  std::atomic<uint64_t> sessions_expired{0};

  // Request metrics
  std::atomic<uint64_t> notifications_total{0};
  std::atomic<uint64_t> requests_invalid{0};
  std::atomic<uint64_t> requests_unauthorized{0};

  // Resource metrics
  std::atomic<uint64_t> resources_served{0};
  std::atomic<uint64_t> resources_subscribed{0};
  std::atomic<uint64_t> resource_updates_sent{0};

  // Tool metrics
  std::atomic<uint64_t> tools_executed{0};
  std::atomic<uint64_t> tools_failed{0};

  // Prompt metrics
  std::atomic<uint64_t> prompts_retrieved{0};

  // Filter-related metrics
  std::atomic<uint64_t> circuit_breaker_trips{0};
  std::atomic<uint64_t> circuit_requests_blocked{0};
  std::atomic<uint64_t> rate_limited_requests{0};
  std::atomic<uint64_t> backpressure_events{0};
  std::atomic<uint64_t> bytes_dropped{0};
  std::atomic<uint64_t> threshold_violations{0};
  std::atomic<double> current_success_rate{1.0};
  std::atomic<uint64_t> average_latency_ms{0};

  // Responses that arrived from a client with no request of ours waiting
  // for them. Counted rather than ignored: a non-zero value means either a
  // confused peer or a waiter released too early, and neither is visible
  // any other way.
  std::atomic<uint64_t> responses_unmatched{0};

  // Questions this server asked a client, and the ones that ran out of
  // time. A rising second number is a client that accepts questions and
  // does not answer them, which looks from the outside like slow tools.
  std::atomic<uint64_t> requests_to_clients{0};
  std::atomic<uint64_t> client_requests_timed_out{0};
};

/**
 * Matches responses arriving from a client to the server-initiated requests
 * waiting for them.
 *
 * A server that asks a client something — to sample a model, to elicit
 * input — has to recognize the answer when it comes back, and the answer
 * arrives as an ordinary inbound message with nothing but a JSON-RPC id
 * connecting it to the question. Without this the answer has nowhere to go.
 *
 * Guarded by a mutex rather than confined to a dispatcher: over HTTP the
 * answer arrives on whichever connection the client chose to send it on,
 * which is not the one the question went out on.
 */
class ClientRequestCorrelator {
 public:
  using Waiter = std::function<void(const jsonrpc::Response&)>;

  /** Register interest in the answer to a request already sent. */
  void expect(const RequestId& id, Waiter waiter);

  /**
   * Hand a response to whoever was waiting for it.
   * @return False when nobody was; the caller should count and drop it.
   */
  bool deliver(const jsonrpc::Response& response);

  /** Give up on a request, so a waiter is not left pending forever. */
  bool forget(const RequestId& id);

  size_t pending() const;

 private:
  mutable std::mutex mutex_;
  std::map<RequestIdKey, Waiter> waiters_;
};

/**
 * Session context for client connections
 * Tracks per-session state including subscriptions and capabilities
 */
class SessionContext {
 public:
  using SessionId = std::string;

  SessionContext(const SessionId& id, network::Connection* connection)
      : id_(id),
        connection_(connection),
        created_time_(std::chrono::steady_clock::now()),
        last_activity_(std::chrono::steady_clock::now()) {}

  const SessionId& getId() const { return id_; }
  network::Connection* getConnection() const { return connection_; }

  // Transport-level session id (e.g. the SSE stream id for HTTP+SSE
  // clients). Non-empty only for sessions keyed on a transport session
  // rather than a connection: those clients send each request on a
  // short-lived POST connection, so the connection pointer cannot serve
  // as the durable identity — this id can, and it is also what the
  // server-push path uses to find the client's SSE stream.
  void setTransportSessionId(const std::string& transport_session_id) {
    transport_session_id_ = transport_session_id;
  }
  const std::string& getTransportSessionId() const {
    return transport_session_id_;
  }

  // Somewhere to put an answer that is not ready yet, for a handler
  // registered as streaming. Set for the length of one dispatch and null
  // otherwise, since a stream belongs to a request rather than a session.
  // A handler that means to finish later keeps its own copy.
  void setResponseStream(const ResponseStreamPtr& stream) {
    response_stream_ = stream;
  }
  const ResponseStreamPtr& responseStream() const { return response_stream_; }

  // Update activity timestamp
  void updateActivity() { last_activity_ = std::chrono::steady_clock::now(); }

  // Check if session is expired
  bool isExpired(std::chrono::milliseconds timeout) const {
    auto now = std::chrono::steady_clock::now();
    return (now - last_activity_) > timeout;
  }

  // Client info after initialization
  void setClientInfo(const optional<Implementation>& info) {
    client_info_ = info;
  }

  const optional<Implementation>& getClientInfo() const { return client_info_; }

  // Protocol revision agreed with this client. Recorded when initialize is
  // answered, so later decisions consult what was actually negotiated
  // rather than re-deriving it or assuming the configured default.
  void setProtocolVersion(const std::string& version) {
    protocol_version_ = version;
  }

  const std::string& getProtocolVersion() const { return protocol_version_; }

  void setNegotiatedProtocolVersion(const std::string& protocol_version) {
    setProtocolVersion(protocol_version);
  }
  const std::string& getNegotiatedProtocolVersion() const {
    return getProtocolVersion();
  }

  void setClientCapabilities(const ClientCapabilities& capabilities) {
    client_capabilities_ = capabilities;
  }
  const ClientCapabilities& getClientCapabilities() const {
    return client_capabilities_;
  }

  // Request-scoped metadata: the in-flight request's params._meta, carried as
  // its stringified-JSON form (consistent with how nested arguments are
  // represented in Metadata). Set immediately before each tool handler is
  // dispatched and cleared when the request carries no _meta, so a handler can
  // read out-of-band correlation ids (e.g. run_id / tool_call_id) without the
  // dispatch forking. Safe as a per-session field because request dispatch is
  // synchronous per request on the dispatcher thread; it is overwritten on the
  // next call.
  void setRequestMeta(const optional<std::string>& meta) {
    request_meta_ = meta;
  }
  const optional<std::string>& getRequestMeta() const { return request_meta_; }

  // Subscription management
  void addSubscription(const std::string& uri) {
    std::lock_guard<std::mutex> lock(mutex_);
    subscriptions_.insert(uri);
  }

  void removeSubscription(const std::string& uri) {
    std::lock_guard<std::mutex> lock(mutex_);
    subscriptions_.erase(uri);
  }

  std::set<std::string> getSubscriptions() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return subscriptions_;
  }

  bool isSubscribed(const std::string& uri) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return subscriptions_.find(uri) != subscriptions_.end();
  }

 private:
  SessionId id_;
  network::Connection* connection_;    // Store raw pointer
  std::string transport_session_id_;   // Durable transport identity, may be ""
  ResponseStreamPtr response_stream_;  // Live only during one dispatch
  std::chrono::steady_clock::time_point created_time_;
  std::chrono::steady_clock::time_point last_activity_;
  optional<Implementation> client_info_;
  std::string protocol_version_;  // negotiated at initialize
  ClientCapabilities client_capabilities_;
  optional<std::string> request_meta_;  // params._meta of the in-flight request

  mutable std::mutex mutex_;
  std::set<std::string> subscriptions_;
};

/**
 * Request handler interface
 * Base class for handling specific request types
 */
class RequestHandler {
 public:
  virtual ~RequestHandler() = default;

  // Handle request and return response
  virtual jsonrpc::Response handle(const jsonrpc::Request& request,
                                   SessionContext& session) = 0;

  // Check if handler can process this method
  virtual bool canHandle(const std::string& method) const = 0;
};

/**
 * Notification handler interface
 */
class NotificationHandler {
 public:
  virtual ~NotificationHandler() = default;

  // Handle notification (no response)
  virtual void handle(const jsonrpc::Notification& notification,
                      SessionContext& session) = 0;

  // Check if handler can process this method
  virtual bool canHandle(const std::string& method) const = 0;
};

/**
 * Resource manager
 * Manages resources and subscriptions
 */
class ResourceManager {
 public:
  // Handler invoked on resources/read to produce the actual content.
  // Receives the URI so a single handler can serve multiple resources.
  using ResourceReadHandler = std::function<ReadResourceResult(
      const std::string& uri, SessionContext& session)>;

  ResourceManager(McpServerStats& stats) : stats_(stats) {}

  // Register a resource with a read handler that supplies content on read.
  void registerResource(const Resource& resource, ResourceReadHandler handler) {
    std::lock_guard<std::mutex> lock(mutex_);
    resources_[resource.uri] = resource;
    resource_handlers_[resource.uri] = handler;
  }

  // Register a resource without a read handler (metadata-only, e.g. for list).
  void registerResource(const Resource& resource) {
    std::lock_guard<std::mutex> lock(mutex_);
    resources_[resource.uri] = resource;
  }

  // Register resource template
  void registerResourceTemplate(const ResourceTemplate& template_) {
    std::lock_guard<std::mutex> lock(mutex_);
    resource_templates_.push_back(template_);
  }

  // List resources with pagination
  ListResourcesResult listResources(const optional<Cursor>& cursor = nullopt) {
    std::lock_guard<std::mutex> lock(mutex_);
    ListResourcesResult result;

    // Simple pagination implementation
    size_t start = 0;
    if (cursor.has_value()) {
      start = std::stoull(cursor.value());
    }

    size_t count = 0;
    const size_t page_size = 100;

    for (auto it = resources_.begin();
         it != resources_.end() && count < page_size; ++it) {
      if (start > 0) {
        start--;
        continue;
      }
      result.resources.push_back(it->second);
      count++;
    }

    // Set next cursor if more resources available
    if (count == page_size && resources_.size() > (start + count)) {
      result.nextCursor = std::to_string(start + count);
    }

    return result;
  }

  // Read resource content by delegating to the registered handler.
  // Returns an empty result when the URI is unknown, and throws if
  // the resource was registered without a read handler.
  ReadResourceResult readResource(const std::string& uri,
                                  SessionContext& session) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto res_it = resources_.find(uri);
    if (res_it == resources_.end()) {
      return ReadResourceResult{};  // unknown resource
    }

    auto handler_it = resource_handlers_.find(uri);
    if (handler_it == resource_handlers_.end()) {
      throw std::runtime_error("Resource registered without a read handler: " +
                               uri);
    }

    auto result = handler_it->second(uri, session);
    stats_.resources_served++;
    return result;
  }

  // Handle subscription
  void subscribe(const std::string& uri, SessionContext& session) {
    std::lock_guard<std::mutex> lock(mutex_);
    subscriptions_[uri].insert(session.getId());
    session.addSubscription(uri);
    stats_.resources_subscribed++;
  }

  // Handle unsubscription
  void unsubscribe(const std::string& uri, SessionContext& session) {
    std::lock_guard<std::mutex> lock(mutex_);
    subscriptions_[uri].erase(session.getId());
    session.removeSubscription(uri);
  }

  // Drop every subscription a session holds. Called when the session ends
  // (connection closed, SSE stream torn down, expiry) so updates for its
  // URIs stop being fanned out to an id that can no longer receive them.
  void releaseSession(SessionContext& session) {
    auto uris = session.getSubscriptions();
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& uri : uris) {
      auto it = subscriptions_.find(uri);
      if (it != subscriptions_.end()) {
        it->second.erase(session.getId());
        if (it->second.empty()) {
          subscriptions_.erase(it);
        }
      }
    }
  }

  // Sessions currently subscribed to a URI (snapshot). The server builds
  // and routes the notifications/resources/updated messages from this;
  // the manager only owns the bookkeeping. This replaces a pending-updates
  // queue that nothing ever drained — updates were counted as sent but
  // silently accumulated forever.
  std::set<std::string> getSubscribers(const std::string& uri) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = subscriptions_.find(uri);
    return it != subscriptions_.end() ? it->second : std::set<std::string>{};
  }

 private:
  mutable std::mutex mutex_;
  std::map<std::string, Resource> resources_;
  std::map<std::string, ResourceReadHandler> resource_handlers_;
  std::vector<ResourceTemplate> resource_templates_;
  std::map<std::string, std::set<std::string>>
      subscriptions_;  // uri -> session_ids
  McpServerStats& stats_;
};

/**
 * Tool registry
 * Manages tool registration and execution
 */
class ToolRegistry {
 public:
  using ToolHandler =
      std::function<CallToolResult(const std::string& name,
                                   const optional<Metadata>& arguments,
                                   SessionContext& session)>;

  ToolRegistry(McpServerStats& stats) : stats_(stats) {}

  // Register a tool
  /**
   * Register a tool, unless its definition cannot be served.
   *
   * A tool whose schema designates arguments to be mirrored into headers
   * in a way both ends cannot resolve identically is refused: every call
   * to it would be rejected for a mismatch neither end introduced, and a
   * tool nobody can call correctly is not a tool. The rest of the
   * registry is unaffected, which is the same shape the protocol asks of
   * a client reading such a definition — one bad tool must not cost the
   * others.
   *
   * @return False when the tool was refused, with the reason logged.
   */
  bool registerTool(const Tool& tool, ToolHandler handler) {
    std::vector<protocol::modern::DesignatedParam> designated;
    auto usable = protocol::modern::designatedParams(tool, &designated);
    if (!holds_alternative<std::nullptr_t>(usable)) {
      GOPHER_LOG_ERROR("{}", get<Error>(usable).message);
      return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    tools_[tool.name] = tool;
    tool_handlers_[tool.name] = handler;
    designated_[tool.name] = std::move(designated);
    return true;
  }

  /** The arguments a tool asks to have carried in headers as well. */
  bool paramsForTool(
      const std::string& tool_name,
      std::vector<protocol::modern::DesignatedParam>* out) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = designated_.find(tool_name);
    if (it == designated_.end()) {
      return false;
    }
    *out = it->second;
    return true;
  }

  // List all tools
  ListToolsResult listTools() {
    std::lock_guard<std::mutex> lock(mutex_);
    ListToolsResult result;
    for (const auto& pair : tools_) {
      result.tools.push_back(pair.second);
    }
    return result;
  }

  // Execute tool
  CallToolResult callTool(const std::string& name,
                          const optional<Metadata>& arguments,
                          SessionContext& session) {
    GOPHER_LOG_DEBUG("ToolRegistry::callTool invoked for: {}", name);

    std::lock_guard<std::mutex> lock(mutex_);

    auto it = tool_handlers_.find(name);
    if (it != tool_handlers_.end()) {
      GOPHER_LOG_DEBUG("Tool handler found, invoking: {}", name);
      try {
        auto result = it->second(name, arguments, session);
        GOPHER_LOG_DEBUG("Tool handler returned successfully for: {}", name);
        stats_.tools_executed++;
        return result;
      } catch (const std::exception& e) {
        GOPHER_LOG_DEBUG("Exception in tool handler for {}: {}", name,
                         e.what());
        stats_.tools_failed++;
        CallToolResult error_result;
        error_result.isError = true;
        error_result.content.push_back(ExtendedContentBlock(
            TextContent("Tool execution failed: " + std::string(e.what()))));
        return error_result;
      }
    }

    // Tool not found
    GOPHER_LOG_DEBUG("Tool not found in registry: {}", name);
    CallToolResult error_result;
    error_result.isError = true;
    error_result.content.push_back(
        ExtendedContentBlock(TextContent("Tool not found: " + name)));
    return error_result;
  }

 private:
  mutable std::mutex mutex_;
  std::map<std::string, Tool> tools_;
  std::map<std::string, ToolHandler> tool_handlers_;
  // Derived once at registration rather than at each call: the schema
  // does not change, and a call is not the moment to discover that a tool
  // was never usable.
  std::map<std::string, std::vector<protocol::modern::DesignatedParam>>
      designated_;
  McpServerStats& stats_;
};

/**
 * Prompt registry
 * Manages prompt templates
 */
class PromptRegistry {
 public:
  using PromptHandler =
      std::function<GetPromptResult(const std::string& name,
                                    const optional<Metadata>& arguments,
                                    SessionContext& session)>;

  PromptRegistry(McpServerStats& stats) : stats_(stats) {}

  // Register a prompt
  void registerPrompt(const Prompt& prompt, PromptHandler handler) {
    std::lock_guard<std::mutex> lock(mutex_);
    prompts_[prompt.name] = prompt;
    prompt_handlers_[prompt.name] = handler;
  }

  // List all prompts
  ListPromptsResult listPrompts(const optional<Cursor>& cursor = nullopt) {
    std::lock_guard<std::mutex> lock(mutex_);
    ListPromptsResult result;
    for (const auto& pair : prompts_) {
      result.prompts.push_back(pair.second);
    }
    return result;
  }

  // Get prompt
  GetPromptResult getPrompt(const std::string& name,
                            const optional<Metadata>& arguments,
                            SessionContext& session) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = prompt_handlers_.find(name);
    if (it != prompt_handlers_.end()) {
      stats_.prompts_retrieved++;
      return it->second(name, arguments, session);
    }

    // Return empty result if not found
    return GetPromptResult();
  }

 private:
  mutable std::mutex mutex_;
  std::map<std::string, Prompt> prompts_;
  std::map<std::string, PromptHandler> prompt_handlers_;
  McpServerStats& stats_;
};

/**
 * Session manager
 * Manages client sessions and their lifecycle
 */
class SessionManager {
 public:
  using SessionPtr = std::shared_ptr<SessionContext>;

  SessionManager(const McpServerConfig& config, McpServerStats& stats)
      : config_(config), stats_(stats) {}

  // Observer invoked once for each session removed by the expiry sweep, on
  // the thread that triggered the sweep (the dispatcher thread: background
  // cleanup timer, or the at-capacity path of a session create). It fires
  // AFTER mutex_ is released, so the callback may safely take other locks
  // (e.g. ResourceManager's) without lock-ordering hazards. This is how the
  // server releases state it keys on a session — resource subscriptions —
  // that would otherwise leak when a session times out. The explicit
  // removeSession/removeSessionByConnection/removeSessionByTransportId
  // paths do NOT fire it; their callers already hold the removed session
  // and release such state directly.
  using SessionRemovedCallback = std::function<void(const SessionPtr&)>;
  void setSessionRemovedCallback(SessionRemovedCallback callback) {
    session_removed_callback_ = std::move(callback);
  }

  // Create new session
  SessionPtr createSession(network::Connection* connection) {
    SessionPtr session;
    std::vector<SessionPtr> expired;
    {
      std::lock_guard<std::mutex> lock(mutex_);

      // Check max sessions limit. Use the Locked variant — mutex_ is already
      // held here and is not recursive, so calling the public
      // cleanupExpiredSessions() would self-deadlock.
      if (sessions_.size() >= config_.max_sessions) {
        // Try to clean up expired sessions first
        cleanupExpiredSessionsLocked(expired);
        if (sessions_.size() >= config_.max_sessions) {
          fireSessionRemoved(expired);
          return nullptr;  // Max sessions reached
        }
      }

      // Generate session ID
      std::string session_id = generateSessionId();

      // Create session
      session = std::make_shared<SessionContext>(session_id, connection);
      sessions_[session_id] = session;

      // Track by connection if available
      if (connection) {
        connection_sessions_[connection] = session;
      }

      stats_.sessions_total++;
      stats_.sessions_active++;
    }

    // Fire removal observers for any sessions the capacity sweep evicted,
    // outside the lock.
    fireSessionRemoved(expired);
    return session;
  }

  // Get session by ID
  SessionPtr getSession(const std::string& session_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(session_id);
    if (it != sessions_.end()) {
      it->second->updateActivity();
      return it->second;
    }
    return nullptr;
  }

  // Get-or-create a session keyed on a transport-level session id (e.g.
  // the SSE stream id). Used for transports where each request arrives on
  // a fresh short-lived connection: the connection pointer changes per
  // request, so keying on it would give every request its own session and
  // lose state such as resource subscriptions. The session is deliberately
  // created with a null connection — it must NOT be tracked in
  // connection_sessions_, or the close of whichever POST connection it was
  // first seen on would tear it down. Its lifetime is bounded by the
  // transport session instead (removeSessionByTransportId on SSE stream
  // close) plus the usual expiry sweep.
  SessionPtr getOrCreateSessionByTransportId(
      const std::string& transport_session_id) {
    SessionPtr session;
    std::vector<SessionPtr> expired;
    {
      std::lock_guard<std::mutex> lock(mutex_);

      auto it = transport_sessions_.find(transport_session_id);
      if (it != transport_sessions_.end()) {
        it->second->updateActivity();
        return it->second;
      }

      if (sessions_.size() >= config_.max_sessions) {
        cleanupExpiredSessionsLocked(expired);
        if (sessions_.size() >= config_.max_sessions) {
          fireSessionRemoved(expired);
          return nullptr;  // Max sessions reached
        }
      }

      session = std::make_shared<SessionContext>(generateSessionId(), nullptr);
      session->setTransportSessionId(transport_session_id);
      sessions_[session->getId()] = session;
      transport_sessions_[transport_session_id] = session;

      stats_.sessions_total++;
      stats_.sessions_active++;
    }

    fireSessionRemoved(expired);
    return session;
  }

  // Get session by transport-level session id (no creation).
  SessionPtr getSessionByTransportId(const std::string& transport_session_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = transport_sessions_.find(transport_session_id);
    return (it != transport_sessions_.end()) ? it->second : nullptr;
  }

  // Remove the session bound to a transport-level session id. Called when
  // the transport session ends (e.g. the client's SSE stream closes).
  // Returns the removed session so the caller can release related state
  // (e.g. resource subscriptions) that lives outside this manager.
  SessionPtr removeSessionByTransportId(
      const std::string& transport_session_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = transport_sessions_.find(transport_session_id);
    if (it == transport_sessions_.end()) {
      return nullptr;
    }
    auto session = it->second;
    transport_sessions_.erase(it);
    sessions_.erase(session->getId());
    stats_.sessions_active--;
    return session;
  }

  // Remove session
  void removeSession(const std::string& session_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(session_id);
    if (it != sessions_.end()) {
      // Remove from connection map if tracked
      if (it->second->getConnection()) {
        connection_sessions_.erase(it->second->getConnection());
      }
      if (!it->second->getTransportSessionId().empty()) {
        transport_sessions_.erase(it->second->getTransportSessionId());
      }
      sessions_.erase(it);
      stats_.sessions_active--;
    }
  }

  // Remove session by connection. Returns the removed session (or null)
  // so the caller can release related state, mirroring
  // removeSessionByTransportId.
  SessionPtr removeSessionByConnection(network::Connection* connection) {
    if (!connection)
      return nullptr;

    std::lock_guard<std::mutex> lock(mutex_);
    auto conn_it = connection_sessions_.find(connection);
    if (conn_it == connection_sessions_.end()) {
      return nullptr;
    }
    auto session = conn_it->second;
    connection_sessions_.erase(conn_it);
    sessions_.erase(session->getId());
    stats_.sessions_active--;
    return session;
  }

  // Get session by connection
  SessionPtr getSessionByConnection(network::Connection* connection) {
    if (!connection)
      return nullptr;

    std::lock_guard<std::mutex> lock(mutex_);
    auto it = connection_sessions_.find(connection);
    return (it != connection_sessions_.end()) ? it->second : nullptr;
  }

  // Clean up expired sessions. Fires the session-removed observer for each
  // swept session, outside the lock.
  void cleanupExpiredSessions() {
    std::vector<SessionPtr> expired;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      cleanupExpiredSessionsLocked(expired);
    }
    fireSessionRemoved(expired);
  }

  // Enumerate active sessions (snapshot). Used by broadcast paths, which
  // must not hold the session mutex while writing to connections.
  std::vector<SessionPtr> getAllSessions() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<SessionPtr> result;
    result.reserve(sessions_.size());
    for (const auto& pair : sessions_) {
      result.push_back(pair.second);
    }
    return result;
  }

 private:
  // Fire the session-removed observer for each session, if one is set.
  // Called only with mutex_ released.
  void fireSessionRemoved(const std::vector<SessionPtr>& removed) {
    if (!session_removed_callback_) {
      return;
    }
    for (const auto& session : removed) {
      session_removed_callback_(session);
    }
  }

  // Expiry sweep body shared by the public entry point and the
  // at-capacity path inside createSession / getOrCreateSessionByTransportId,
  // which already hold mutex_ (non-recursive). Appends every removed session
  // to `removed` so the caller can fire observers after releasing the lock.
  void cleanupExpiredSessionsLocked(std::vector<SessionPtr>& removed) {
    std::vector<std::string> expired;

    for (const auto& pair : sessions_) {
      // Transport-keyed sessions (HTTP+SSE) are NOT subject to activity
      // expiry: their lifetime is bounded by the SSE stream closing
      // (removeSessionByTransportId), and the client may legitimately hold
      // an idle-but-open stream far longer than session_timeout. Expiring
      // one here would silently drop a live client's session — and its
      // resource subscriptions — while its stream is still connected.
      if (!pair.second->getTransportSessionId().empty()) {
        continue;
      }
      if (pair.second->isExpired(config_.session_timeout)) {
        expired.push_back(pair.first);
      }
    }

    for (const auto& session_id : expired) {
      auto it = sessions_.find(session_id);
      if (it != sessions_.end()) {
        // Remove from connection map if tracked
        if (it->second->getConnection()) {
          connection_sessions_.erase(it->second->getConnection());
        }
        removed.push_back(it->second);
        sessions_.erase(it);
        stats_.sessions_expired++;
        stats_.sessions_active--;
      }
    }
  }
  // Generate unique session ID
  std::string generateSessionId() {
    static std::atomic<uint64_t> counter{0};
    return "session_" + std::to_string(++counter);
  }

  mutable std::mutex mutex_;
  std::map<std::string, SessionPtr> sessions_;
  std::unordered_map<network::Connection*, SessionPtr> connection_sessions_;
  // Transport session id -> session, for transports whose requests arrive
  // on short-lived connections (HTTP+SSE POST callbacks). Kept consistent
  // with sessions_ by every mutation path above.
  std::map<std::string, SessionPtr> transport_sessions_;
  SessionRemovedCallback session_removed_callback_;
  McpServerConfig config_;
  McpServerStats& stats_;
};

/**
 * Enterprise-grade MCP Server
 *
 * Architecture (production-grade patterns):
 * - Inherits from ApplicationBase for worker thread model
 * - Implements McpProtocolCallbacks for protocol handling
 * - Implements ListenerCallbacks for connection acceptance
 * - Implements ConnectionCallbacks for connection lifecycle
 * - Uses robust listener management infrastructure
 * - Uses filter chain for extensible message processing
 * - Manages sessions with timeout and cleanup
 * - Provides handler registration for requests and notifications
 * - Implements resource, tool, and prompt management
 *
 * Connection Flow (production architecture):
 * 1. TcpListenerImpl accepts socket
 * 2. Listener infrastructure manages lifecycle
 * 3. Filter chain processes messages
 * 4. McpServer handles protocol logic
 */
class McpServer : public application::ApplicationBase,
                  public network::ListenerCallbacks {
 public:
  McpServer(const McpServerConfig& config);
  ~McpServer() override;

  // Server lifecycle
  VoidResult listen(const std::string& address);
  void run() override;  // Run event loop in main thread
  void shutdown() override;
  bool isRunning() const { return server_running_; }

  // Listener configuration-based startup
  VoidResult createListenersFromConfig(
      const std::vector<mcp::config::ListenerConfig>& listeners);
  void startListener(const mcp::config::ListenerConfig& listener_config);

  // Handler registration
  void registerRequestHandler(
      const std::string& method,
      std::function<jsonrpc::Response(const jsonrpc::Request&, SessionContext&)>
          handler);

  /**
   * Register a handler that answers with more than one message.
   *
   * The mode has to be declared rather than discovered, because how a
   * response is framed is settled before the handler runs. Optional means
   * the handler reports progress but is still answerable without it;
   * Required means it will ask the client something and wait, so a client
   * that cannot read a stream is refused rather than served an answer its
   * handler will never finish.
   *
   * The handler reaches its stream through SessionContext::responseStream(),
   * which is set for the length of the dispatch.
   */
  void registerRequestHandler(
      const std::string& method,
      std::function<jsonrpc::Response(const jsonrpc::Request&, SessionContext&)>
          handler,
      StreamingMode streaming);

  /**
   * A handler that answers whenever it can, rather than by returning.
   *
   * Given the stream its answer goes on, and told nothing about when to
   * use it: it may answer before it returns, or minutes later from a
   * callback. Nothing is sent on its behalf, so a handler that never
   * calls sendResponse leaves the client waiting — which is the price of
   * being allowed to wait for something itself.
   *
   * This is what a handler needing to ask the client something is
   * registered as. Every connection this server accepts is on one
   * dispatcher thread, and the client's answer arrives on a request that
   * same thread has to accept, so a handler that blocked waiting for it
   * would be waiting for itself.
   *
   * **Needs a transport that can hold an answer open**, which today means
   * the Streamable HTTP endpoint. One that answers a request with exactly
   * one message has no handle outliving the dispatch, so a request for
   * such a method arriving there is refused with an error rather than
   * accepted and left unanswered — a hung request being the one outcome
   * worse than a refusal.
   */
  using AsyncRequestHandler = std::function<void(
      const jsonrpc::Request&, SessionContext&, const ResponseStreamPtr&)>;

  /**
   * Register one.
   *
   * @param streaming Optional serves a client that cannot read a stream by
   *        answering it plainly; Required refuses such a client outright,
   *        which is right for a handler that will ask it something. None
   *        would leave the handler with nothing to answer on and is
   *        treated as Optional.
   */
  void registerAsyncRequestHandler(
      const std::string& method,
      AsyncRequestHandler handler,
      StreamingMode streaming = StreamingMode::Optional);

  /** What kind of response a request will need, asked before dispatch. */
  StreamingMode streamingFor(const jsonrpc::Request& request) const;

  /**
   * Whether a request is of the era that has no handshake.
   *
   * Read from the request itself, which is the only place it is said:
   * that era settles no version anywhere else, and every earlier one
   * settles it at a handshake this one does not have.
   */
  bool isModernRequest(const jsonrpc::Request& request) const;

  /**
   * Whether this server has any answer for a method at all.
   *
   * Asked by a transport that has to tell "no such method" apart from "no
   * such endpoint" in its status codes. Covers what is built in as well
   * as what has been registered, because a caller cannot see the
   * difference and neither should the answer.
   */
  bool knowsMethod(const std::string& method) const;

  /**
   * Everything a client asked to be told about, and told on.
   *
   * In the newest revision a change notification has nowhere else to go:
   * there is no standalone stream, so a client hears about one only on a
   * subscription it opened. Held here because the subscriptions are this
   * server's, and reached from the dispatcher thread alone.
   */
  ListenRegistry& subscriptions() { return subscriptions_; }

  void registerNotificationHandler(
      const std::string& method,
      std::function<void(const jsonrpc::Notification&, SessionContext&)>
          handler);

  // Update initialize instructions returned to future clients. Useful for
  // gateways that discover backend instructions after server construction.
  void setInstructions(const std::string& instructions);

  // Resource management — register with a read handler for resources/read
  void registerResource(
      const Resource& resource,
      std::function<ReadResourceResult(const std::string&, SessionContext&)>
          handler) {
    resource_manager_->registerResource(resource, handler);
  }

  // Register metadata only (appears in resources/list but has no read handler)
  void registerResource(const Resource& resource) {
    resource_manager_->registerResource(resource);
  }

  void registerResourceTemplate(const ResourceTemplate& template_) {
    resource_manager_->registerResourceTemplate(template_);
  }

  // Push notifications/resources/updated to every session subscribed to
  // the URI. Callable from any thread; delivery happens on the dispatcher
  // thread through each session's own channel (SSE stream, connection, or
  // stdio manager).
  void notifyResourceUpdate(const std::string& uri);

  /**
   * End every subscription this server holds, gracefully.
   *
   * Each gets the response its listen request never had, which is what
   * tells a client the subscription ended rather than its connection
   * dropping. For a server withdrawing them without going away; shutdown
   * does the same on its way out.
   *
   * Safe from any thread.
   */
  void endAllSubscriptions();

  // Tool management
  /**
   * @return False when the definition cannot be served and was refused,
   *         with the reason logged. See ToolRegistry::registerTool.
   */
  bool registerTool(const Tool& tool,
                    std::function<CallToolResult(const std::string&,
                                                 const optional<Metadata>&,
                                                 SessionContext&)> handler) {
    return tool_registry_->registerTool(tool, handler);
  }

  // Prompt management
  void registerPrompt(const Prompt& prompt,
                      std::function<GetPromptResult(const std::string&,
                                                    const optional<Metadata>&,
                                                    SessionContext&)> handler) {
    prompt_registry_->registerPrompt(prompt, handler);
  }

  // Get server statistics
  const McpServerStats& getServerStats() const { return server_stats_; }

  // Send notification to specific session
  VoidResult sendNotification(const std::string& session_id,
                              const jsonrpc::Notification& notification);

  // Send a server-initiated request to a specific session and resolve the
  // returned future when that client sends the matching JSON-RPC response,
  // or with an error response when the deadline passes first. Off-dispatcher
  // callers are posted onto the MCP dispatcher before sending; callers must not
  // wait on the returned future from the dispatcher thread, because the client
  // response path runs there too. HTTP/SSE sessions require an active SSE
  // stream for server push.
  std::future<jsonrpc::Response> sendRequest(
      const std::string& session_id,
      const jsonrpc::Request& request,
      std::chrono::milliseconds timeout = std::chrono::milliseconds(30000));

  bool sessionSupportsElicitation(const std::string& session_id,
                                  const std::string& mode) const;

  // Broadcast notification to all sessions
  void broadcastNotification(const jsonrpc::Notification& notification);

  /**
   * Close the stream a session is holding, without ending the session.
   *
   * The session survives, its replay buffer survives, and anything said
   * while nothing is connected waits for whatever connects next — so a
   * client that comes back naming where it got to is given what it
   * missed. That is the difference between this and ending the session,
   * which throws all of it away.
   *
   * For making a client reconnect: to move it off a server about to go
   * away, or to prove that coming back works.
   *
   * @return False when the session is unknown or was holding no stream.
   *         Delivered asynchronously because the stream may belong to
   *         another thread, and a caller with nothing to do afterwards
   *         may pass nothing.
   */
  void dropSessionStream(const std::string& session_id,
                         std::function<void(bool dropped)> done = nullptr);

  /**
   * Answer a request by saying what is still needed to finish it.
   *
   * The newest revision has servers initiate nothing, so this is how a
   * handler asks: the client is told what to go and get, makes the whole
   * request again with the answers attached, and the handler runs a
   * second time with them in hand. Nothing is remembered between the
   * two — whatever has to survive goes in `request_state`, which comes
   * back through the client and must therefore be treated as something
   * an attacker could have written.
   *
   * Refuses rather than asks when the caller declared it cannot do what
   * is being asked of it: such a question would sit unanswerable, and
   * the caller has no way to say so. The refusal names what it would
   * have had to declare.
   *
   * @return An error when the request is not one that may be answered
   *         this way, or when there is nothing to answer on.
   */
  VoidResult answerWithInput(const ResponseStreamPtr& stream,
                             const jsonrpc::Request& request,
                             SessionContext& session,
                             const protocol::modern::NeedsInput& needed);

  /**
   * Ask the client something on the way to answering it.
   *
   * For the questions a server asks mid-request — sample this, elicit
   * that. The question goes down the stream the request is being answered
   * on, because that is where the client is already listening, and the
   * answer comes back as an ordinary inbound message on whichever
   * connection the client chose. Nothing but the JSON-RPC id connects the
   * two, so the id is registered here before the question goes out.
   *
   * The handler asking must not wait for the answer: every connection
   * this server accepts is on one dispatcher thread, and the answer
   * arrives on a request that same thread has to accept. Ask, return, and
   * answer from the callback.
   *
   * @param stream    Where the question goes and where the eventual answer
   *                  to the original request will go. Must still be open.
   * @param request   Carrying an id no other outstanding question uses.
   * @param on_answer Told exactly once: with the client's response, or
   *                  with an error when the deadline passed first.
   * @param timeout   How long the client has. Zero waits forever, which
   *                  hands a client the power to keep a request open
   *                  indefinitely — so it is not the default.
   * @return An error when the question could not be sent at all, in which
   *         case on_answer is never called.
   */
  VoidResult askClient(
      const ResponseStreamPtr& stream,
      const jsonrpc::Request& request,
      std::function<void(const jsonrpc::Response&)> on_answer,
      std::chrono::milliseconds timeout = std::chrono::milliseconds(30000));

  // Run work on the MCP dispatcher. Deferred request handlers that finish from
  // worker threads use this to write their retained response stream on the
  // stream's owning thread.
  bool postToDispatcher(std::function<void()> callback);

 protected:
  // ApplicationBase overrides
  void initializeWorker(application::WorkerContext& worker) override;
  void setupFilterChain(application::FilterChainBuilder& builder) override;

  // Message handling methods (called through internal callbacks)
  void onRequest(const jsonrpc::Request& request) override;
  void onNotification(const jsonrpc::Notification& notification) override;
  void onResponse(const jsonrpc::Response& response) override;

  // Context-carrying dispatch entry points. The context travels with the
  // message from the filter that parsed it, so session keying and the reply
  // both bind to the message's own origin — never to whichever connection
  // happened to be accepted or announced most recently. The context-free
  // overrides above remain only as a degraded fallback for producers that
  // do not supply origin information.
  void onRequestWithContext(const jsonrpc::Request& request,
                            MessageDispatchContext& context);
  void onNotificationWithContext(const jsonrpc::Notification& notification,
                                 MessageDispatchContext& context);
  void onConnectionEvent(network::ConnectionEvent event);
  void onError(const Error& error) override;

  /**
   * Answers this server is waiting on from clients. A caller that sends a
   * request to a client registers here so the reply reaches it rather than
   * being dropped as unrecognized.
   */
  ClientRequestCorrelator& clientRequests() { return client_requests_; }

  /**
   * Stop tracking a request, by the key onRequestWithContext filed it
   * under. For an answer that arrives after its dispatch returned, where
   * there is no scope left to unwind.
   */
  void forgetPendingRequest(const std::string& key);

  // Request tracking helpers
  bool isRequestCancelled(const RequestId& id) const {
    std::lock_guard<std::mutex> lock(pending_requests_mutex_);
    // Convert RequestId to string key
    std::string key = holds_alternative<std::string>(id)
                          ? get<std::string>(id)
                          : std::to_string(get<int64_t>(id));
    auto it = pending_requests_.find(key);
    return (it != pending_requests_.end() && it->second->cancelled.load());
  }

  // ListenerCallbacks overrides (production pattern)
  // Called when listener accepts a new socket
  void onAccept(network::ConnectionSocketPtr&& socket) override;
  // Called when connection is fully established with filters
  void onNewConnection(network::ConnectionPtr&& connection) override;

 private:
  // Register built-in handlers
  void registerBuiltinHandlers();

  // Resolve the session for the message described by `context`. Prefers
  // the transport session id (durable across the short-lived POST
  // connections of HTTP+SSE) and falls back to the origin connection for
  // transports without one (stdio, plain HTTP). Returns nullptr only when
  // the session limit is reached. Dispatcher thread only.
  SessionManager::SessionPtr getOrCreateSessionFor(
      const MessageDispatchContext& context);

  // Reply path for messages that arrived without origin information (the
  // context-free legacy hooks): no connection to key a session on, replies
  // fall back to the first connected stdio manager. Defined in the .cc.
  class LegacyDispatchContext;

  // The single shared session for context-free dispatches. A null-keyed
  // session is unretrievable by any lookup, so creating one per message
  // would leak sessions until max_sessions starves every transport;
  // reusing one also preserves a legacy client's state across messages.
  // Dispatcher thread only.
  SessionManager::SessionPtr legacy_session_;

  // The context-free dispatch warning fires once per server, not per
  // message: a legacy producer emits it on every single message, which is
  // log flooding, not signal. Dispatcher thread only.
  bool legacy_dispatch_warned_{false};

  // Deliver a notification to one session's client, routed by how the
  // session is keyed: SSE stream (via the registry), owning connection,
  // or the stdio connection manager. Dispatcher thread only.
  VoidResult sendNotificationToSession(
      const SessionManager::SessionPtr& session,
      const jsonrpc::Notification& notification);

  VoidResult sendRequestToSession(const SessionManager::SessionPtr& session,
                                  const jsonrpc::Request& request);

  // Internal method to perform actual listening (called from dispatcher thread)
  void performListen();

  /**
   * Work out which address an HTTP listen URL asks for, and refuse it if
   * binding it would put the endpoint on the network without being told
   * to. Sets bind_address_ on success.
   */
  VoidResult resolveBindAddress(const std::string& url);

  // Built-in request handlers
  jsonrpc::Response handleInitialize(const jsonrpc::Request& request,
                                     SessionContext& session);
  jsonrpc::Response handleDiscover(const jsonrpc::Request& request,
                                   SessionContext& session);
  jsonrpc::Response handlePing(const jsonrpc::Request& request,
                               SessionContext& session);
  jsonrpc::Response handleListResources(const jsonrpc::Request& request,
                                        SessionContext& session);
  jsonrpc::Response handleReadResource(const jsonrpc::Request& request,
                                       SessionContext& session);
  jsonrpc::Response handleSubscribe(const jsonrpc::Request& request,
                                    SessionContext& session);
  jsonrpc::Response handleUnsubscribe(const jsonrpc::Request& request,
                                      SessionContext& session);
  jsonrpc::Response handleListTools(const jsonrpc::Request& request,
                                    SessionContext& session);
  jsonrpc::Response handleCallTool(const jsonrpc::Request& request,
                                   SessionContext& session);
  jsonrpc::Response handleListPrompts(const jsonrpc::Request& request,
                                      SessionContext& session);
  jsonrpc::Response handleGetPrompt(const jsonrpc::Request& request,
                                    SessionContext& session);

  // Background task management using dispatcher timers
  void startBackgroundTasks();
  void stopBackgroundTasks();

 private:
  // Per-connection lifecycle callback adapter.
  //
  // Why: ConnectionCallbacks::onEvent carries no Connection identity. Before
  // this adapter, McpServer registered itself on every connection and then
  // fell back to a global `current_connection_` pointer to figure out which
  // one closed — which is stale whenever multiple connections are active.
  // Each adapter binds (server, connection) at construction so the close
  // path knows exactly which connection is dying.
  //
  // Also DeferredDeletable: the adapter is registered on the connection, so
  // destroying it synchronously while we are inside its own onEvent() would
  // be a use-after-free. The server hands it to dispatcher.deferredDelete()
  // alongside the connection itself.
  class ConnectionLifecycleCallbacks : public network::ConnectionCallbacks,
                                       public event::DeferredDeletable {
   public:
    ConnectionLifecycleCallbacks(McpServer& server,
                                 network::Connection* connection)
        : server_(server), connection_(connection) {}

    void onEvent(network::ConnectionEvent event) override {
      server_.onConnectionLifecycleEvent(connection_, event);
    }
    void onAboveWriteBufferHighWatermark() override {}
    void onBelowWriteBufferLowWatermark() override {}

   private:
    McpServer& server_;
    network::Connection* connection_;
  };

  // Dispatcher-thread only: all entries added/removed inside dispatcher.
  std::unordered_map<network::Connection*,
                     std::unique_ptr<ConnectionLifecycleCallbacks>>
      lifecycle_callbacks_;

  void onConnectionLifecycleEvent(network::Connection* connection,
                                  network::ConnectionEvent event);

  // Internal callbacks class to bridge McpProtocolCallbacks to McpServer
  // Following production pattern: separate callback interface from main class
  class ServerProtocolCallbacks : public McpProtocolCallbacks {
   public:
    explicit ServerProtocolCallbacks(McpServer& server) : server_(server) {}

    void onRequest(const jsonrpc::Request& request) override {
      GOPHER_LOG_DEBUG("ServerProtocolCallbacks::onRequest for method: {}",
                       request.method);
      server_.onRequest(request);
    }

    void onNotification(const jsonrpc::Notification& notification) override {
      server_.onNotification(notification);
    }

    void onRequestWithContext(const jsonrpc::Request& request,
                              MessageDispatchContext& context) override {
      GOPHER_LOG_DEBUG(
          "ServerProtocolCallbacks::onRequestWithContext for method: {}",
          request.method);
      server_.onRequestWithContext(request, context);
    }

    void onNotificationWithContext(const jsonrpc::Notification& notification,
                                   MessageDispatchContext& context) override {
      server_.onNotificationWithContext(notification, context);
    }

    void onResponse(const jsonrpc::Response& response) override {
      server_.onResponse(response);
    }

    void onConnectionEvent(network::ConnectionEvent event) override {
      server_.onConnectionEvent(event);
    }

    void onError(const Error& error) override { server_.onError(error); }

    StreamingMode streamingFor(const jsonrpc::Request& request) const override {
      return server_.streamingFor(request);
    }

    bool knowsMethod(const std::string& method) const override {
      return server_.knowsMethod(method);
    }

   private:
    McpServer& server_;
  };

  McpServerConfig config_;
  mutable std::mutex config_mutex_;
  McpServerStats server_stats_;
  std::unique_ptr<ServerProtocolCallbacks> protocol_callbacks_;
  std::shared_ptr<filter::MetricsFilter> metrics_filter_;
  std::shared_ptr<filter::MetricsFilter::MetricsCallbacks> metrics_callbacks_;
  std::shared_ptr<filter::FilterChainEventHub> enhanced_filter_event_hub_;
  std::shared_ptr<filter::FilterChainCallbacks>
      enhanced_filter_event_callbacks_;
  filter::FilterChainEventHub::ObserverHandle enhanced_filter_event_handle_;

  // Connection management (production pattern)
  // IMPROVEMENT: Using TcpActiveListener for robust listener management
  // Following production architecture for better connection lifecycle handling
  std::vector<std::unique_ptr<network::TcpActiveListener>> tcp_listeners_;

  // Server-side HTTP+SSE filter chain factory (default listener path).
  // Held here — not just dropped into the listener config — because the
  // server needs its SSE session registry outside any request cycle: to
  // push server-initiated notifications through a client's SSE stream and
  // to observe stream teardown so the MCP session keyed on it is released.
  std::shared_ptr<filter::HttpSseFilterChainFactory> http_sse_factory_;

  // Pending listener configurations (for config-driven startup)
  std::vector<mcp::config::ListenerConfig> pending_listener_configs_;

  // Store active connections to manage their lifetime
  // Following production pattern: server owns connections until they close

  // Legacy connection managers (for stdio transport)
  // TODO: Migrate stdio to use listener pattern
  std::vector<std::unique_ptr<McpConnectionManager>> connection_managers_;
  std::atomic<bool> server_running_{false};

  // Session management
  std::unique_ptr<SessionManager> session_manager_;

  // Connection management
  // Following production pattern: listener owns connections, not threads
  // Connections tracked by count only, ownership managed by listener

  // Active connections owned by server
  // Following production pattern: all operations in dispatcher thread, no mutex
  // needed Connections removed when they close via callbacks
  std::list<network::ConnectionPtr> active_connections_;

  // Connection to session mapping
  // Following production pattern: managed by session manager, not thread-local
  std::map<network::Connection*, SessionManager::SessionPtr>
      connection_sessions_;
  std::mutex connection_sessions_mutex_;

  // Request tracking for cancellation support
  struct PendingRequest {
    RequestId id;
    std::string session_id;
    std::chrono::steady_clock::time_point start_time;
    std::atomic<bool> cancelled{false};
  };
  // Use string key for map to avoid variant comparison issues
  std::unordered_map<std::string, std::shared_ptr<PendingRequest>>
      pending_requests_;
  mutable std::mutex pending_requests_mutex_;

  // Answers we are waiting on from clients, for questions this server asked.
  ClientRequestCorrelator client_requests_;

  // Cross-thread posts capture this as a guard token, not as ownership. Reset
  // before teardown so queued callbacks can resolve their public futures
  // without touching a destroyed server.
  std::shared_ptr<bool> alive_{std::make_shared<bool>(true)};

  // A deadline per outstanding question. Held here because a timer that
  // goes out of scope never fires, and dropped when the question is
  // settled either way. Dispatcher thread only, like the questions.
  std::map<RequestIdKey, event::TimerPtr> client_request_deadlines_;

  /**
   * The endpoint's way of asking the tool registry what a tool
   * designates. An adapter rather than the registry itself, so the
   * transport depends on a question rather than on where the answer is
   * kept.
   */
  class ToolDesignations : public protocol::modern::DesignatedParamLookup {
   public:
    explicit ToolDesignations(McpServer& server) : server_(server) {}
    bool paramsForTool(
        const std::string& tool_name,
        std::vector<protocol::modern::DesignatedParam>* out) const override;

   private:
    McpServer& server_;
  };
  ToolDesignations tool_designations_{*this};

  // The subscriptions this server is holding open. Dispatcher-confined,
  // like the streams inside it.
  ListenRegistry subscriptions_;

  // Resource, tool, and prompt management
  std::unique_ptr<ResourceManager> resource_manager_;
  std::unique_ptr<ToolRegistry> tool_registry_;
  std::unique_ptr<PromptRegistry> prompt_registry_;

  // Request and notification handlers
  std::map<std::string,
           std::function<jsonrpc::Response(const jsonrpc::Request&,
                                           SessionContext&)>>
      request_handlers_;
  std::map<std::string,
           std::function<void(const jsonrpc::Notification&, SessionContext&)>>
      notification_handlers_;
  // Handlers that answer after their dispatch has returned. Looked up
  // before the ones above, so a method registered both ways answers the
  // way it was most recently registered rather than twice.
  std::map<std::string, AsyncRequestHandler> async_request_handlers_;
  // Methods whose handler answers with more than one message. Absent means
  // None, so a handler registered without saying anything stays on the
  // unary path.
  std::map<std::string, StreamingMode> streaming_methods_;
  mutable std::mutex handlers_mutex_;

  // Background task state
  std::atomic<bool> background_threads_running_{false};

  // Timers for periodic background tasks. Stored as members so they survive
  // past startBackgroundTasks() returning — otherwise the TimerPtr would drop
  // at end of scope and the callback would never fire.
  event::TimerPtr session_cleanup_timer_;
  event::TimerPtr resource_update_timer_;

  // Deferred listen address
  std::string listen_address_;

  // The address an HTTP listener will actually bind, resolved and checked
  // when listen() was called rather than in the dispatcher thread, where
  // the only way to report a refusal is a log line. Null for transports
  // that do not bind a port.
  network::Address::InstanceConstSharedPtr bind_address_;

  bool need_perform_listen_ = false;
};

/**
 * Factory function for creating MCP server
 */
inline std::unique_ptr<McpServer> createMcpServer(
    const McpServerConfig& config = {}) {
  return std::make_unique<McpServer>(config);
}

}  // namespace server
}  // namespace mcp

#endif  // MCP_SERVER_H
