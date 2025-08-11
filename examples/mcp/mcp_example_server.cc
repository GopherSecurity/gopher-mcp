/**
 * @file mcp_example_server.cc
 * @brief Example usage of enterprise-grade MCP server
 * 
 * Demonstrates:
 * - Multi-transport support
 * - Resource registration and management
 * - Tool registration and execution
 * - Prompt management
 * - Session tracking
 * - Custom request/notification handlers
 * - Metrics collection
 * - Graceful shutdown
 */

#include "mcp/server/mcp_server.h"
#include <iostream>
#include <signal.h>
#include <ctime>

using namespace mcp;
using namespace mcp::server;

// Global server for signal handling
std::unique_ptr<McpServer> g_server;
std::atomic<bool> g_shutdown(false);

void signal_handler(int signal) {
  std::cerr << "\n[INFO] Received signal " << signal << ", shutting down..." << std::endl;
  g_shutdown = true;
  if (g_server) {
    g_server->shutdown();
  }
}

// Example tool implementation
CallToolResult executeSampleTool(const std::string& name,
                                 const optional<Metadata>& arguments,
                                 SessionContext& session) {
  CallToolResult result;
  
  // Simple calculator tool
  if (name == "calculator") {
    if (!arguments.has_value()) {
      result.isError = true;
      result.content.push_back(ExtendedContentBlock(
          TextContent("Missing arguments for calculator")));
      return result;
    }
    
    auto args = arguments.value();
    
    // Extract operation and operands
    auto op_it = args.find("operation");
    auto a_it = args.find("a");
    auto b_it = args.find("b");
    
    if (op_it == args.end() || a_it == args.end() || b_it == args.end()) {
      result.isError = true;
      result.content.push_back(ExtendedContentBlock(
          TextContent("Missing required parameters: operation, a, b")));
      return result;
    }
    
    std::string op = holds_alternative<std::string>(op_it->second) ? 
                     get<std::string>(op_it->second) : "";
    double a = holds_alternative<double>(a_it->second) ? 
               get<double>(a_it->second) : 
               (holds_alternative<long long>(a_it->second) ? 
                static_cast<double>(get<long long>(a_it->second)) : 0.0);
    double b = holds_alternative<double>(b_it->second) ? 
               get<double>(b_it->second) :
               (holds_alternative<long long>(b_it->second) ? 
                static_cast<double>(get<long long>(b_it->second)) : 0.0);
    
    double calc_result = 0;
    if (op == "add") {
      calc_result = a + b;
    } else if (op == "subtract") {
      calc_result = a - b;
    } else if (op == "multiply") {
      calc_result = a * b;
    } else if (op == "divide") {
      if (b != 0) {
        calc_result = a / b;
      } else {
        result.isError = true;
        result.content.push_back(ExtendedContentBlock(
            TextContent("Division by zero")));
        return result;
      }
    } else {
      result.isError = true;
      result.content.push_back(ExtendedContentBlock(
          TextContent("Unknown operation: " + op)));
      return result;
    }
    
    // Return successful result
    result.content.push_back(ExtendedContentBlock(
        TextContent("Result: " + std::to_string(calc_result))));
  }
  
  // System info tool
  else if (name == "system_info") {
    std::stringstream info;
    info << "System Information:\n";
    info << "- Server uptime: " << session.getId() << "\n";
    info << "- Current time: " << std::time(nullptr) << "\n";
    info << "- Session ID: " << session.getId() << "\n";
    
    result.content.push_back(ExtendedContentBlock(
        TextContent(info.str())));
  }
  
  return result;
}

