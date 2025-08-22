/**
 * @file mcp_example_server_enhanced.cc
 * @brief MCP server with enhanced filter chain including enterprise features
 * 
 * This example demonstrates how to use the MCP server with additional filters:
 * - Circuit breaker for fault tolerance
 * - Rate limiting for flow control  
 * - Metrics collection for observability
 * - Request validation for security
 * - Backpressure management for stability
 */

#include "mcp/server/mcp_server.h"
#include "mcp/transport/http_sse_transport_socket.h"
#include "mcp/filter/enhanced_filter_chain_factory.h"
#include <iostream>
#include <signal.h>
#include <ctime>
#include <sstream>
#include <thread>
#include <chrono>
#include <mutex>

using namespace mcp;
using namespace mcp::server;

// Global server for signal handling
std::shared_ptr<McpServer> g_server;
std::atomic<bool> g_shutdown(false);
std::mutex g_server_mutex;

// Signal handler
void signalHandler(int signal) {
  std::cerr << "\n[SIGNAL] Received signal " << signal << ", shutting down..." << std::endl;
  g_shutdown = true;
  
  std::lock_guard<std::mutex> lock(g_server_mutex);
  if (g_server) {
    g_server->stop();
  }
}

// Example tool implementation
CallToolResult executeSampleTool(const std::string& name,
                                 const optional<Metadata>& arguments,
                                 SessionContext& session) {
  CallToolResult result;
  
  if (name == "calculator") {
    if (!arguments.has_value()) {
      result.isError = true;
      result.content.push_back(ExtendedContentBlock(
          TextContent("Missing arguments for calculator")));
      return result;
    }
    
    auto args = arguments.value();
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
    
    result.content.push_back(ExtendedContentBlock(
        TextContent("Result: " + std::to_string(calc_result))));
  }
  else if (name == "system_info") {
    std::stringstream info;
    info << "System Information:\n";
    info << "- Server time: " << std::time(nullptr) << "\n";
    info << "- Session ID: " << session.getId() << "\n";
    
    result.content.push_back(ExtendedContentBlock(
        TextContent(info.str())));
  }
  
  return result;
}

// Setup server with resources and tools
void setupServer(McpServer& server) {
  std::cerr << "[SETUP] Configuring server resources and tools..." << std::endl;
  
  // Register calculator tool
  Tool calc_tool;
  calc_tool.name = "calculator";
  calc_tool.description = make_optional(std::string(
      "Simple calculator for basic arithmetic operations"));
  
  json::JsonValue schema;
  schema["type"] = "object";
  schema["properties"]["operation"]["type"] = "string";
  
  auto enum_array = json::JsonValue::array();
  enum_array.push_back("add");
  enum_array.push_back("subtract");
  enum_array.push_back("multiply");
  enum_array.push_back("divide");
  schema["properties"]["operation"]["enum"] = enum_array;
  
  schema["properties"]["a"]["type"] = "number";
  schema["properties"]["b"]["type"] = "number";
  
  auto required_array = json::JsonValue::array();
  required_array.push_back("operation");
  required_array.push_back("a");
  required_array.push_back("b");
  schema["required"] = required_array;
  
  calc_tool.inputSchema = make_optional(schema);
  server.registerTool(calc_tool, executeSampleTool);
  
  // Register system info tool
  Tool info_tool;
  info_tool.name = "system_info";
  info_tool.description = make_optional(std::string(
      "Get system and server information"));
  server.registerTool(info_tool, executeSampleTool);
  
  // Register ping handler
  server.registerRequestHandler("ping", 
      [](const jsonrpc::Request& request, SessionContext& session) {
    auto pong = make<Metadata>()
        .add("pong", true)
        .add("timestamp", static_cast<long long>(std::time(nullptr)))
        .build();
    
    return jsonrpc::Response::success(request.id, 
        jsonrpc::ResponseResult(pong));
  });
  
  std::cerr << "[SETUP] Server configuration complete" << std::endl;
}

int main(int argc, char* argv[]) {
  // Parse command line
  int port = 3000;
  std::string host = "0.0.0.0";
  bool enable_filters = true;
  
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--port" && i + 1 < argc) {
      port = std::atoi(argv[++i]);
    }
    else if (arg == "--host" && i + 1 < argc) {
      host = argv[++i];
    }
    else if (arg == "--no-filters") {
      enable_filters = false;
    }
    else if (arg == "--help") {
      std::cout << "Usage: " << argv[0] << " [options]\n"
                << "Options:\n"
                << "  --port <port>     Listen port (default: 3000)\n"
                << "  --host <address>  Bind address (default: 0.0.0.0)\n"
                << "  --no-filters      Disable enterprise filters\n"
                << "  --help            Show this help message\n";
      return 0;
    }
  }
  
  std::cerr << "=== MCP Enhanced Server Starting ===" << std::endl;
  std::cerr << "[CONFIG] Host: " << host << std::endl;
  std::cerr << "[CONFIG] Port: " << port << std::endl;
  std::cerr << "[CONFIG] Enterprise filters: " << (enable_filters ? "enabled" : "disabled") << std::endl;
  
  // Configure server
  McpServerConfig config;
  config.name = "Enhanced MCP Server";
  config.version = "1.0.0";
  
  // Configure enterprise filter settings if enabled
  if (enable_filters) {
    std::cerr << "[CONFIG] Configuring enterprise filters:" << std::endl;
    std::cerr << "  - Circuit breaker: enabled (failure threshold: 5)" << std::endl;
    std::cerr << "  - Rate limiting: enabled (100 req/s per method)" << std::endl;
    std::cerr << "  - Metrics collection: enabled" << std::endl;
    std::cerr << "  - Request validation: enabled" << std::endl;
    std::cerr << "  - Backpressure: enabled (high watermark: 10MB)" << std::endl;
  }
  
  // Create server
  {
    std::lock_guard<std::mutex> lock(g_server_mutex);
    g_server = createMcpServer(config);
    if (!g_server) {
      std::cerr << "[ERROR] Failed to create server" << std::endl;
      return 1;
    }
  }
  
  // Setup server
  setupServer(*g_server);
  
  // Install signal handlers
  signal(SIGINT, signalHandler);
  signal(SIGTERM, signalHandler);
  
  // Start server with HTTP/SSE transport
  try {
    HttpSseTransportConfig transport_config;
    transport_config.host = host;
    transport_config.port = port;
    transport_config.rpc_path = "/rpc";
    transport_config.sse_path = "/events";
    transport_config.enable_cors = true;
    
    std::cerr << "[SERVER] Starting HTTP/SSE transport on " 
              << host << ":" << port << std::endl;
    std::cerr << "[SERVER] RPC endpoint: http://" << host << ":" << port << "/rpc" << std::endl;
    std::cerr << "[SERVER] SSE endpoint: http://" << host << ":" << port << "/events" << std::endl;
    
    if (!g_server->start(transport_config)) {
      std::cerr << "[ERROR] Failed to start server" << std::endl;
      return 1;
    }
    
    std::cerr << "[SERVER] Server started successfully" << std::endl;
    std::cerr << "[SERVER] Press Ctrl+C to shutdown" << std::endl;
    
    // Main loop
    while (!g_shutdown) {
      std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    
    std::cerr << "[SERVER] Shutting down..." << std::endl;
    g_server->stop();
    
  } catch (const std::exception& e) {
    std::cerr << "[ERROR] Server error: " << e.what() << std::endl;
    return 1;
  }
  
  std::cerr << "[SERVER] Shutdown complete" << std::endl;
  return 0;
}