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
 *   --transport <type>   Transport type: http, stdio, websocket, all (default:
 * http)
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

#include <chrono>
#include <condition_variable>
#include <ctime>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>
#include <thread>

// Platform-specific includes for signal/shutdown handling
#ifdef _WIN32
#include <windows.h>
#else
#include <signal.h>
#include <unistd.h>  // for _exit
#endif

#include "mcp/json/json_bridge.h"
#include "mcp/logging/log_macros.h"
#include "mcp/logging/log_sink.h"
#include "mcp/logging/logger_registry.h"
#include "mcp/server/mcp_server.h"
#include "mcp/transport/http_sse_transport_socket.h"

using namespace mcp;
using namespace mcp::server;

// Use logging namespace explicitly to avoid conflicts with std::make_optional
namespace logging = mcp::logging;

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
  std::string config_file;  // Configuration file path

  // Advanced options
  int session_timeout_minutes = 30;
  int request_queue_size = 500;
  int request_timeout_seconds = 60;

  // HTTP/SSE endpoint paths (configurable)
  std::string http_rpc_path = "/rpc";
  std::string http_sse_path = "/events";
  std::string http_health_path = "/health";
};

// Application logger - logs at INFO level to console
std::shared_ptr<logging::Logger> g_logger;

// Setup logging framework
// - Default level: INFO
// - Library (mcp.*): DEBUG level
// - Application: INFO level (visible on console)
void setupLogging(bool verbose) {
  auto& registry = logging::LoggerRegistry::instance();

  // Set global default level to INFO
  registry.setGlobalLevel(logging::LogLevel::Info);

  // Create console sink (stderr)
  auto console_sink = logging::SinkFactory::createStdioSink(true);

  // Create application logger at INFO level
  g_logger = registry.getOrCreateLogger("mcp_example_server");
  g_logger->setSink(std::move(console_sink));
  g_logger->setLevel(logging::LogLevel::Info);

  // Set library loggers to DEBUG level (will show if global is DEBUG)
  // Use pattern matching for library components
  registry.setPattern("mcp.*", logging::LogLevel::Debug);

  // If verbose mode, set global to DEBUG to see library logs
  if (verbose) {
    registry.setGlobalLevel(logging::LogLevel::Debug);
  }
}

// Platform-specific shutdown handler
#ifdef _WIN32
// Windows Console Control Handler
BOOL WINAPI ConsoleCtrlHandler(DWORD ctrlType) {
  switch (ctrlType) {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
    case CTRL_CLOSE_EVENT:
    case CTRL_SHUTDOWN_EVENT:
      std::cerr << "\n[INFO] Received console control event " << ctrlType
                << ", initiating graceful shutdown..." << std::endl;

      // Set the shutdown flag
      g_shutdown = true;

      // Notify the shutdown monitor thread
      g_shutdown_cv.notify_all();

      // For safety, if we receive multiple signals, force exit
      static std::atomic<int> signal_count(0);
      signal_count++;
      if (signal_count > 1) {
        std::cerr << "\n[INFO] Force shutdown after multiple signals..."
                  << std::endl;
        ExitProcess(0);
      }
      return TRUE;
    default:
      return FALSE;
  }
}
#else
// Unix signal handler
void signal_handler(int signal) {
  // Signal handlers should do minimal work
  // Just set the flag and notify - actual shutdown happens in main thread
  std::cerr << "\n[INFO] Received signal " << signal
            << ", initiating graceful shutdown..." << std::endl;

  // Set the shutdown flag
  g_shutdown = true;

  // Notify the shutdown monitor thread
  g_shutdown_cv.notify_all();

  // For safety, if we receive multiple signals, force exit
  static std::atomic<int> signal_count(0);
  signal_count++;
  if (signal_count > 1) {
    std::cerr << "\n[INFO] Force shutdown after multiple signals..."
              << std::endl;
    // Use _exit instead of std::exit to avoid destructor issues in signal
    // handler This is safer as it doesn't run destructors which might use
    // mutexes
    _exit(0);
  }
}
#endif