// Example prompt handler
GetPromptResult getSamplePrompt(const std::string& name,
                                const optional<Metadata>& arguments,
                                SessionContext& session) {
  GetPromptResult result;
  
  if (name == "greeting") {
    result.description = make_optional(std::string("A friendly greeting prompt"));
    
    // Add prompt messages
    PromptMessage msg1(enums::Role::USER, TextContent("Hello!"));
    result.messages.push_back(msg1);
    
    PromptMessage msg2(enums::Role::ASSISTANT, 
                      TextContent("Hello! How can I help you today?"));
    result.messages.push_back(msg2);
  }
  else if (name == "code_review") {
    result.description = make_optional(std::string("Code review prompt template"));
    
    std::string code = "// Your code here";
    if (arguments.has_value()) {
      auto args = arguments.value();
      auto code_it = args.find("code");
      if (code_it != args.end() && holds_alternative<std::string>(code_it->second)) {
        code = get<std::string>(code_it->second);
      }
    }
    
    PromptMessage msg(enums::Role::USER, 
                     TextContent("Please review this code:\n" + code));
    result.messages.push_back(msg);
  }
  
  return result;
}

// Setup server with resources, tools, and prompts
void setupServer(McpServer& server) {
  std::cerr << "[SETUP] Configuring server resources and tools..." << std::endl;
  
  // Register sample resources
  {
    Resource config_resource;
    config_resource.uri = "config://server/settings";
    config_resource.name = "Server Configuration";
    config_resource.description = make_optional(std::string(
        "Current server configuration and settings"));
    config_resource.mimeType = make_optional(std::string("application/json"));
    
    server.registerResource(config_resource);
    
    Resource log_resource;
    log_resource.uri = "log://server/events";
    log_resource.name = "Server Event Log";
    log_resource.description = make_optional(std::string(
        "Real-time server event log"));
    log_resource.mimeType = make_optional(std::string("text/plain"));
    
    server.registerResource(log_resource);
  }
  
  // Register resource templates
  {
    ResourceTemplate template1;
    template1.uriTemplate = "file://{path}";
    template1.name = "File Resource";
    template1.description = make_optional(std::string(
        "Access files on the server filesystem"));
    template1.mimeType = make_optional(std::string("text/plain"));
    
    server.registerResourceTemplate(template1);
  }
  
  // Register tools
  {
    Tool calc_tool;
    calc_tool.name = "calculator";
    calc_tool.description = make_optional(std::string(
        "Simple calculator for basic arithmetic operations"));
    
    // Define input schema for calculator
    calc_tool.inputSchema = make_optional(json::JsonValue());
    // TODO: Set proper JSON schema
    
    server.registerTool(calc_tool, executeSampleTool);
    
    Tool info_tool;
    info_tool.name = "system_info";
    info_tool.description = make_optional(std::string(
        "Get system and server information"));
    
    server.registerTool(info_tool, executeSampleTool);
  }
  
  // Register prompts
  {
    Prompt greeting_prompt;
    greeting_prompt.name = "greeting";
    greeting_prompt.description = make_optional(std::string(
        "A friendly greeting interaction"));
    
    server.registerPrompt(greeting_prompt, getSamplePrompt);
    
    Prompt code_prompt;
    code_prompt.name = "code_review";
    code_prompt.description = make_optional(std::string(
        "Template for code review requests"));
    
    // Define prompt arguments
    PromptArgument code_arg;
    code_arg.name = "code";
    code_arg.description = make_optional(std::string("The code to review"));
    code_arg.required = true;
    
    code_prompt.arguments = make_optional(std::vector<PromptArgument>{code_arg});
    
    server.registerPrompt(code_prompt, getSamplePrompt);
  }
  
  // Register custom request handlers
  {
    // Echo handler
    server.registerRequestHandler("echo", 
        [](const jsonrpc::Request& request, SessionContext& session) {
      auto response_data = make<Metadata>()
          .add("echo", true)
          .add("session_id", session.getId())
          .build();
      
      if (request.params.has_value()) {
        // Create new metadata with additional fields
        auto builder = make<Metadata>();
        // Copy existing fields from response_data
        for (const auto& pair : response_data) {
          builder.add(pair.first, pair.second);
        }
        // Add params
        builder.add("params", std::string("params_placeholder"));
        response_data = builder.build();
      }
      
      return jsonrpc::Response::success(request.id, 
          jsonrpc::ResponseResult(response_data));
    });
    
    // Status handler
    server.registerRequestHandler("server/status",
        [&server](const jsonrpc::Request& request, SessionContext& session) {
      const auto& stats = server.getServerStats();
      
      auto status = make<Metadata>()
          .add("running", server.isRunning())
          .add("sessions_active", static_cast<int64_t>(stats.sessions_active.load()))
          .add("requests_total", static_cast<int64_t>(stats.requests_total.load()))
          .add("connections_active", static_cast<int64_t>(stats.connections_active.load()))
          .build();
      
      return jsonrpc::Response::success(request.id,
          jsonrpc::ResponseResult(status));
    });
  }
  
  // Register notification handlers
  {
    // Log notification handler
    server.registerNotificationHandler("log",
        [](const jsonrpc::Notification& notification, SessionContext& session) {
      if (notification.params.has_value()) {
        auto params = notification.params.value();
        auto msg_it = params.find("message");
        if (msg_it != params.end() && holds_alternative<std::string>(msg_it->second)) {
          std::cerr << "[LOG from " << session.getId() << "] " 
                    << get<std::string>(msg_it->second) << std::endl;
        }
      }
    });
    
    // Heartbeat handler
    server.registerNotificationHandler("heartbeat",
        [](const jsonrpc::Notification& notification, SessionContext& session) {
      // Update session activity
      session.updateActivity();
    });
  }
  
  std::cerr << "[SETUP] Server configuration complete" << std::endl;
}

