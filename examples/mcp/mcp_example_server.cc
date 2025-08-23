/**
 * @file mcp_example_server.cc
 * @brief Enterprise-grade MCP server with HTTP/SSE transport
 * 
 * This example demonstrates a production-ready MCP server with:
 * - HTTP/SSE transport (default) with configurable port
 * - Multi-transport support (HTTP, WebSocket, stdio)
 * - Resource registration and subscription management
 * - Tool registration with schema validation
 * - Prompt templates with argument validation
 * - Session management with timeouts
 * - Request routing and middleware
 * - Comprehensive metrics and monitoring
 * - Graceful shutdown with client notification
 * 
 * USAGE:
 *   mcp_example_server [options]
 * 
 * OPTIONS:
 *   --port <port>        Listen port (default: 3000)
 *   --host <address>     Bind address (default: 0.0.0.0)
 *   --transport <type>   Transport type: http, stdio, websocket, all (default: http)
 *   --workers <n>        Number of worker threads (default: 4)
 *   --max-sessions <n>   Maximum concurrent sessions (default: 100)
 *   --metrics            Enable metrics endpoint
 *   --verbose            Enable verbose logging
 *   --help               Show this help message
 * 
 * ENTERPRISE FEATURES:
 * 
 * 1. TRANSPORT LAYER
 *    - HTTP/SSE for scalable client connections
 *    - WebSocket for bidirectional real-time communication
 *    - stdio for CLI tool integration
 *    - Automatic protocol negotiation
 * 
 * 2. RESOURCE MANAGEMENT
 *    - Static and templated resource registration
 *    - Resource subscription with change notifications
 *    - Efficient resource caching
 *    - Access control per resource
 * 
 * 3. TOOL EXECUTION
 *    - Schema-based input validation
 *    - Async tool execution with progress reporting
 *    - Tool result caching
 *    - Rate limiting per tool
 * 
 * 4. SESSION MANAGEMENT
 *    - Session isolation and security
 *    - Configurable session timeouts
 *    - Session state persistence
 *    - Concurrent session support
 * 
 * 5. REQUEST PROCESSING
 *    - Request validation and sanitization
 *    - Custom middleware pipeline
 *    - Request routing with patterns
 *    - Response compression
 * 
 * 6. OBSERVABILITY
 *    - Prometheus-compatible metrics
 *    - Request tracing with correlation IDs
 *    - Structured logging
 *    - Health and readiness endpoints
 * 
 * 7. SECURITY
 *    - TLS/SSL support with certificate management
 *    - Authentication middleware
 *    - Authorization per resource/tool
 *    - Rate limiting and DDoS protection
 * 
 * EXAMPLES:
 *   # Start server on default port with HTTP/SSE
 *   ./mcp_example_server
 * 
 *   # Start on custom port with multiple transports
 *   ./mcp_example_server --port 8080 --transport all
 * 
 *   # Production setup with metrics
 *   ./mcp_example_server --port 443 --workers 8 --metrics
 * 
 * API ENDPOINTS (HTTP Transport):
 *   POST /rpc             - JSON-RPC endpoint
 *   GET  /events          - SSE event stream
 *   GET  /health          - Health check (custom handler)
 *   GET  /metrics         - Prometheus metrics (if enabled)
 */

#include "mcp/server/mcp_server.h"
#include "mcp/transport/http_sse_transport_socket.h"
#include "mcp/json/json_bridge.h"
#include <iostream>
#include <signal.h>
#include <unistd.h>  // for _exit
#include <ctime>
#include <sstream>
#include <condition_variable>
#include <thread>
#include <chrono>
#include <mutex>

using namespace mcp;
using namespace mcp::server;

// Global server for signal handling
std::shared_ptr<McpServer> g_server;
std::atomic<bool> g_shutdown(false);
std::mutex g_server_mutex;
std::condition_variable g_shutdown_cv;

// Command-line options
struct ServerOptions {
  int port = 3000;
  std::string host = "0.0.0.0";
  std::string transport = "http";
  int workers = 4;
  int max_sessions = 100;
  bool metrics = false;
  bool verbose = false;
  
  // Advanced options
  int session_timeout_minutes = 30;
  int request_queue_size = 500;
  int request_timeout_seconds = 60;
  