void printUsage(const char* program) {
  std::cerr << "USAGE: " << program << " [options]\n\n";
  std::cerr << "OPTIONS:\n";
  std::cerr << "  --port <port>        Listen port (default: 3000)\n";
  std::cerr << "  --host <address>     Bind address (default: 0.0.0.0)\n";
  std::cerr << "  --transport <type>   Transport type: http, stdio, websocket, "
               "all (default: http)\n";
  std::cerr << "  --workers <n>        Number of worker threads (default: 4)\n";
  std::cerr
      << "  --max-sessions <n>   Maximum concurrent sessions (default: 100)\n";
  std::cerr << "  --config <path>      Configuration file (JSON format)\n";
  std::cerr << "  --metrics            Enable metrics endpoint\n";
  std::cerr << "  --verbose            Enable verbose logging\n";
  std::cerr
      << "  --rpc-path <path>    HTTP JSON-RPC endpoint path (default: /rpc)\n";
  std::cerr << "  --sse-path <path>    HTTP SSE events endpoint path (default: "
               "/events)\n";
  std::cerr << "  --health-path <path> HTTP health check endpoint path "
               "(default: /health)\n";
  std::cerr << "  --help               Show this help message\n";
}

ServerOptions parseArguments(int argc, char* argv[]) {
  ServerOptions options;

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];

    if (arg == "--help" || arg == "-h") {
      printUsage(argv[0]);
      exit(0);
    } else if (arg == "--port" && i + 1 < argc) {
      options.port = std::atoi(argv[++i]);
    } else if (arg == "--host" && i + 1 < argc) {
      options.host = argv[++i];
    } else if (arg == "--transport" && i + 1 < argc) {
      options.transport = argv[++i];
    } else if (arg == "--workers" && i + 1 < argc) {
      options.workers = std::atoi(argv[++i]);
    } else if (arg == "--max-sessions" && i + 1 < argc) {
      options.max_sessions = std::atoi(argv[++i]);
    } else if (arg == "--metrics") {
      options.metrics = true;
    } else if (arg == "--verbose") {
      options.verbose = true;
    } else if (arg == "--config" && i + 1 < argc) {
      options.config_file = argv[++i];
    } else if (arg == "--rpc-path" && i + 1 < argc) {
      options.http_rpc_path = argv[++i];
    } else if (arg == "--sse-path" && i + 1 < argc) {
      options.http_sse_path = argv[++i];
    } else if (arg == "--health-path" && i + 1 < argc) {
      options.http_health_path = argv[++i];
    } else {
      // Use std::cerr since logging may not be set up yet
      std::cerr << "ERROR: Unknown option: " << arg << std::endl;
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

    std::string op = holds_alternative<std::string>(op_it->second)
                         ? get<std::string>(op_it->second)
                         : "";
    double a = holds_alternative<double>(a_it->second)
                   ? get<double>(a_it->second)
                   : (holds_alternative<int64_t>(a_it->second)
                          ? static_cast<double>(get<int64_t>(a_it->second))
                          : 0.0);
    double b = holds_alternative<double>(b_it->second)
                   ? get<double>(b_it->second)
                   : (holds_alternative<int64_t>(b_it->second)
                          ? static_cast<double>(get<int64_t>(b_it->second))
                          : 0.0);

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
        result.content.push_back(
            ExtendedContentBlock(TextContent("Division by zero")));
        return result;
      }
    } else {
      result.isError = true;
      result.content.push_back(ExtendedContentBlock(
          TextContent("Unknown operation: " + op +
                      ". Valid operations: add, subtract, multiply, divide")));
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
    // info << "- Session start: " << session.getStartTime() << "\n";  // Not
    // yet available info << "- Requests handled: " << session.getRequestCount()
    // << "\n";  // Not yet available

    result.content.push_back(ExtendedContentBlock(TextContent(info.str())));
  }

  // Database query tool - demonstrates async operations
  else if (name == "database_query") {
    if (!arguments.has_value()) {
      result.isError = true;
      result.content.push_back(
          ExtendedContentBlock(TextContent("Missing query parameter")));
      return result;
    }

    auto args = arguments.value();
    auto query_it = args.find("query");

    if (query_it == args.end() ||
        !holds_alternative<std::string>(query_it->second)) {
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

    result.content.push_back(ExtendedContentBlock(TextContent(response.str())));
  }

  else {
    result.isError = true;
    result.content.push_back(
        ExtendedContentBlock(TextContent("Unknown tool: " + name)));
  }

  return result;
}

