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
#include "mcp/event/event_loop.h"
#include "mcp/mcp_application_base.h"
#include "mcp/mcp_connection_manager.h"
#include "mcp/network/filter.h"
#include "mcp/types.h"

namespace mcp {
namespace server {

// Forward declarations
class RequestHandler;
class ResourceManager;
class ToolRegistry;
class PromptRegistry;
class SessionManager;

/**
 * Server configuration
 */
struct McpServerConfig : public application::ApplicationBase::Config {
  // Protocol configuration
  std::string protocol_version = "2024-11-05";
  std::string server_name = "mcp-cpp-server";
  std::string server_version = "1.0.0";
  std::string instructions;  // Optional server instructions

  // Transport configuration
  std::vector<TransportType> supported_transports = {TransportType::Stdio,
                                                     TransportType::HttpSse};

  // Session management
  size_t max_sessions = 100;
  std::chrono::milliseconds session_timeout{300000};  // 5 minutes
  bool allow_concurrent_sessions = true;

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
  network::Connection* connection_;  // Store raw pointer
  std::chrono::steady_clock::time_point created_time_;
  std::chrono::steady_clock::time_point last_activity_;
  optional<Implementation> client_info_;

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
  ResourceManager(McpServerStats& stats) : stats_(stats) {}

  // Register a resource
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

  // Read resource content
  ReadResourceResult readResource(const std::string& uri) {
    std::lock_guard<std::mutex> lock(mutex_);
    ReadResourceResult result;

    auto it = resources_.find(uri);
    if (it != resources_.end()) {
      // Create text content for the resource
      TextResourceContents content;
      content.uri = uri;
      content.mimeType = it->second.mimeType;
      content.text = "Resource content for: " +
                     uri;  // Actual implementation would read real content

      result.contents.push_back(content);
      stats_.resources_served++;
    }

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

  // Notify subscribers of resource update
  void notifyResourceUpdate(const std::string& uri) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = subscriptions_.find(uri);
    if (it != subscriptions_.end()) {
      // Queue update notifications for subscribers
      for (const auto& session_id : it->second) {
        pending_updates_[session_id].insert(uri);
      }
      stats_.resource_updates_sent++;
    }
  }

  // Get pending updates for session
  std::set<std::string> getPendingUpdates(const std::string& session_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = pending_updates_.find(session_id);
    if (it != pending_updates_.end()) {
      auto updates = it->second;
      pending_updates_.erase(it);
      return updates;
    }
    return {};
  }

 private:
  mutable std::mutex mutex_;
  std::map<std::string, Resource> resources_;
  std::vector<ResourceTemplate> resource_templates_;
  std::map<std::string, std::set<std::string>>
      subscriptions_;  // uri -> session_ids
  std::map<std::string, std::set<std::string>>
      pending_updates_;  // session_id -> uris
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
  void registerTool(const Tool& tool, ToolHandler handler) {
    std::lock_guard<std::mutex> lock(mutex_);
    tools_[tool.name] = tool;
    tool_handlers_[tool.name] = handler;
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
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = tool_handlers_.find(name);
    if (it != tool_handlers_.end()) {
      try {
        auto result = it->second(name, arguments, session);
        stats_.tools_executed++;
        return result;
      } catch (const std::exception& e) {
        stats_.tools_failed++;
        CallToolResult error_result;
        error_result.isError = true;
        error_result.content.push_back(ExtendedContentBlock(
            TextContent("Tool execution failed: " + std::string(e.what()))));
        return error_result;
      }
    }

    // Tool not found
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

  // Create new session
  SessionPtr createSession(network::Connection* connection) {
    std::lock_guard<std::mutex> lock(mutex_);

    // Check max sessions limit
    if (sessions_.size() >= config_.max_sessions) {
      // Try to clean up expired sessions first
      cleanupExpiredSessions();
      if (sessions_.size() >= config_.max_sessions) {
        return nullptr;  // Max sessions reached
      }
    }

    // Generate session ID
    std::string session_id = generateSessionId();

    // Create session
    auto session = std::make_shared<SessionContext>(session_id, connection);
    sessions_[session_id] = session;

    stats_.sessions_total++;
    stats_.sessions_active++;

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

  // Get session by connection
  SessionPtr getSessionByConnection(network::Connection* connection) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& pair : sessions_) {
      if (pair.second->getConnection() == connection) {
        pair.second->updateActivity();
        return pair.second;
      }
    }
    return nullptr;
  }