// Print server statistics
void printStatistics(const McpServer& server) {
  const auto& stats = server.getServerStats();
  
  std::cerr << "\n=== Server Statistics ===" << std::endl;
  std::cerr << "Sessions:" << std::endl;
  std::cerr << "  Total: " << stats.sessions_total << std::endl;
  std::cerr << "  Active: " << stats.sessions_active << std::endl;
  std::cerr << "  Expired: " << stats.sessions_expired << std::endl;
  
  std::cerr << "\nConnections:" << std::endl;
  std::cerr << "  Total: " << stats.connections_total << std::endl;
  std::cerr << "  Active: " << stats.connections_active << std::endl;
  
  std::cerr << "\nRequests:" << std::endl;
  std::cerr << "  Total: " << stats.requests_total << std::endl;
  std::cerr << "  Success: " << stats.requests_success << std::endl;
  std::cerr << "  Failed: " << stats.requests_failed << std::endl;
  std::cerr << "  Invalid: " << stats.requests_invalid << std::endl;
  std::cerr << "  Notifications: " << stats.notifications_total << std::endl;
  
  std::cerr << "\nResources:" << std::endl;
  std::cerr << "  Served: " << stats.resources_served << std::endl;
  std::cerr << "  Subscribed: " << stats.resources_subscribed << std::endl;
  std::cerr << "  Updates sent: " << stats.resource_updates_sent << std::endl;
  
  std::cerr << "\nTools:" << std::endl;
  std::cerr << "  Executed: " << stats.tools_executed << std::endl;
  std::cerr << "  Failed: " << stats.tools_failed << std::endl;
  
  std::cerr << "\nPrompts:" << std::endl;
  std::cerr << "  Retrieved: " << stats.prompts_retrieved << std::endl;
  
  std::cerr << "\nData Transfer:" << std::endl;
  std::cerr << "  Bytes sent: " << stats.bytes_sent << std::endl;
  std::cerr << "  Bytes received: " << stats.bytes_received << std::endl;
  
  std::cerr << "\nErrors:" << std::endl;
  std::cerr << "  Total: " << stats.errors_total << std::endl;
}