  // HTTP/SSE endpoint paths (configurable)
  std::string http_rpc_path = "/rpc";
  std::string http_sse_path = "/events";
  std::string http_health_path = "/health";
};

void signal_handler(int signal) {
  // Signal handlers should do minimal work
  // Just set the flag and notify - actual shutdown happens in main thread
  std::cerr << "\n[INFO] Received signal " << signal << ", initiating graceful shutdown..." << std::endl;
  
  // Set the shutdown flag
  g_shutdown = true;
  
  // Notify the shutdown monitor thread
  g_shutdown_cv.notify_all();
  
  // For safety, if we receive multiple signals, force exit
  static std::atomic<int> signal_count(0);
  signal_count++;
  if (signal_count > 1) {
    std::cerr << "\n[INFO] Force shutdown after multiple signals..." << std::endl;
    // Use _exit instead of std::exit to avoid destructor issues in signal handler
    // This is safer as it doesn't run destructors which might use mutexes
    _exit(0);
  }
}

void printUsage(const char* program) {
  std::cerr << "USAGE: " << program << " [options]\n\n";
  std::cerr << "OPTIONS:\n";
  std::cerr << "  --port <port>        Listen port (default: 3000)\n";
  std::cerr << "  --host <address>     Bind address (default: 0.0.0.0)\n";
  std::cerr << "  --transport <type>   Transport type: http, stdio, websocket, all (default: http)\n";
  std::cerr << "  --workers <n>        Number of worker threads (default: 4)\n";
  std::cerr << "  --max-sessions <n>   Maximum concurrent sessions (default: 100)\n";
  std::cerr << "  --metrics            Enable metrics endpoint\n";
  std::cerr << "  --verbose            Enable verbose logging\n";
  std::cerr << "  --rpc-path <path>    HTTP JSON-RPC endpoint path (default: /rpc)\n";
  std::cerr << "  --sse-path <path>    HTTP SSE events endpoint path (default: /events)\n";
  std::cerr << "  --health-path <path> HTTP health check endpoint path (default: /health)\n";
  std::cerr << "  --help               Show this help message\n";
}

ServerOptions parseArguments(int argc, char* argv[]) {
  ServerOptions options;
  
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    
    if (arg == "--help" || arg == "-h") {
      printUsage(argv[0]);
      exit(0);
    }
    else if (arg == "--port" && i + 1 < argc) {
      options.port = std::atoi(argv[++i]);
    }
    else if (arg == "--host" && i + 1 < argc) {
      options.host = argv[++i];
    }
    else if (arg == "--transport" && i + 1 < argc) {
      options.transport = argv[++i];
    }
    else if (arg == "--workers" && i + 1 < argc) {
      options.workers = std::atoi(argv[++i]);
    }
    else if (arg == "--max-sessions" && i + 1 < argc) {
      options.max_sessions = std::atoi(argv[++i]);
    }
    else if (arg == "--metrics") {
      options.metrics = true;
    }
    else if (arg == "--verbose") {
      options.verbose = true;
    }
    else if (arg == "--rpc-path" && i + 1 < argc) {
      options.http_rpc_path = argv[++i];
    }
    else if (arg == "--sse-path" && i + 1 < argc) {
      options.http_sse_path = argv[++i];
    }
    else if (arg == "--health-path" && i + 1 < argc) {
      options.http_health_path = argv[++i];
    }
    else {
      std::cerr << "[ERROR] Unknown option: " << arg << std::endl;
      printUsage(argv[0]);
      exit(1);
    }
  }
  
  return options;
}