  // Remove session
  void removeSession(const std::string& session_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(session_id);
    if (it != sessions_.end()) {
      sessions_.erase(it);
      stats_.sessions_active--;
    }
  }

  // Clean up expired sessions
  void cleanupExpiredSessions() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> expired;

    for (const auto& pair : sessions_) {
      if (pair.second->isExpired(config_.session_timeout)) {
        expired.push_back(pair.first);
      }
    }

    for (const auto& session_id : expired) {
      sessions_.erase(session_id);
      stats_.sessions_expired++;
      stats_.sessions_active--;
    }
  }

 private:
  // Generate unique session ID
  std::string generateSessionId() {
    static std::atomic<uint64_t> counter{0};
    return "session_" + std::to_string(++counter);
  }

  mutable std::mutex mutex_;
  std::map<std::string, SessionPtr> sessions_;
  McpServerConfig config_;
  McpServerStats& stats_;
};

/**
 * Enterprise-grade MCP Server
 *
 * Architecture:
 * - Inherits from ApplicationBase for worker thread model
 * - Implements McpMessageCallbacks for protocol handling
 * - Uses filter chain for extensible message processing
 * - Manages sessions with timeout and cleanup
 * - Provides handler registration for requests and notifications
 * - Implements resource, tool, and prompt management
 */
class McpServer : public application::ApplicationBase,
                  public McpMessageCallbacks {
 public:
  McpServer(const McpServerConfig& config);
  ~McpServer() override;

  // Server lifecycle
  VoidResult listen(const std::string& address);
  void run();  // Run event loop in main thread (Envoy pattern)
  void shutdown();
  bool isRunning() const { return server_running_; }

  // Handler registration
  void registerRequestHandler(
      const std::string& method,
      std::function<jsonrpc::Response(const jsonrpc::Request&, SessionContext&)>
          handler);

  void registerNotificationHandler(
      const std::string& method,
      std::function<void(const jsonrpc::Notification&, SessionContext&)>
          handler);

  // Resource management
  void registerResource(const Resource& resource) {
    resource_manager_->registerResource(resource);
  }

  void registerResourceTemplate(const ResourceTemplate& template_) {
    resource_manager_->registerResourceTemplate(template_);
  }

  void notifyResourceUpdate(const std::string& uri) {
    resource_manager_->notifyResourceUpdate(uri);
  }

  // Tool management
  void registerTool(const Tool& tool,
                    std::function<CallToolResult(const std::string&,
                                                 const optional<Metadata>&,
                                                 SessionContext&)> handler) {
    tool_registry_->registerTool(tool, handler);
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

  // Broadcast notification to all sessions
  void broadcastNotification(const jsonrpc::Notification& notification);

 protected:
  // ApplicationBase overrides
  void initializeWorker(application::WorkerContext& worker) override;
  void setupFilterChain(application::FilterChainBuilder& builder) override;

  // McpMessageCallbacks overrides
  void onRequest(const jsonrpc::Request& request) override;
  void onNotification(const jsonrpc::Notification& notification) override;
  void onResponse(const jsonrpc::Response& response) override;
  void onConnectionEvent(network::ConnectionEvent event) override;
  void onError(const Error& error) override;

 private:
  // Register built-in handlers
  void registerBuiltinHandlers();
  
  // Internal method to perform actual listening (called from dispatcher thread)
  void performListen();

  // Built-in request handlers
  jsonrpc::Response handleInitialize(const jsonrpc::Request& request,
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
  McpServerConfig config_;
  McpServerStats server_stats_;

  // Connection management
  std::vector<std::unique_ptr<McpConnectionManager>> connection_managers_;
  std::atomic<bool> server_running_{false};

  // Session management
  std::unique_ptr<SessionManager> session_manager_;

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
  std::mutex handlers_mutex_;

  // Background task state
  std::atomic<bool> background_threads_running_{false};
  
  // Deferred listen address (for Envoy pattern)
  std::string listen_address_;
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