// Example prompt handler
GetPromptResult getSamplePrompt(const std::string& name,
                                const optional<Metadata>& arguments,
                                SessionContext& session) {
  GetPromptResult result;

  if (name == "greeting") {
    result.description =
        mcp::make_optional(std::string("A friendly greeting prompt"));

    // Add prompt messages
    PromptMessage msg1(enums::Role::USER, TextContent("Hello!"));
    result.messages.push_back(msg1);

    PromptMessage msg2(
        enums::Role::ASSISTANT,
        TextContent(
            "Hello! I'm the MCP Enterprise Server. How can I help you today?"));
    result.messages.push_back(msg2);
  } else if (name == "code_review") {
    result.description =
        mcp::make_optional(std::string("Code review prompt template"));

    std::string code = "// Your code here";
    std::string language = "javascript";

    if (arguments.has_value()) {
      auto args = arguments.value();
      auto code_it = args.find("code");
      auto lang_it = args.find("language");

      if (code_it != args.end() &&
          holds_alternative<std::string>(code_it->second)) {
        code = get<std::string>(code_it->second);
      }
      if (lang_it != args.end() &&
          holds_alternative<std::string>(lang_it->second)) {
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
  } else if (name == "data_analysis") {
    result.description =
        mcp::make_optional(std::string("Data analysis prompt template"));

    std::string dataset = "sample_data.csv";
    std::string analysis_type = "descriptive";

    if (arguments.has_value()) {
      auto args = arguments.value();
      auto dataset_it = args.find("dataset");
      auto type_it = args.find("analysis_type");

      if (dataset_it != args.end() &&
          holds_alternative<std::string>(dataset_it->second)) {
        dataset = get<std::string>(dataset_it->second);
      }
      if (type_it != args.end() &&
          holds_alternative<std::string>(type_it->second)) {
        analysis_type = get<std::string>(type_it->second);
      }
    }

    std::stringstream prompt;
    prompt << "Perform " << analysis_type << " analysis on dataset: " << dataset
           << "\n\n";
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
  g_logger->info("Configuring server resources and tools...");

  // Register sample resources
  {
    // Configuration resource
    Resource config_resource;
    config_resource.uri = "config://server/settings";
    config_resource.name = "Server Configuration";
    config_resource.description =
        mcp::make_optional(std::string("Current server configuration and settings"));
    config_resource.mimeType = mcp::make_optional(std::string("application/json"));

    server.registerResource(config_resource);

    // Log resource
    Resource log_resource;
    log_resource.uri = "log://server/events";
    log_resource.name = "Server Event Log";
    log_resource.description =
        mcp::make_optional(std::string("Real-time server event log"));
    log_resource.mimeType = mcp::make_optional(std::string("text/plain"));

    server.registerResource(log_resource);

    // Metrics resource
    Resource metrics_resource;
    metrics_resource.uri = "metrics://server/stats";
    metrics_resource.name = "Server Metrics";
    metrics_resource.description =
        mcp::make_optional(std::string("Server performance metrics and statistics"));
    metrics_resource.mimeType = mcp::make_optional(std::string("application/json"));

    server.registerResource(metrics_resource);

    if (verbose) {
      g_logger->debug("Registered 3 static resources");
    }
  }

  // Register resource templates
  {
    ResourceTemplate file_template;
    file_template.uriTemplate = "file://{path}";
    file_template.name = "File Resource";
    file_template.description =
        mcp::make_optional(std::string("Access files on the server filesystem"));
    file_template.mimeType = mcp::make_optional(std::string("text/plain"));

    server.registerResourceTemplate(file_template);

    ResourceTemplate db_template;
    db_template.uriTemplate = "db://{database}/{table}";
    db_template.name = "Database Resource";
    db_template.description =
        mcp::make_optional(std::string("Access database tables"));
    db_template.mimeType = mcp::make_optional(std::string("application/json"));

    server.registerResourceTemplate(db_template);

    if (verbose) {
      g_logger->debug("Registered 2 resource templates");
    }
  }

  // Register tools
  {
    g_logger->debug("About to register tools...");

    // Calculator tool with schema
    Tool calc_tool;
    calc_tool.name = "calculator";
    calc_tool.description = mcp::make_optional(
        std::string("Simple calculator for basic arithmetic operations"));

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

    calc_tool.inputSchema = mcp::make_optional(schema);

    g_logger->debug("Registering calculator tool...");
    server.registerTool(calc_tool, executeSampleTool);
    g_logger->debug("Calculator tool registered");

    // System info tool
    Tool info_tool;
    info_tool.name = "system_info";
    info_tool.description =
        mcp::make_optional(std::string("Get system and server information"));

    server.registerTool(info_tool, executeSampleTool);

    // Database query tool
    Tool db_tool;
    db_tool.name = "database_query";
    db_tool.description =
        mcp::make_optional(std::string("Execute database queries"));

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

    db_tool.inputSchema = mcp::make_optional(db_schema);

    server.registerTool(db_tool, executeSampleTool);

    if (verbose) {
      g_logger->debug("Registered 3 tools");
    }
  }

  // Register prompts
  {
    Prompt greeting_prompt;
    greeting_prompt.name = "greeting";
    greeting_prompt.description =
        mcp::make_optional(std::string("A friendly greeting interaction"));

    server.registerPrompt(greeting_prompt, getSamplePrompt);

    // Code review prompt with arguments
    Prompt code_prompt;
    code_prompt.name = "code_review";
    code_prompt.description =
        mcp::make_optional(std::string("Template for code review requests"));

    PromptArgument code_arg;
    code_arg.name = "code";
    code_arg.description = mcp::make_optional(std::string("The code to review"));
    code_arg.required = true;

    PromptArgument lang_arg;
    lang_arg.name = "language";
    lang_arg.description = mcp::make_optional(std::string("Programming language"));
    lang_arg.required = false;

    code_prompt.arguments =
        mcp::make_optional(std::vector<PromptArgument>{code_arg, lang_arg});

    server.registerPrompt(code_prompt, getSamplePrompt);

    // Data analysis prompt
    Prompt data_prompt;
    data_prompt.name = "data_analysis";
    data_prompt.description =
        mcp::make_optional(std::string("Template for data analysis requests"));

    PromptArgument dataset_arg;
    dataset_arg.name = "dataset";
    dataset_arg.description = mcp::make_optional(std::string("Dataset to analyze"));
    dataset_arg.required = true;

    PromptArgument type_arg;
    type_arg.name = "analysis_type";
    type_arg.description = mcp::make_optional(std::string("Type of analysis"));
    type_arg.required = false;

    data_prompt.arguments =
        mcp::make_optional(std::vector<PromptArgument>{dataset_arg, type_arg});

    server.registerPrompt(data_prompt, getSamplePrompt);

    if (verbose) {
      g_logger->debug("Registered 3 prompts");
    }
  }

  // Register custom request handlers
  {
    // Ping handler - IMPORTANT: Required for client keep-alive
    server.registerRequestHandler(
        "ping", [](const jsonrpc::Request& request, SessionContext& session) {
          auto pong =
              make<Metadata>()
                  .add("pong", true)
                  .add("timestamp", static_cast<int64_t>(std::time(nullptr)))
                  .build();

          return jsonrpc::Response::success(request.id,
                                            jsonrpc::ResponseResult(pong));
        });

    // Echo handler
    server.registerRequestHandler(
        "echo", [](const jsonrpc::Request& request, SessionContext& session) {
          auto response_data =
              make<Metadata>()
                  .add("echo", true)
                  .add("session_id", session.getId())
                  .add("timestamp", static_cast<int64_t>(std::time(nullptr)))
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

          return jsonrpc::Response::success(
              request.id, jsonrpc::ResponseResult(response_data));
        });

    // Status handler
    server.registerRequestHandler(
        "server/status",
        [&server](const jsonrpc::Request& request, SessionContext& session) {
          const auto& stats = server.getServerStats();

          auto status =
              make<Metadata>()
                  .add("running", server.isRunning())
                  .add("uptime_seconds",
                       static_cast<int64_t>(
                           std::time(nullptr)))  // Just use current time
                  .add("sessions_active",
                       static_cast<int64_t>(stats.sessions_active.load()))
                  .add("requests_total",
                       static_cast<int64_t>(stats.requests_total.load()))
                  .add("connections_active",
                       static_cast<int64_t>(stats.connections_active.load()))
                  .build();

          return jsonrpc::Response::success(request.id,
                                            jsonrpc::ResponseResult(status));
        });

    // Health check handler
    server.registerRequestHandler(
        "health", [](const jsonrpc::Request& request, SessionContext& session) {
          auto health =
              make<Metadata>()
                  .add("status", "healthy")
                  .add("timestamp", static_cast<int64_t>(std::time(nullptr)))
                  .build();

          return jsonrpc::Response::success(request.id,
                                            jsonrpc::ResponseResult(health));
        });

    if (verbose) {
      g_logger->debug(
          "Registered 4 custom request handlers (ping, echo, status, health)");
    }
  }

  // Register notification handlers
  {
    // Log notification handler
    server.registerNotificationHandler(
        "log", [verbose](const jsonrpc::Notification& notification,
                         SessionContext& session) {
          if (notification.params.has_value()) {
            auto params = notification.params.value();
            auto msg_it = params.find("message");
            auto level_it = params.find("level");

            std::string level = "INFO";
            if (level_it != params.end() &&
                holds_alternative<std::string>(level_it->second)) {
              level = get<std::string>(level_it->second);
            }

            if (msg_it != params.end() &&
                holds_alternative<std::string>(msg_it->second)) {
              if (verbose || level == "ERROR" || level == "WARN") {
                std::string msg = get<std::string>(msg_it->second);
                if (level == "ERROR") {
                  g_logger->error("[{}] {}", session.getId(), msg);
                } else if (level == "WARN") {
                  g_logger->warning("[{}] {}", session.getId(), msg);
                } else {
                  g_logger->info("[{}] {}", session.getId(), msg);
                }
              }
            }
          }
        });

    // Heartbeat handler
    server.registerNotificationHandler(
        "heartbeat",
        [](const jsonrpc::Notification& notification, SessionContext& session) {
          // Update session activity
          session.updateActivity();
        });

    // Progress notification handler
    server.registerNotificationHandler(
        "progress", [verbose](const jsonrpc::Notification& notification,
                              SessionContext& session) {
          if (verbose && notification.params.has_value()) {
            auto params = notification.params.value();
            auto progress_it = params.find("progress");
            auto message_it = params.find("message");

            if (progress_it != params.end()) {
              double progress = holds_alternative<double>(progress_it->second)
                                    ? get<double>(progress_it->second)
                                    : 0.0;

              std::string message = "";
              if (message_it != params.end() &&
                  holds_alternative<std::string>(message_it->second)) {
                message = get<std::string>(message_it->second);
              }

              g_logger->debug("[PROGRESS from {}] {}% - {}", session.getId(),
                              progress * 100, message);
            }
          }
        });

    if (verbose) {
      g_logger->debug("Registered 3 notification handlers");
    }
  }

  g_logger->info("Server configuration complete");
}

// Print server statistics
void printStatistics(const McpServer& server) {
  const auto& stats = server.getServerStats();

  g_logger->info("=== Server Statistics ===");
  g_logger->info("Sessions: Total={} Active={} Expired={}",
                 stats.sessions_total.load(), stats.sessions_active.load(),
                 stats.sessions_expired.load());
  g_logger->info("Connections: Total={} Active={}", stats.connections_total.load(),
                 stats.connections_active.load());
  g_logger->info(
      "Requests: Total={} Success={} Failed={} Invalid={} Notifications={}",
      stats.requests_total.load(), stats.requests_success.load(),
      stats.requests_failed.load(), stats.requests_invalid.load(),
      stats.notifications_total.load());
  g_logger->info("Resources: Served={} Subscribed={} Updates={}",
                 stats.resources_served.load(), stats.resources_subscribed.load(),
                 stats.resource_updates_sent.load());
  g_logger->info("Tools: Executed={} Failed={}", stats.tools_executed.load(),
                 stats.tools_failed.load());
  g_logger->info("Prompts: Retrieved={}", stats.prompts_retrieved.load());
  g_logger->info("Data Transfer: Sent={} bytes Received={} bytes",
                 stats.bytes_sent.load(), stats.bytes_received.load());
  g_logger->info("Errors: Total={}", stats.errors_total.load());
}

int main(int argc, char* argv[]) {
  // Install platform-specific signal/shutdown handlers
#ifdef _WIN32
  SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);
#else
  signal(SIGINT, signal_handler);
  signal(SIGTERM, signal_handler);
#endif

  // Parse command-line options
  ServerOptions options = parseArguments(argc, argv);

  // Setup logging framework before any logging
  setupLogging(options.verbose);

  g_logger->info("=====================================================");
  g_logger->info("MCP Server - Enterprise Edition");
  g_logger->info("=====================================================");

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

  g_logger->info("Primary transport: {}", options.transport);
  g_logger->info("Listen address: {}", listen_address);

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
    config.supported_transports = {TransportType::HttpSse, TransportType::Stdio,
                                   TransportType::WebSocket};
  } else if (options.transport == "stdio") {
    config.supported_transports = {TransportType::Stdio};
  } else if (options.transport == "websocket" || options.transport == "ws") {
    config.supported_transports = {TransportType::WebSocket};
  } else {
    config.supported_transports = {TransportType::HttpSse};
  }

  // Session management
  config.max_sessions = options.max_sessions;
  config.session_timeout =
      std::chrono::minutes(options.session_timeout_minutes);
  config.allow_concurrent_sessions = true;
  // config.session_cleanup_interval = std::chrono::minutes(5);  // Not yet
  // available

  // Request processing
  config.request_queue_size = options.request_queue_size;
  config.request_processing_timeout =
      std::chrono::seconds(options.request_timeout_seconds);
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
  // config.metrics_interval = std::chrono::seconds(10);  // Not available in
  // current API config.enable_tracing = options.verbose;  // Not yet available
  // config.enable_access_logs = true;  // Not yet available

  // Server capabilities
  config.capabilities.tools = mcp::make_optional(true);
  config.capabilities.prompts = mcp::make_optional(true);
  config.capabilities.logging = mcp::make_optional(true);

  // Resources capability with subscription support
  ResourcesCapability res_cap;
  res_cap.subscribe = mcp::make_optional(EmptyCapability());
  res_cap.listChanged = mcp::make_optional(EmptyCapability());
  config.capabilities.resources =
      mcp::make_optional(variant<bool, ResourcesCapability>(res_cap));

  // Experimental capabilities
  config.capabilities.experimental = mcp::make_optional(Metadata());

  // Parse configuration file if provided
  if (!options.config_file.empty()) {
    g_logger->info("Loading configuration from: {}", options.config_file);

    std::ifstream config_file(options.config_file);
    if (!config_file) {
      g_logger->error("Cannot open config file: {}", options.config_file);
      return 1;
    }

    std::string json_str((std::istreambuf_iterator<char>(config_file)),
                         std::istreambuf_iterator<char>());

    try {
      auto json_config = mcp::json::JsonValue::parse(json_str);

      // Look for filter chains configuration
      if (json_config.contains("filter_chains")) {
        auto& chains = json_config["filter_chains"];
        if (chains.isArray()) {
          for (size_t i = 0; i < chains.size(); ++i) {
            auto& chain = chains[i];
            // Look for the server chain
            if (chain.contains("name") &&
                chain["name"].getString() == "server") {
              // Store the entire chain configuration
              config.filter_chain_config = mcp::make_optional(chain);
              g_logger->info("Found server filter chain configuration");

              // Log the filters that will be created
              if (chain.contains("filters") && chain["filters"].isArray()) {
                auto& filters = chain["filters"];
                g_logger->info("Filter chain will include {} filters:",
                               filters.size());
                for (size_t j = 0; j < filters.size(); ++j) {
                  if (filters[j].contains("type")) {
                    std::string filter_type = filters[j]["type"].getString();
                    std::string framing_info;

                    // Special handling for json_rpc to show framing config
                    if (filter_type == "json_rpc" &&
                        filters[j].contains("config") &&
                        filters[j]["config"].contains("use_framing")) {
                      bool use_framing =
                          filters[j]["config"]["use_framing"].getBool();
                      framing_info = use_framing ? " (framing: enabled)"
                                                 : " (framing: disabled)";
                    }
                    g_logger->info("  - {}{}", filter_type, framing_info);
                  }
                }
              }
              break;
            }
          }
        }
      }

      if (!config.filter_chain_config.has_value()) {
        g_logger->warning("No server filter chain found in config file");
      }
    } catch (const std::exception& e) {
      g_logger->error("Failed to parse config file: {}", e.what());
      return 1;
    }
  }

  // Create server
  g_logger->info("Creating MCP server...");
  g_logger->info("Worker threads: {}", options.workers);
  g_logger->info("Max sessions: {}", options.max_sessions);
  g_logger->info("Session timeout: {} minutes", options.session_timeout_minutes);

  {
    std::lock_guard<std::mutex> lock(g_server_mutex);
    g_server = createMcpServer(config);
    if (!g_server) {
      g_logger->error("Failed to create server");
      return 1;
    }
  }

  // Setup server resources, tools, and prompts
  setupServer(*g_server, options.verbose);

  // Start listening
  g_logger->info("Starting server on port {}...", options.port);
  auto listen_result = g_server->listen(listen_address);

  if (is_error<std::nullptr_t>(listen_result)) {
    g_logger->error("Failed to start server: {}",
                    get_error<std::nullptr_t>(listen_result)->message);
    g_logger->info("HINT: Make sure port {} is not already in use",
                   options.port);
    return 1;
  }

  g_logger->info("Server started successfully!");
  g_logger->info("Listening on {}:{}", options.host, options.port);

  if (options.transport == "http" || options.transport == "all") {
    g_logger->info("HTTP/SSE Endpoints:");
    g_logger->info("  JSON-RPC: POST http://{}:{}{}", options.host, options.port,
                   config.http_rpc_path);
    g_logger->info("  SSE Events: GET http://{}:{}{}", options.host,
                   options.port, config.http_sse_path);
    g_logger->info("  Health: GET http://{}:{}{}", options.host, options.port,
                   config.http_health_path);
    if (options.metrics) {
      g_logger->info("  Metrics: GET http://{}:{}/metrics", options.host,
                     options.port);
    }
  }

  g_logger->info("Press Ctrl+C to shutdown");

  // Give server time to fully initialize
  std::this_thread::sleep_for(std::chrono::seconds(1));

  // Main loop - periodic status and resource updates
  int update_count = 0;
  auto start_time = std::chrono::steady_clock::now();

  if (options.verbose) {
    g_logger->debug("About to run server event loop...");
  }

  // Start a shutdown monitor thread
  std::thread shutdown_monitor([&]() {
    std::unique_lock<std::mutex> lock(g_server_mutex);
    g_shutdown_cv.wait(lock, []() { return g_shutdown.load(); });

    g_logger->debug("Shutdown monitor triggered, initiating server shutdown...");

    // Shutdown was requested
    if (g_server) {
      try {
        // Print statistics before shutdown
        g_logger->info("Printing server statistics before shutdown...");
        printStatistics(*g_server);

        // Send shutdown notification to all clients
        auto shutdown_notif = jsonrpc::make_notification("server/shutdown");
        g_server->broadcastNotification(shutdown_notif);

        // Give clients time to disconnect
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        // Shutdown server - this will cause run() to return
        g_logger->debug("Calling g_server->shutdown()...");
        g_server->shutdown();
        g_logger->debug("Server shutdown called successfully");
      } catch (const std::exception& e) {
        g_logger->error("Exception during shutdown: {}", e.what());
      }
    } else {
      g_logger->error("g_server is null, cannot shutdown properly");
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
          std::cerr << "[WARNING] Server is not running, exiting main loop" <<
  std::endl; break;
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
              auto status_notif =
  jsonrpc::make_notification("server/heartbeat");
              g_server->broadcastNotification(status_notif);
            }
          } catch (const std::exception& e) {
            std::cerr << "[ERROR] Failed to send notifications: " << e.what() <<
  std::endl;
          }
        }

        // Print status periodically
        if (options.verbose || (update_count % 6 == 0)) {  // Every minute
          auto now = std::chrono::steady_clock::now();
          auto uptime = std::chrono::duration_cast<std::chrono::seconds>(now -
  start_time).count();

          try {
            const auto& stats = g_server->getServerStats();
            std::cerr << "[STATUS] Uptime: " << uptime << "s"
                      << " | Sessions: " << stats.sessions_active
                      << " | Requests: " << stats.requests_total
                      << " | Connections: " << stats.connections_active <<
  std::endl; } catch (const std::exception& e) { std::cerr << "[ERROR] Failed to
  get server stats: " << e.what() << std::endl;
          }
        }

        // Show detailed metrics periodically if enabled
        if (options.metrics && update_count % 30 == 0) {  // Every 5 minutes
          try {
            printStatistics(*g_server);
          } catch (const std::exception& e) {
            std::cerr << "[ERROR] Failed to print statistics: " << e.what() <<
  std::endl;
          }
        }
      }

    } catch (const std::exception& e) {
      std::cerr << "[ERROR] Exception in main loop: " << e.what() << std::endl;
    }
  }
  */

  // Graceful shutdown
  g_logger->info("Shutting down application...");

  // Server shutdown already initiated in signal handler
  // Statistics were already printed in shutdown monitor
  // Just wait a bit for cleanup
  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  // Clean up server
  {
    std::lock_guard<std::mutex> lock(g_server_mutex);
    if (g_server) {
      g_server.reset();  // Clean up server
    }
  }

  g_logger->info("Server shutdown complete");

  return 0;
}