int main(int argc, char* argv[]) {
  // Install signal handlers
  signal(SIGINT, signal_handler);
  signal(SIGTERM, signal_handler);
  
  std::cerr << "=====================================================" << std::endl;
  std::cerr << "MCP Server Example - Enterprise Features Demo" << std::endl;
  std::cerr << "=====================================================" << std::endl;
  
  // Parse command line arguments
  std::string listen_address = "stdio://";
  if (argc > 1) {
    listen_address = argv[1];
  }
  
  // Configure server with enterprise features
  McpServerConfig config;
  
  // Protocol settings
  config.protocol_version = "2024-11-05";
  config.server_name = "mcp-example-server";
  config.server_version = "1.0.0";
  config.instructions = "Example MCP server demonstrating enterprise features";
  
  // Transport settings
  config.supported_transports = {TransportType::Stdio, TransportType::HttpSse};
  
  // Session management
  config.max_sessions = 100;
  config.session_timeout = std::chrono::minutes(5);
  config.allow_concurrent_sessions = true;
  
  // Request processing
  config.request_queue_size = 500;
  config.request_processing_timeout = std::chrono::seconds(60);
  config.enable_request_validation = true;
  
  // Resource management
  config.enable_resource_subscriptions = true;
  config.max_subscriptions_per_session = 50;
  config.resource_update_debounce = std::chrono::milliseconds(100);
  
  // Worker configuration
  config.num_workers = 4;
  
  // Flow control
  config.buffer_high_watermark = 1024 * 1024;  // 1MB
  config.buffer_low_watermark = 256 * 1024;    // 256KB
  
  // Observability
  config.enable_metrics = true;
  config.metrics_interval = std::chrono::seconds(10);
  
  // Server capabilities
  config.capabilities.tools = make_optional(true);
  config.capabilities.prompts = make_optional(true);
  config.capabilities.logging = make_optional(true);
  
  // Resources capability with subscription support
  ResourcesCapability res_cap;
  res_cap.subscribe = make_optional(EmptyCapability());
  res_cap.listChanged = make_optional(EmptyCapability());
  config.capabilities.resources = make_optional(variant<bool, ResourcesCapability>(res_cap));
  
  // Create server
  std::cerr << "[INFO] Creating MCP server..." << std::endl;
  g_server = createMcpServer(config);
  
  // Setup server resources, tools, and prompts
  setupServer(*g_server);
  
  // Start listening
  std::cerr << "[INFO] Starting server on: " << listen_address << std::endl;
  auto listen_result = g_server->listen(listen_address);
  
  if (is_error<std::nullptr_t>(listen_result)) {
    std::cerr << "[ERROR] Failed to start server: " 
              << get_error<std::nullptr_t>(listen_result)->message << std::endl;
    return 1;
  }
  
  std::cerr << "[INFO] Server started successfully" << std::endl;
  std::cerr << "[INFO] Supported transports: stdio, http+sse" << std::endl;
  std::cerr << "[INFO] Max sessions: " << config.max_sessions << std::endl;
  std::cerr << "[INFO] Worker threads: " << config.num_workers << std::endl;
  std::cerr << "[INFO] Press Ctrl+C to shutdown" << std::endl;
  
  // Give server time to fully initialize
  std::this_thread::sleep_for(std::chrono::seconds(1));
  
  // Main loop - periodic status and resource updates
  int update_count = 0;
  std::cerr << "[DEBUG] Starting main loop, server running: " << g_server->isRunning() << std::endl;
  
  // Keep the main thread alive
  while (!g_shutdown) {
    try {
      std::this_thread::sleep_for(std::chrono::seconds(10));
    
    update_count++;
    
      // Simulate resource update every 30 seconds
      if (update_count % 3 == 0) {
        std::cerr << "[INFO] Update cycle " << update_count << std::endl;
        
        // TODO: Re-enable when server is stable
        // g_server->notifyResourceUpdate("log://server/events");
        // auto status_notif = jsonrpc::make_notification("server/status");
        // g_server->broadcastNotification(status_notif);
      }
      
      // Print brief status
      std::cerr << "[STATUS] Running for " << (update_count * 10) << " seconds" << std::endl;
    } catch (const std::exception& e) {
      std::cerr << "[ERROR] Exception in main loop: " << e.what() << std::endl;
    }
  }
  
  // Graceful shutdown
  std::cerr << "\n[INFO] Shutting down server..." << std::endl;
  
  // Send shutdown notification to all clients
  auto shutdown_notif = jsonrpc::make_notification("server/shutdown");
  g_server->broadcastNotification(shutdown_notif);
  
  // Give clients time to disconnect
  std::this_thread::sleep_for(std::chrono::seconds(1));
  
  // Shutdown server
  g_server->shutdown();
  
  // Print final statistics
  printStatistics(*g_server);
  
  std::cerr << "\n[INFO] Server shutdown complete" << std::endl;
  
  return 0;
}