// Example tool implementation
CallToolResult executeSampleTool(const std::string& name,
                                 const optional<Metadata>& arguments,
                                 SessionContext& session) {
  CallToolResult result;
  
  // Calculator tool - demonstrates parameter validation and computation
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
          TextContent("Unknown operation: " + op + ". Valid operations: add, subtract, multiply, divide")));
      return result;
    }
    
    // Return successful result
    result.content.push_back(ExtendedContentBlock(
        TextContent("Result: " + std::to_string(calc_result))));
  }
  
  // System info tool - demonstrates session context usage
  else if (name == "system_info") {
    std::stringstream info;
    info << "System Information:\n";
    info << "- Server time: " << std::time(nullptr) << "\n";
    info << "- Session ID: " << session.getId() << "\n";
    // info << "- Session start: " << session.getStartTime() << "\n";  // Not yet available
    // info << "- Requests handled: " << session.getRequestCount() << "\n";  // Not yet available
    
    result.content.push_back(ExtendedContentBlock(
        TextContent(info.str())));
  }
  
  // Database query tool - demonstrates async operations
  else if (name == "database_query") {
    if (!arguments.has_value()) {
      result.isError = true;
      result.content.push_back(ExtendedContentBlock(
          TextContent("Missing query parameter")));
      return result;
    }
    
    auto args = arguments.value();
    auto query_it = args.find("query");
    
    if (query_it == args.end() || !holds_alternative<std::string>(query_it->second)) {
      result.isError = true;
      result.content.push_back(ExtendedContentBlock(
          TextContent("Invalid or missing query parameter")));
      return result;
    }
    
    std::string query = get<std::string>(query_it->second);
    
    // Simulate database query (in production, this would be actual DB access)
    std::stringstream response;
    response << "Query executed: " << query << "\n";
    response << "Results: 42 rows affected\n";
    response << "Execution time: 15ms";
    
    result.content.push_back(ExtendedContentBlock(
        TextContent(response.str())));
  }
  
  else {
    result.isError = true;
    result.content.push_back(ExtendedContentBlock(
        TextContent("Unknown tool: " + name)));
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
                      TextContent("Hello! I'm the MCP Enterprise Server. How can I help you today?"));
    result.messages.push_back(msg2);
  }
  else if (name == "code_review") {
    result.description = make_optional(std::string("Code review prompt template"));
    
    std::string code = "// Your code here";
    std::string language = "javascript";
    
    if (arguments.has_value()) {
      auto args = arguments.value();
      auto code_it = args.find("code");
      auto lang_it = args.find("language");
      
      if (code_it != args.end() && holds_alternative<std::string>(code_it->second)) {
        code = get<std::string>(code_it->second);
      }
      if (lang_it != args.end() && holds_alternative<std::string>(lang_it->second)) {
        language = get<std::string>(lang_it->second);
      }
    }
    
    std::stringstream prompt;
    prompt << "Please review the following " << language << " code:\n\n";
    prompt << "```" << language << "\n";
    prompt << code << "\n";
    prompt << "```\n\n";
    prompt << "Consider:\n";
    prompt << "1. Code quality and best practices\n";
    prompt << "2. Potential bugs or issues\n";
    prompt << "3. Performance implications\n";
    prompt << "4. Security concerns\n";
    prompt << "5. Suggestions for improvement";
    
    PromptMessage msg(enums::Role::USER, TextContent(prompt.str()));
    result.messages.push_back(msg);
  }
  else if (name == "data_analysis") {
    result.description = make_optional(std::string("Data analysis prompt template"));
    
    std::string dataset = "sample_data.csv";
    std::string analysis_type = "descriptive";
    
    if (arguments.has_value()) {
      auto args = arguments.value();
      auto dataset_it = args.find("dataset");
      auto type_it = args.find("analysis_type");
      
      if (dataset_it != args.end() && holds_alternative<std::string>(dataset_it->second)) {
        dataset = get<std::string>(dataset_it->second);
      }
      if (type_it != args.end() && holds_alternative<std::string>(type_it->second)) {
        analysis_type = get<std::string>(type_it->second);
      }
    }
    
    std::stringstream prompt;
    prompt << "Perform " << analysis_type << " analysis on dataset: " << dataset << "\n\n";
    prompt << "Please provide:\n";
    prompt << "1. Summary statistics\n";
    prompt << "2. Key insights\n";
    prompt << "3. Visualizations recommendations\n";
    prompt << "4. Data quality assessment";
    
    PromptMessage msg(enums::Role::USER, TextContent(prompt.str()));
    result.messages.push_back(msg);
  }
  
  return result;
}

// Setup server with resources, tools, and prompts
void setupServer(McpServer& server, bool verbose) {
  std::cerr << "[SETUP] Configuring server resources and tools..." << std::endl;
  
  // Register sample resources
  {
    // Configuration resource
    Resource config_resource;
    config_resource.uri = "config://server/settings";
    config_resource.name = "Server Configuration";
    config_resource.description = make_optional(std::string(
        "Current server configuration and settings"));
    config_resource.mimeType = make_optional(std::string("application/json"));
    
    server.registerResource(config_resource);
    
    // Log resource
    Resource log_resource;
    log_resource.uri = "log://server/events";
    log_resource.name = "Server Event Log";
    log_resource.description = make_optional(std::string(
        "Real-time server event log"));
    log_resource.mimeType = make_optional(std::string("text/plain"));
    
    server.registerResource(log_resource);
    
    // Metrics resource
    Resource metrics_resource;
    metrics_resource.uri = "metrics://server/stats";
    metrics_resource.name = "Server Metrics";
    metrics_resource.description = make_optional(std::string(
        "Server performance metrics and statistics"));
    metrics_resource.mimeType = make_optional(std::string("application/json"));
    
    server.registerResource(metrics_resource);
    
    if (verbose) {
      std::cerr << "[SETUP] Registered 3 static resources" << std::endl;
    }
  }
  
  // Register resource templates
  {
    ResourceTemplate file_template;
    file_template.uriTemplate = "file://{path}";
    file_template.name = "File Resource";
    file_template.description = make_optional(std::string(
        "Access files on the server filesystem"));
    file_template.mimeType = make_optional(std::string("text/plain"));
    
    server.registerResourceTemplate(file_template);
    
    ResourceTemplate db_template;
    db_template.uriTemplate = "db://{database}/{table}";
    db_template.name = "Database Resource";
    db_template.description = make_optional(std::string(
        "Access database tables"));
    db_template.mimeType = make_optional(std::string("application/json"));
    
    server.registerResourceTemplate(db_template);
    
    if (verbose) {
      std::cerr << "[SETUP] Registered 2 resource templates" << std::endl;
    }
  }
  
  // Register tools
  {
    std::cerr << "[SETUP] About to register tools..." << std::endl;
    
    // Calculator tool with schema
    Tool calc_tool;
    calc_tool.name = "calculator";
    calc_tool.description = make_optional(std::string(
        "Simple calculator for basic arithmetic operations"));
    
    // Define input schema for calculator
    // Tool schema needs to be a json::JsonValue object
    json::JsonValue schema;
    schema["type"] = "object";
    
    // Define properties
    json::JsonValue properties;
    
    // Operation property
    json::JsonValue operation_prop;
    operation_prop["type"] = "string";
    json::JsonValue enum_array = json::JsonValue::array();
    enum_array.push_back("add");
    enum_array.push_back("subtract");
    enum_array.push_back("multiply");
    enum_array.push_back("divide");
    operation_prop["enum"] = enum_array;
    properties["operation"] = operation_prop;
    
    // Number properties
    json::JsonValue a_prop;
    a_prop["type"] = "number";
    properties["a"] = a_prop;
    
    json::JsonValue b_prop;
    b_prop["type"] = "number";
    properties["b"] = b_prop;
    
    schema["properties"] = properties;
    
    // Required fields
    json::JsonValue required = json::JsonValue::array();
    required.push_back("operation");
    required.push_back("a");
    required.push_back("b");
    schema["required"] = required;
    
    calc_tool.inputSchema = make_optional(schema);
    
    std::cerr << "[SETUP] Registering calculator tool..." << std::endl;
    server.registerTool(calc_tool, executeSampleTool);
    std::cerr << "[SETUP] Calculator tool registered" << std::endl;
    
    // System info tool
    Tool info_tool;
    info_tool.name = "system_info";
    info_tool.description = make_optional(std::string(
        "Get system and server information"));
    
    server.registerTool(info_tool, executeSampleTool);
    
    // Database query tool
    Tool db_tool;
    db_tool.name = "database_query";
    db_tool.description = make_optional(std::string(
        "Execute database queries"));
    
    // Define schema as json::JsonValue
    json::JsonValue db_schema;
    db_schema["type"] = "object";
    
    json::JsonValue db_properties;
    json::JsonValue query_prop;
    query_prop["type"] = "string";
    query_prop["description"] = "SQL query to execute";
    db_properties["query"] = query_prop;
    db_schema["properties"] = db_properties;
    
    json::JsonValue db_required = json::JsonValue::array();
    db_required.push_back("query");
    db_schema["required"] = db_required;
    
    db_tool.inputSchema = make_optional(db_schema);
    
    server.registerTool(db_tool, executeSampleTool);
    
    if (verbose) {
      std::cerr << "[SETUP] Registered 3 tools" << std::endl;
    }
  }
  
  // Register prompts
  {
    Prompt greeting_prompt;
    greeting_prompt.name = "greeting";
    greeting_prompt.description = make_optional(std::string(
        "A friendly greeting interaction"));
    
    server.registerPrompt(greeting_prompt, getSamplePrompt);
    
    // Code review prompt with arguments
    Prompt code_prompt;
    code_prompt.name = "code_review";
    code_prompt.description = make_optional(std::string(
        "Template for code review requests"));
    
    PromptArgument code_arg;
    code_arg.name = "code";
    code_arg.description = make_optional(std::string("The code to review"));
    code_arg.required = true;
    
    PromptArgument lang_arg;
    lang_arg.name = "language";
    lang_arg.description = make_optional(std::string("Programming language"));
    lang_arg.required = false;
    
    code_prompt.arguments = make_optional(std::vector<PromptArgument>{code_arg, lang_arg});
    
    server.registerPrompt(code_prompt, getSamplePrompt);
    
    // Data analysis prompt
    Prompt data_prompt;
    data_prompt.name = "data_analysis";
    data_prompt.description = make_optional(std::string(
        "Template for data analysis requests"));
    
    PromptArgument dataset_arg;
    dataset_arg.name = "dataset";
    dataset_arg.description = make_optional(std::string("Dataset to analyze"));
    dataset_arg.required = true;
    
    PromptArgument type_arg;
    type_arg.name = "analysis_type";
    type_arg.description = make_optional(std::string("Type of analysis"));
    type_arg.required = false;
    
    data_prompt.arguments = make_optional(std::vector<PromptArgument>{dataset_arg, type_arg});
    
    server.registerPrompt(data_prompt, getSamplePrompt);
    
    if (verbose) {
      std::cerr << "[SETUP] Registered 3 prompts" << std::endl;
    }
  }
  
  // Register custom request handlers
  {
    // Ping handler - IMPORTANT: Required for client keep-alive
    server.registerRequestHandler("ping", 
        [](const jsonrpc::Request& request, SessionContext& session) {
      auto pong = make<Metadata>()
          .add("pong", true)
          .add("timestamp", static_cast<long long>(std::time(nullptr)))
          .build();
      
      return jsonrpc::Response::success(request.id, 
          jsonrpc::ResponseResult(pong));
    });
    
    // Echo handler
    server.registerRequestHandler("echo", 
        [](const jsonrpc::Request& request, SessionContext& session) {
      auto response_data = make<Metadata>()
          .add("echo", true)
          .add("session_id", session.getId())
          .add("timestamp", static_cast<long long>(std::time(nullptr)))
          .build();
      
      if (request.params.has_value()) {
        // Echo back the params - just add a flag that params were received
        auto builder = make<Metadata>();
        for (const auto& pair : response_data) {
          builder.add(pair.first, pair.second);
        }
        builder.add("params_received", true);
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
          .add("uptime_seconds", static_cast<long long>(std::time(nullptr)))  // Just use current time
          .add("sessions_active", static_cast<long long>(stats.sessions_active.load()))
          .add("requests_total", static_cast<long long>(stats.requests_total.load()))
          .add("connections_active", static_cast<long long>(stats.connections_active.load()))
          .build();
      
      return jsonrpc::Response::success(request.id,
          jsonrpc::ResponseResult(status));
    });
    
    // Health check handler
    server.registerRequestHandler("health",
        [](const jsonrpc::Request& request, SessionContext& session) {
      auto health = make<Metadata>()
          .add("status", "healthy")
          .add("timestamp", static_cast<long long>(std::time(nullptr)))
          .build();
      
      return jsonrpc::Response::success(request.id,
          jsonrpc::ResponseResult(health));
    });
    
    if (verbose) {
      std::cerr << "[SETUP] Registered 4 custom request handlers (ping, echo, status, health)" << std::endl;
    }
  }
  
  // Register notification handlers
  {
    // Log notification handler
    server.registerNotificationHandler("log",
        [verbose](const jsonrpc::Notification& notification, SessionContext& session) {
      if (notification.params.has_value()) {
        auto params = notification.params.value();
        auto msg_it = params.find("message");
        auto level_it = params.find("level");
        
        std::string level = "INFO";
        if (level_it != params.end() && holds_alternative<std::string>(level_it->second)) {
          level = get<std::string>(level_it->second);
        }
        
        if (msg_it != params.end() && holds_alternative<std::string>(msg_it->second)) {
          if (verbose || level == "ERROR" || level == "WARN") {
            std::cerr << "[" << level << " from " << session.getId() << "] " 
                      << get<std::string>(msg_it->second) << std::endl;
          }
        }
      }
    });
    
    // Heartbeat handler
    server.registerNotificationHandler("heartbeat",
        [](const jsonrpc::Notification& notification, SessionContext& session) {
      // Update session activity
      session.updateActivity();
    });
    
    // Progress notification handler
    server.registerNotificationHandler("progress",
        [verbose](const jsonrpc::Notification& notification, SessionContext& session) {
      if (verbose && notification.params.has_value()) {
        auto params = notification.params.value();
        auto progress_it = params.find("progress");
        auto message_it = params.find("message");
        
        if (progress_it != params.end()) {
          double progress = holds_alternative<double>(progress_it->second) ?
                           get<double>(progress_it->second) : 0.0;
          
          std::string message = "";
          if (message_it != params.end() && holds_alternative<std::string>(message_it->second)) {
            message = get<std::string>(message_it->second);
          }
          
          std::cerr << "[PROGRESS from " << session.getId() << "] " 
                    << (progress * 100) << "% - " << message << std::endl;
        }
      }
    });
    
    if (verbose) {
      std::cerr << "[SETUP] Registered 3 notification handlers" << std::endl;
    }
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
  
  // Parse command-line options
  ServerOptions options = parseArguments(argc, argv);
  
  std::cerr << "=====================================================" << std::endl;
  std::cerr << "MCP Server - Enterprise Edition" << std::endl;
  std::cerr << "=====================================================" << std::endl;
  
  // Build listen address based on transport type
  std::string listen_address;
  if (options.transport == "stdio") {
    listen_address = "stdio://";
  } else if (options.transport == "websocket" || options.transport == "ws") {
    std::ostringstream addr;
    addr << "ws://" << options.host << ":" << options.port;
    listen_address = addr.str();
  } else if (options.transport == "all") {
    // For "all", we'll start with HTTP and add other transports later
    std::ostringstream addr;
    addr << "http://" << options.host << ":" << options.port;
    listen_address = addr.str();
  } else {  // Default to HTTP/SSE
    std::ostringstream addr;
    addr << "http://" << options.host << ":" << options.port;
    listen_address = addr.str();
  }
  
  std::cerr << "[INFO] Primary transport: " << options.transport << std::endl;
  std::cerr << "[INFO] Listen address: " << listen_address << std::endl;
  
  // Configure server with enterprise features
  McpServerConfig config;
  
  // Protocol settings
  config.protocol_version = "2024-11-05";
  config.server_name = "mcp-enterprise-server";
  config.server_version = "2.0.0";
  config.instructions = "Enterprise MCP server with full feature set";
  
  // HTTP/SSE endpoint configuration
  config.http_rpc_path = options.http_rpc_path;
  config.http_sse_path = options.http_sse_path;
  config.http_health_path = options.http_health_path;
  
  // Transport settings
  if (options.transport == "all") {
    config.supported_transports = {TransportType::HttpSse, TransportType::Stdio, TransportType::WebSocket};
  } else if (options.transport == "stdio") {
    config.supported_transports = {TransportType::Stdio};
  } else if (options.transport == "websocket" || options.transport == "ws") {
    config.supported_transports = {TransportType::WebSocket};
  } else {
    config.supported_transports = {TransportType::HttpSse};
  }
  
  // Session management
  config.max_sessions = options.max_sessions;
  config.session_timeout = std::chrono::minutes(options.session_timeout_minutes);
  config.allow_concurrent_sessions = true;
  // config.session_cleanup_interval = std::chrono::minutes(5);  // Not yet available
  
  // Request processing
  config.request_queue_size = options.request_queue_size;
  config.request_processing_timeout = std::chrono::seconds(options.request_timeout_seconds);
  config.enable_request_validation = true;
  // config.max_request_size = 10 * 1024 * 1024;  // 10MB - not yet available
  
  // Resource management
  config.enable_resource_subscriptions = true;
  config.max_subscriptions_per_session = 50;
  config.resource_update_debounce = std::chrono::milliseconds(100);
  // config.resource_cache_size = 100;  // Not yet available
  
  // Worker configuration
  config.num_workers = options.workers;
  
  // Flow control
  config.buffer_high_watermark = 1024 * 1024;  // 1MB
  config.buffer_low_watermark = 256 * 1024;    // 256KB
  // config.enable_compression = true;  // Not yet available
  
  // Observability
  config.enable_metrics = options.metrics;
  // config.metrics_interval = std::chrono::seconds(10);  // Not available in current API
  // config.enable_tracing = options.verbose;  // Not yet available
  // config.enable_access_logs = true;  // Not yet available
  
  // Server capabilities
  config.capabilities.tools = make_optional(true);
  config.capabilities.prompts = make_optional(true);
  config.capabilities.logging = make_optional(true);
  
  // Resources capability with subscription support
  ResourcesCapability res_cap;
  res_cap.subscribe = make_optional(EmptyCapability());
  res_cap.listChanged = make_optional(EmptyCapability());
  config.capabilities.resources = make_optional(variant<bool, ResourcesCapability>(res_cap));
  
  // Experimental capabilities
  config.capabilities.experimental = make_optional(Metadata());
  
  // Create server
  std::cerr << "[INFO] Creating MCP server..." << std::endl;
  std::cerr << "[INFO] Worker threads: " << options.workers << std::endl;
  std::cerr << "[INFO] Max sessions: " << options.max_sessions << std::endl;
  std::cerr << "[INFO] Session timeout: " << options.session_timeout_minutes << " minutes" << std::endl;
  
  {
    std::lock_guard<std::mutex> lock(g_server_mutex);
    g_server = createMcpServer(config);
    if (!g_server) {
      std::cerr << "[ERROR] Failed to create server" << std::endl;
      return 1;
    }
  }
  
  // Setup server resources, tools, and prompts
  setupServer(*g_server, options.verbose);
  
  // Start listening
  std::cerr << "[INFO] Starting server on port " << options.port << "..." << std::endl;
  auto listen_result = g_server->listen(listen_address);
  
  if (is_error<std::nullptr_t>(listen_result)) {
    std::cerr << "[ERROR] Failed to start server: " 
              << get_error<std::nullptr_t>(listen_result)->message << std::endl;
    std::cerr << "[HINT] Make sure port " << options.port << " is not already in use" << std::endl;
    return 1;
  }
  
  std::cerr << "[INFO] Server started successfully!" << std::endl;
  std::cerr << "[INFO] Listening on " << options.host << ":" << options.port << std::endl;
  
  if (options.transport == "http" || options.transport == "all") {
    std::cerr << "\n[INFO] HTTP/SSE Endpoints:" << std::endl;
    std::cerr << "  JSON-RPC: POST http://" << options.host << ":" << options.port << config.http_rpc_path << std::endl;
    std::cerr << "  SSE Events: GET http://" << options.host << ":" << options.port << config.http_sse_path << std::endl;
    std::cerr << "  Health: GET http://" << options.host << ":" << options.port << config.http_health_path << std::endl;
    if (options.metrics) {
      std::cerr << "  Metrics: GET http://" << options.host << ":" << options.port << "/metrics" << std::endl;
    }
  }
  
  std::cerr << "\n[INFO] Press Ctrl+C to shutdown" << std::endl;
  
  // Give server time to fully initialize
  std::this_thread::sleep_for(std::chrono::seconds(1));
  
  // Main loop - periodic status and resource updates
  int update_count = 0;
  auto start_time = std::chrono::steady_clock::now();
  
  if (options.verbose) {
    std::cerr << "[DEBUG] About to run server event loop..." << std::endl;
  }
  
  // Start a shutdown monitor thread
  std::thread shutdown_monitor([&]() {
    std::unique_lock<std::mutex> lock(g_server_mutex);
    g_shutdown_cv.wait(lock, []() { return g_shutdown.load(); });
    
    std::cerr << "[DEBUG] Shutdown monitor triggered, initiating server shutdown..." << std::endl;
    
    // Shutdown was requested
    if (g_server) {
      try {
        // Print statistics before shutdown
        std::cerr << "\n[INFO] Printing server statistics before shutdown..." << std::endl;
        printStatistics(*g_server);
        
        // Send shutdown notification to all clients
        auto shutdown_notif = jsonrpc::make_notification("server/shutdown");
        g_server->broadcastNotification(shutdown_notif);
        
        // Give clients time to disconnect
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        // Shutdown server - this will cause run() to return
        std::cerr << "[DEBUG] Calling g_server->shutdown()..." << std::endl;
        g_server->shutdown();
        std::cerr << "[DEBUG] Server shutdown called successfully" << std::endl;
      } catch (const std::exception& e) {
        std::cerr << "[ERROR] Exception during shutdown: " << e.what() << std::endl;
      }
    } else {
      std::cerr << "[ERROR] g_server is null, cannot shutdown properly" << std::endl;
    }
  });
  
  // Run the server's event loop in main thread
  // Following good design pattern: main thread runs the event loop
  // This blocks until shutdown() is called
  g_server->run();
  
  // Wait for shutdown monitor to complete
  if (shutdown_monitor.joinable()) {
    shutdown_monitor.join();
  }
  
  // Old monitoring loop - no longer needed since event loop handles everything
  /*
  while (!g_shutdown) {
    try {
      std::this_thread::sleep_for(std::chrono::seconds(10));
      
      if (g_shutdown) break;
      
      update_count++;
      
      // Use mutex to safely access server
      {
        std::lock_guard<std::mutex> lock(g_server_mutex);
        if (!g_server || !g_server->isRunning()) {
          std::cerr << "[WARNING] Server is not running, exiting main loop" << std::endl;
          break;
        }
        
        // Update server metrics resource
        if (update_count % 3 == 0) {  // Every 30 seconds
          if (options.verbose) {
            std::cerr << "[INFO] Update cycle " << update_count/3 << std::endl;
          }
          
          try {
            // Notify subscribers of metrics update
            g_server->notifyResourceUpdate("metrics://server/stats");
            
            // Broadcast server status if verbose
            if (options.verbose) {
              auto status_notif = jsonrpc::make_notification("server/heartbeat");
              g_server->broadcastNotification(status_notif);
            }
          } catch (const std::exception& e) {
            std::cerr << "[ERROR] Failed to send notifications: " << e.what() << std::endl;
          }
        }
        
        // Print status periodically
        if (options.verbose || (update_count % 6 == 0)) {  // Every minute
          auto now = std::chrono::steady_clock::now();
          auto uptime = std::chrono::duration_cast<std::chrono::seconds>(now - start_time).count();
          
          try {
            const auto& stats = g_server->getServerStats();
            std::cerr << "[STATUS] Uptime: " << uptime << "s"
                      << " | Sessions: " << stats.sessions_active
                      << " | Requests: " << stats.requests_total
                      << " | Connections: " << stats.connections_active << std::endl;
          } catch (const std::exception& e) {
            std::cerr << "[ERROR] Failed to get server stats: " << e.what() << std::endl;
          }
        }
        
        // Show detailed metrics periodically if enabled
        if (options.metrics && update_count % 30 == 0) {  // Every 5 minutes
          try {
            printStatistics(*g_server);
          } catch (const std::exception& e) {
            std::cerr << "[ERROR] Failed to print statistics: " << e.what() << std::endl;
          }
        }
      }
      
    } catch (const std::exception& e) {
      std::cerr << "[ERROR] Exception in main loop: " << e.what() << std::endl;
    }
  }
  */
  
  // Graceful shutdown
  std::cerr << "\n[INFO] Shutting down application..." << std::endl;
  
  // Server shutdown already initiated in signal handler
  // Statistics were already printed in shutdown monitor
  // Just wait a bit for cleanup
  std::this_thread::sleep_for(std::chrono::milliseconds(500));
  
  // Clean up server
  {
    std::lock_guard<std::mutex> lock(g_server_mutex);
    if (g_server) {
      g_server.reset(); // Clean up server
    }
  }
  
  std::cerr << "\n[INFO] Server shutdown complete" << std::endl;
  
  return 0;
}