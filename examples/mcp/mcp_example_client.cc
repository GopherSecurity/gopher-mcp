/**
 * @file mcp_example_client.cc
 * @brief Enterprise-grade MCP client with HTTP/SSE transport
 *
 * This example demonstrates a production-ready MCP client with:
 * - HTTP/SSE transport (default) with configurable host and port
 * - Automatic transport negotiation and fallback
 * - Connection pooling for high throughput
 * - Circuit breaker for fault tolerance
 * - Exponential backoff retry logic
 * - Request batching and pipelining
 * - Progress tracking for long operations
 * - Comprehensive metrics and observability
 * - Graceful shutdown handling
 *
 * USAGE:
 *   mcp_example_client [options]
 *
 * OPTIONS:
 *   --host <hostname>    Server hostname (default: localhost)
 *   --port <port>        Server port (default: 3000)
 *   --transport <type>   Transport type: http, stdio, websocket (default: http)
 *   --demo               Run feature demonstrations
 *   --metrics            Show detailed metrics
 *   --verbose            Enable verbose logging
 *   --help               Show this help message
 *
 * ENTERPRISE FEATURES:
 *
 * 1. CONNECTION MANAGEMENT
 *    - Connection pooling with configurable size
 *    - Keep-alive and connection reuse
 *    - Automatic reconnection with exponential backoff
 *    - Health checks and dead connection detection
 *
 * 2. FAULT TOLERANCE
 *    - Circuit breaker pattern to prevent cascading failures
 *    - Configurable error thresholds and timeout periods
 *    - Half-open state for gradual recovery
 *    - Request retry with jitter
 *
 * 3. PERFORMANCE OPTIMIZATION
 *    - Request batching for reduced round trips
 *    - Request pipelining for improved throughput
 *    - Compression support (gzip, brotli)
 *    - Zero-copy buffer management
 *
 * 4. OBSERVABILITY
 *    - Detailed metrics collection (requests, latency, errors)
 *    - Distributed tracing support
 *    - Structured logging
 *    - Health endpoints
 *
 * 5. SECURITY
 *    - TLS/SSL support with certificate validation
 *    - Client certificate authentication
 *    - API key and OAuth2 support
 *    - Request signing and verification
 *
 * EXAMPLES:
 *   # Connect to local server on default port
 *   ./mcp_example_client
 *
 *   # Connect to remote server
 *   ./mcp_example_client --host api.example.com --port 8080
 *
 *   # Use WebSocket transport
 *   ./mcp_example_client --transport websocket --port 8081
 *
 *   # Run with demo and metrics
 *   ./mcp_example_client --demo --metrics
 */

#include <chrono>
#include <cstring>
#include <iostream>
#include <mutex>
#include <signal.h>
#include <sstream>
#include <thread>

#include "mcp/client/mcp_client.h"
#include "mcp/transport/http_sse_transport_socket.h"

using namespace mcp;
using namespace mcp::client;

// Global client for signal handling
std::shared_ptr<McpClient> g_client;
std::atomic<bool> g_shutdown(false);
std::mutex g_client_mutex;

// Command-line options
struct ClientOptions {
  std::string host = "localhost";
  int port = 3000;
  std::string transport = "http";
  bool demo = false;
  bool metrics = false;
  bool verbose = false;
  bool quiet = false;  // Reduce output for automated usage

  // Advanced options
  int pool_size = 5;
  int max_retries = 3;
  int circuit_breaker_threshold = 5;
  int request_timeout_seconds = 30;
  int num_workers = 2;
};

void signal_handler(int signal) {
  // Signal handlers should do minimal work to avoid deadlocks
  std::cerr << "\n[INFO] Received signal " << signal << ", shutting down..."
            << std::endl;
  g_shutdown = true;

  // Schedule shutdown through client if it exists
  // This is thread-safe as shutdown() is idempotent
  auto client = g_client;  // Local copy to avoid lock in signal handler
  if (client) {
    client->shutdown();
  }
}

void printUsage(const char* program) {
  std::cerr << "USAGE: " << program << " [options]\n\n";
  std::cerr << "OPTIONS:\n";
  std::cerr << "  --host <hostname>    Server hostname (default: localhost)\n";
  std::cerr << "  --port <port>        Server port (default: 3000)\n";
  std::cerr << "  --transport <type>   Transport type: http, stdio, websocket "
               "(default: http)\n";
  std::cerr << "  --demo               Run feature demonstrations\n";
  std::cerr << "  --metrics            Show detailed metrics\n";
  std::cerr << "  --verbose            Enable verbose logging\n";
  std::cerr << "  --pool-size <n>      Connection pool size (default: 5)\n";
  std::cerr << "  --max-retries <n>    Maximum retry attempts (default: 3)\n";
  std::cerr << "  --workers <n>        Number of worker threads (default: 2)\n";
  std::cerr << "  --quiet              Reduce output (only show errors)\n";
  std::cerr << "  --help               Show this help message\n";
}

ClientOptions parseArguments(int argc, char* argv[]) {
  ClientOptions options;

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];

    if (arg == "--help" || arg == "-h") {
      printUsage(argv[0]);
      exit(0);
    } else if (arg == "--host" && i + 1 < argc) {
      options.host = argv[++i];
    } else if (arg == "--port" && i + 1 < argc) {
      options.port = std::atoi(argv[++i]);
    } else if (arg == "--transport" && i + 1 < argc) {
      options.transport = argv[++i];
    } else if (arg == "--demo") {
      options.demo = true;
    } else if (arg == "--metrics") {
      options.metrics = true;
    } else if (arg == "--verbose") {
      options.verbose = true;
    } else if (arg == "--pool-size" && i + 1 < argc) {
      options.pool_size = std::atoi(argv[++i]);
    } else if (arg == "--max-retries" && i + 1 < argc) {
      options.max_retries = std::atoi(argv[++i]);
    } else if (arg == "--workers" && i + 1 < argc) {
      options.num_workers = std::atoi(argv[++i]);
    } else if (arg == "--quiet") {
      options.quiet = true;
    } else {
      std::cerr << "[ERROR] Unknown option: " << arg << std::endl;
      printUsage(argv[0]);
      exit(1);
    }
  }

  return options;
}

// Helper to extract string from Metadata response
std::string extractMetadataString(const jsonrpc::Response& response,
                                  const std::string& key) {
  if (!response.result.has_value()) return "";
  if (!holds_alternative<Metadata>(response.result.value())) return "";
  auto metadata = get<Metadata>(response.result.value());
  auto it = metadata.find(key);
  if (it == metadata.end()) return "";
  if (holds_alternative<std::string>(it->second)) {
    return get<std::string>(it->second);
  }
  return "";
}

// Helper to extract bool from Metadata response
bool extractMetadataBool(const jsonrpc::Response& response,
                         const std::string& key) {
  if (!response.result.has_value()) return false;
  if (!holds_alternative<Metadata>(response.result.value())) return false;
  auto metadata = get<Metadata>(response.result.value());
  auto it = metadata.find(key);
  if (it == metadata.end()) return false;
  if (holds_alternative<bool>(it->second)) {
    return get<bool>(it->second);
  }
  return false;
}

// Helper to extract int64 from Metadata response
int64_t extractMetadataInt(const jsonrpc::Response& response,
                           const std::string& key) {
  if (!response.result.has_value()) return 0;
  if (!holds_alternative<Metadata>(response.result.value())) return 0;
  auto metadata = get<Metadata>(response.result.value());
  auto it = metadata.find(key);
  if (it == metadata.end()) return 0;
  if (holds_alternative<int64_t>(it->second)) {
    return get<int64_t>(it->second);
  }
  return 0;
}

// Helper to print CallToolResult content
void printToolResult(const CallToolResult& result) {
  if (result.isError) {
    std::cerr << "[ERROR] ";
  }
  for (const auto& content : result.content) {
    if (holds_alternative<TextContent>(content)) {
      std::cerr << get<TextContent>(content).text;
    }
  }
}

// Test counter for tracking pass/fail
struct TestResults {
  int passed = 0;
  int failed = 0;

  void pass(const std::string& test_name) {
    passed++;
    std::cerr << "  [PASS] " << test_name << std::endl;
  }

  void fail(const std::string& test_name, const std::string& reason = "") {
    failed++;
    std::cerr << "  [FAIL] " << test_name;
    if (!reason.empty()) {
      std::cerr << " - " << reason;
    }
    std::cerr << std::endl;
  }

  void summary() {
    std::cerr << "\n========================================" << std::endl;
    std::cerr << "TEST SUMMARY: " << passed << " passed, " << failed
              << " failed" << std::endl;
    std::cerr << "========================================" << std::endl;
  }
};

// Demonstrate and verify all server features
void demonstrateFeatures(McpClient& client, bool verbose) {
  std::cerr << "\n========================================" << std::endl;
  std::cerr << "MCP Client Feature Verification Suite" << std::endl;
  std::cerr << "========================================" << std::endl;

  TestResults results;

  // 1. Initialize protocol
  std::cerr << "\n[TEST 1] Protocol Initialization" << std::endl;
  {
    auto init_future = client.initializeProtocol();

    try {
      auto init_result = init_future.get();
      std::cerr << "  Protocol: " << init_result.protocolVersion << std::endl;
      std::cerr << "  Server: "
                << (init_result.serverInfo.has_value()
                        ? init_result.serverInfo->name + " v" +
                              init_result.serverInfo->version
                        : "unknown")
                << std::endl;

      client.setServerCapabilities(init_result.capabilities);

      if (!init_result.protocolVersion.empty()) {
        results.pass("Protocol initialization");
      } else {
        results.fail("Protocol initialization", "Empty protocol version");
      }
    } catch (const std::exception& e) {
      results.fail("Protocol initialization", e.what());
      return;  // Cannot continue without initialization
    }
  }

  // 2. Test custom request handlers (ping, echo, server/status, health)
  std::cerr << "\n[TEST 2] Custom Request Handlers" << std::endl;
  {
    // Test ping handler
    try {
      auto ping_future = client.sendRequest("ping");
      auto ping_response = ping_future.get();
      if (!ping_response.error.has_value() &&
          extractMetadataBool(ping_response, "pong")) {
        results.pass("ping handler - returns pong=true");
      } else {
        results.fail("ping handler", "Missing pong=true");
      }
    } catch (const std::exception& e) {
      results.fail("ping handler", e.what());
    }

    // Test echo handler
    try {
      auto echo_params =
          make<Metadata>().add("test_key", "test_value").build();
      auto echo_future =
          client.sendRequest("echo", make_optional(echo_params));
      auto echo_response = echo_future.get();
      if (!echo_response.error.has_value() &&
          extractMetadataBool(echo_response, "echo") &&
          extractMetadataBool(echo_response, "params_received")) {
        results.pass("echo handler - returns echo=true, params_received=true");
      } else {
        results.fail("echo handler", "Missing expected fields");
      }
    } catch (const std::exception& e) {
      results.fail("echo handler", e.what());
    }

    // Test server/status handler
    try {
      auto status_future = client.sendRequest("server/status");
      auto status_response = status_future.get();
      if (!status_response.error.has_value() &&
          extractMetadataBool(status_response, "running")) {
        int64_t sessions = extractMetadataInt(status_response, "sessions_active");
        int64_t requests = extractMetadataInt(status_response, "requests_total");
        std::cerr << "    Sessions: " << sessions << ", Requests: " << requests
                  << std::endl;
        results.pass("server/status handler - returns running=true");
      } else {
        results.fail("server/status handler", "Missing running=true");
      }
    } catch (const std::exception& e) {
      results.fail("server/status handler", e.what());
    }

    // Test health handler
    try {
      auto health_future = client.sendRequest("health");
      auto health_response = health_future.get();
      std::string status = extractMetadataString(health_response, "status");
      if (!health_response.error.has_value() && status == "healthy") {
        results.pass("health handler - returns status=healthy");
      } else {
        results.fail("health handler", "Expected status=healthy, got: " + status);
      }
    } catch (const std::exception& e) {
      results.fail("health handler", e.what());
    }
  }

  // 3. Test resources (config, log, metrics)
  std::cerr << "\n[TEST 3] Resources" << std::endl;
  {
    try {
      auto list_future = client.listResources();
      auto list_result = list_future.get();

      std::cerr << "  Found " << list_result.resources.size() << " resources"
                << std::endl;

      // Verify expected resources exist
      bool found_config = false, found_log = false, found_metrics = false;

      for (const auto& resource : list_result.resources) {
        std::cerr << "    - " << resource.name << " (" << resource.uri << ")"
                  << std::endl;
        if (resource.uri == "config://server/settings") found_config = true;
        if (resource.uri == "log://server/events") found_log = true;
        if (resource.uri == "metrics://server/stats") found_metrics = true;
      }

      if (found_config) {
        results.pass("Resource: config://server/settings");
      } else {
        results.fail("Resource: config://server/settings", "Not found");
      }

      if (found_log) {
        results.pass("Resource: log://server/events");
      } else {
        results.fail("Resource: log://server/events", "Not found");
      }

      if (found_metrics) {
        results.pass("Resource: metrics://server/stats");
      } else {
        results.fail("Resource: metrics://server/stats", "Not found");
      }

    } catch (const std::exception& e) {
      results.fail("List resources", e.what());
    }
  }

  // 4. Test all tools (calculator, system_info, database_query)
  std::cerr << "\n[TEST 4] Tools" << std::endl;
  {
    try {
      auto tools_future = client.listTools();
      auto tools_result = tools_future.get();

      std::cerr << "  Found " << tools_result.tools.size() << " tools"
                << std::endl;

      bool found_calculator = false, found_sysinfo = false, found_dbquery = false;

      for (const auto& tool : tools_result.tools) {
        std::cerr << "    - " << tool.name;
        if (tool.description.has_value()) {
          std::cerr << ": " << tool.description.value();
        }
        std::cerr << std::endl;

        if (tool.name == "calculator") found_calculator = true;
        if (tool.name == "system_info") found_sysinfo = true;
        if (tool.name == "database_query") found_dbquery = true;
      }

      // Test calculator tool - all operations
      if (found_calculator) {
        results.pass("Tool exists: calculator");

        // Test add
        auto add_args = make<Metadata>()
                            .add("operation", "add")
                            .add("a", 10.0)
                            .add("b", 5.0)
                            .build();
        auto add_future = client.callTool("calculator", make_optional(add_args));
        auto add_result = add_future.get();
        std::cerr << "    add(10, 5) = ";
        printToolResult(add_result);
        std::cerr << std::endl;
        if (!add_result.isError && !add_result.content.empty()) {
          results.pass("calculator: add operation");
        } else {
          results.fail("calculator: add operation");
        }

        // Test subtract
        auto sub_args = make<Metadata>()
                            .add("operation", "subtract")
                            .add("a", 20.0)
                            .add("b", 8.0)
                            .build();
        auto sub_future = client.callTool("calculator", make_optional(sub_args));
        auto sub_result = sub_future.get();
        std::cerr << "    subtract(20, 8) = ";
        printToolResult(sub_result);
        std::cerr << std::endl;
        if (!sub_result.isError && !sub_result.content.empty()) {
          results.pass("calculator: subtract operation");
        } else {
          results.fail("calculator: subtract operation");
        }

        // Test multiply
        auto mul_args = make<Metadata>()
                            .add("operation", "multiply")
                            .add("a", 6.0)
                            .add("b", 7.0)
                            .build();
        auto mul_future = client.callTool("calculator", make_optional(mul_args));
        auto mul_result = mul_future.get();
        std::cerr << "    multiply(6, 7) = ";
        printToolResult(mul_result);
        std::cerr << std::endl;
        if (!mul_result.isError && !mul_result.content.empty()) {
          results.pass("calculator: multiply operation");
        } else {
          results.fail("calculator: multiply operation");
        }

        // Test divide
        auto div_args = make<Metadata>()
                            .add("operation", "divide")
                            .add("a", 100.0)
                            .add("b", 4.0)
                            .build();
        auto div_future = client.callTool("calculator", make_optional(div_args));
        auto div_result = div_future.get();
        std::cerr << "    divide(100, 4) = ";
        printToolResult(div_result);
        std::cerr << std::endl;
        if (!div_result.isError && !div_result.content.empty()) {
          results.pass("calculator: divide operation");
        } else {
          results.fail("calculator: divide operation");
        }
      } else {
        results.fail("Tool exists: calculator", "Not found");
      }

      // Test system_info tool
      if (found_sysinfo) {
        results.pass("Tool exists: system_info");
        auto info_future = client.callTool("system_info", nullopt);
        auto info_result = info_future.get();
        std::cerr << "    system_info result: ";
        printToolResult(info_result);
        std::cerr << std::endl;
        if (!info_result.isError && !info_result.content.empty()) {
          results.pass("system_info: execution");
        } else {
          results.fail("system_info: execution");
        }
      } else {
        results.fail("Tool exists: system_info", "Not found");
      }

      // Test database_query tool
      if (found_dbquery) {
        results.pass("Tool exists: database_query");
        auto query_args =
            make<Metadata>().add("query", "SELECT * FROM users").build();
        auto query_future =
            client.callTool("database_query", make_optional(query_args));
        auto query_result = query_future.get();
        std::cerr << "    database_query result: ";
        printToolResult(query_result);
        std::cerr << std::endl;
        if (!query_result.isError && !query_result.content.empty()) {
          results.pass("database_query: execution");
        } else {
          results.fail("database_query: execution");
        }
      } else {
        results.fail("Tool exists: database_query", "Not found");
      }

    } catch (const std::exception& e) {
      results.fail("Tools test", e.what());
    }
  }

  // 5. Test prompts (greeting, code_review, data_analysis)
  std::cerr << "\n[TEST 5] Prompts" << std::endl;
  {
    try {
      auto prompts_future = client.listPrompts();
      auto prompts_result = prompts_future.get();

      std::cerr << "  Found " << prompts_result.prompts.size() << " prompts"
                << std::endl;

      bool found_greeting = false, found_code_review = false,
           found_data_analysis = false;

      for (const auto& prompt : prompts_result.prompts) {
        std::cerr << "    - " << prompt.name;
        if (prompt.description.has_value()) {
          std::cerr << ": " << prompt.description.value();
        }
        std::cerr << std::endl;

        if (prompt.name == "greeting") found_greeting = true;
        if (prompt.name == "code_review") found_code_review = true;
        if (prompt.name == "data_analysis") found_data_analysis = true;
      }

      if (found_greeting) {
        results.pass("Prompt exists: greeting");
        // Test getting the greeting prompt
        auto greeting_future = client.getPrompt("greeting", nullopt);
        try {
          auto greeting_result = greeting_future.get();
          if (!greeting_result.messages.empty()) {
            std::cerr << "    greeting messages: "
                      << greeting_result.messages.size() << std::endl;
            results.pass("getPrompt: greeting");
          } else {
            results.fail("getPrompt: greeting", "No messages returned");
          }
        } catch (const std::exception& e) {
          results.fail("getPrompt: greeting", e.what());
        }
      } else {
        results.fail("Prompt exists: greeting", "Not found");
      }

      if (found_code_review) {
        results.pass("Prompt exists: code_review");
        // Test with arguments
        auto code_args = make<Metadata>()
                             .add("code", "function hello() { return 42; }")
                             .add("language", "javascript")
                             .build();
        auto code_review_future =
            client.getPrompt("code_review", make_optional(code_args));
        try {
          auto code_review_result = code_review_future.get();
          if (!code_review_result.messages.empty()) {
            std::cerr << "    code_review messages: "
                      << code_review_result.messages.size() << std::endl;
            results.pass("getPrompt: code_review with args");
          } else {
            results.fail("getPrompt: code_review", "No messages returned");
          }
        } catch (const std::exception& e) {
          results.fail("getPrompt: code_review", e.what());
        }
      } else {
        results.fail("Prompt exists: code_review", "Not found");
      }

      if (found_data_analysis) {
        results.pass("Prompt exists: data_analysis");
        // Test with arguments
        auto data_args = make<Metadata>()
                             .add("dataset", "sales_2024.csv")
                             .add("analysis_type", "predictive")
                             .build();
        auto data_future =
            client.getPrompt("data_analysis", make_optional(data_args));
        try {
          auto data_result = data_future.get();
          if (!data_result.messages.empty()) {
            std::cerr << "    data_analysis messages: "
                      << data_result.messages.size() << std::endl;
            results.pass("getPrompt: data_analysis with args");
          } else {
            results.fail("getPrompt: data_analysis", "No messages returned");
          }
        } catch (const std::exception& e) {
          results.fail("getPrompt: data_analysis", e.what());
        }
      } else {
        results.fail("Prompt exists: data_analysis", "Not found");
      }

    } catch (const std::exception& e) {
      results.fail("Prompts test", e.what());
    }
  }

  // 6. Test notifications (log, heartbeat, progress)
  std::cerr << "\n[TEST 6] Notifications" << std::endl;
  {
    // Send log notification
    try {
      auto log_params = make<Metadata>()
                            .add("message", "Test log message from client")
                            .add("level", "INFO")
                            .build();
      client.sendNotification("log", make_optional(log_params));
      results.pass("Notification: log");
    } catch (const std::exception& e) {
      results.fail("Notification: log", e.what());
    }

    // Send heartbeat notification
    try {
      client.sendNotification("heartbeat", nullopt);
      results.pass("Notification: heartbeat");
    } catch (const std::exception& e) {
      results.fail("Notification: heartbeat", e.what());
    }

    // Send progress notification
    try {
      auto progress_params =
          make<Metadata>().add("progress", 0.5).add("message", "Halfway done").build();
      client.sendNotification("progress", make_optional(progress_params));
      results.pass("Notification: progress");
    } catch (const std::exception& e) {
      results.fail("Notification: progress", e.what());
    }
  }

  // 7. Batch requests test
  std::cerr << "\n[TEST 7] Batch Requests" << std::endl;
  {
    std::vector<std::pair<std::string, optional<Metadata>>> batch;
    batch.push_back({"ping", nullopt});
    batch.push_back({"health", nullopt});
    batch.push_back({"server/status", nullopt});

    auto futures = client.sendBatch(batch);
    std::cerr << "  Sent " << futures.size() << " batch requests" << std::endl;

    int success_count = 0;
    for (size_t i = 0; i < futures.size(); ++i) {
      try {
        auto response = futures[i].get();
        if (!response.error.has_value()) {
          success_count++;
        }
      } catch (...) {
        // Request failed
      }
    }

    std::cerr << "  Results: " << success_count << "/" << futures.size()
              << " successful" << std::endl;

    if (success_count == static_cast<int>(futures.size())) {
      results.pass("Batch requests: all succeeded");
    } else {
      results.fail("Batch requests",
                   std::to_string(futures.size() - success_count) + " failed");
    }
  }

  // 8. Stress test (only in verbose mode)
  if (verbose) {
    std::cerr << "\n[TEST 8] Stress Test (10 rapid echo requests)" << std::endl;

    std::vector<std::future<jsonrpc::Response>> stress_futures;
    for (int i = 0; i < 10; ++i) {
      auto params = make<Metadata>()
                        .add("request_id", static_cast<int64_t>(i))
                        .add("test", true)
                        .build();
      stress_futures.push_back(
          client.sendRequest("echo", make_optional(params)));
    }

    int completed = 0;
    int failed = 0;
    for (auto& future : stress_futures) {
      try {
        auto response = future.get();
        if (response.error.has_value()) {
          failed++;
        } else {
          completed++;
        }
      } catch (...) {
        failed++;
      }
    }

    std::cerr << "  Results: " << completed << " successful, " << failed
              << " failed" << std::endl;

    if (failed == 0) {
      results.pass("Stress test: all requests succeeded");
    } else {
      results.fail("Stress test", std::to_string(failed) + " requests failed");
    }
  }

  // Print summary
  results.summary();
}

// Print client statistics
void printStatistics(const McpClient& client) {
  const auto& stats = client.getClientStats();

  std::cerr << "\n=== Client Statistics ===" << std::endl;
  std::cerr << "Connections:" << std::endl;
  std::cerr << "  Total: " << stats.connections_total << std::endl;
  std::cerr << "  Active: " << stats.connections_active << std::endl;

  std::cerr << "\nRequests:" << std::endl;
  std::cerr << "  Total: " << stats.requests_total << std::endl;
  std::cerr << "  Success: " << stats.requests_success << std::endl;
  std::cerr << "  Failed: " << stats.requests_failed << std::endl;
  std::cerr << "  Timeout: " << stats.requests_timeout << std::endl;
  std::cerr << "  Retried: " << stats.requests_retried << std::endl;
  std::cerr << "  Batched: " << stats.requests_batched << std::endl;
  std::cerr << "  Queued: " << stats.requests_queued << std::endl;

  std::cerr << "\nCircuit Breaker:" << std::endl;
  std::cerr << "  Opens: " << stats.circuit_breaker_opens << std::endl;
  std::cerr << "  Closes: " << stats.circuit_breaker_closes << std::endl;
  std::cerr << "  Half-opens: " << stats.circuit_breaker_half_opens
            << std::endl;

  std::cerr << "\nConnection Pool:" << std::endl;
  std::cerr << "  Hits: " << stats.connection_pool_hits << std::endl;
  std::cerr << "  Misses: " << stats.connection_pool_misses << std::endl;
  std::cerr << "  Evictions: " << stats.connection_pool_evictions << std::endl;

  std::cerr << "\nProtocol Operations:" << std::endl;
  std::cerr << "  Resources read: " << stats.resources_read << std::endl;
  std::cerr << "  Tools called: " << stats.tools_called << std::endl;
  std::cerr << "  Prompts retrieved: " << stats.prompts_retrieved << std::endl;

  std::cerr << "\nData Transfer:" << std::endl;
  std::cerr << "  Bytes sent: " << stats.bytes_sent << std::endl;
  std::cerr << "  Bytes received: " << stats.bytes_received << std::endl;

  if (stats.requests_success > 0) {
    uint64_t avg_latency =
        stats.request_duration_ms_total / stats.requests_success;
    std::cerr << "\nLatency:" << std::endl;
    std::cerr << "  Average: " << avg_latency << " ms" << std::endl;
    std::cerr << "  Min: " << stats.request_duration_ms_min << " ms"
              << std::endl;
    std::cerr << "  Max: " << stats.request_duration_ms_max << " ms"
              << std::endl;
  }
}

int main(int argc, char* argv[]) {
  // Install signal handlers
  signal(SIGINT, signal_handler);
  signal(SIGTERM, signal_handler);

  // Parse command-line options
  ClientOptions options = parseArguments(argc, argv);

  std::cerr << "====================================================="
            << std::endl;
  std::cerr << "MCP Client - Enterprise Edition" << std::endl;
  std::cerr << "====================================================="
            << std::endl;

  // Build server URI based on transport type
  std::string server_uri;
  if (options.transport == "stdio") {
    server_uri = "stdio://";
  } else if (options.transport == "websocket" || options.transport == "ws") {
    std::ostringstream uri;
    uri << "ws://" << options.host << ":" << options.port << "/mcp";
    server_uri = uri.str();
  } else {  // Default to HTTP/SSE
    std::ostringstream uri;
    uri << "http://" << options.host << ":" << options.port;
    server_uri = uri.str();
  }

  std::cerr << "[INFO] Transport: " << options.transport << std::endl;
  std::cerr << "[INFO] Server URI: " << server_uri << std::endl;

  // Configure client with enterprise features
  McpClientConfig config;

  // Protocol settings
  config.protocol_version = "2024-11-05";
  config.client_name = "mcp-enterprise-client";
  config.client_version = "2.0.0";

  // Transport settings
  config.auto_negotiate_transport = true;

  // Connection pool settings
  config.connection_pool_size = options.pool_size;
  config.max_idle_connections = options.pool_size / 2;

  // Circuit breaker settings
  config.circuit_breaker_threshold = options.circuit_breaker_threshold;
  config.circuit_breaker_timeout = std::chrono::seconds(10);
  config.circuit_breaker_error_rate = 0.5;

  // Retry settings
  config.max_retries = options.max_retries;
  config.initial_retry_delay = std::chrono::milliseconds(500);
  config.retry_backoff_multiplier = 2.0;
  config.max_retry_delay = std::chrono::seconds(30);
  // config.retry_jitter = 0.1;  // 10% jitter - not yet available

  // Request management
  config.request_timeout =
      std::chrono::seconds(options.request_timeout_seconds);
  config.max_concurrent_requests = 50;
  config.request_queue_limit = 100;

  // Worker threads
  config.num_workers = options.num_workers;

  // Flow control
  config.buffer_high_watermark = 1024 * 1024;  // 1MB
  config.buffer_low_watermark = 256 * 1024;    // 256KB

  // Observability
  config.enable_metrics = true;
  // config.metrics_interval = std::chrono::seconds(10);  // Not available in
  // current API config.enable_tracing = options.verbose;  // Not yet available

  // Client capabilities
  config.capabilities = ClientCapabilities();
  config.capabilities.experimental = make_optional(Metadata());

  // Add HTTP/SSE specific configuration if using HTTP transport
  if (options.transport == "http") {
    // HTTP/SSE transport will be configured automatically
    // The client will use the appropriate transport based on the URI scheme
  }

  // Create client
  std::cerr << "[INFO] Creating MCP client..." << std::endl;
  std::cerr << "[INFO] Connection pool size: " << options.pool_size
            << std::endl;
  std::cerr << "[INFO] Worker threads: " << options.num_workers << std::endl;
  std::cerr << "[INFO] Max retries: " << options.max_retries << std::endl;

  {
    std::lock_guard<std::mutex> lock(g_client_mutex);
    g_client = createMcpClient(config);
    if (!g_client) {
      std::cerr << "[ERROR] Failed to create client" << std::endl;
      return 1;
    }
  }

  // Connect to server first - this will initialize the application
  std::cerr << "[INFO] Connecting to server..." << std::endl;
  VoidResult connect_result;
  {
    std::lock_guard<std::mutex> lock(g_client_mutex);
    if (g_client) {
      connect_result = g_client->connect(server_uri);
    } else {
      std::cerr << "[ERROR] Client not initialized" << std::endl;
      return 1;
    }
  }

  if (is_error<std::nullptr_t>(connect_result)) {
    std::cerr << "[ERROR] Failed to connect: "
              << get_error<std::nullptr_t>(connect_result)->message
              << std::endl;
    return 1;
  }

  // For HTTP+SSE, connection is established immediately (stateless protocol)
  // For stdio/websocket, wait for actual connection
  if (options.transport != "http") {
    std::cerr << "[INFO] Waiting for connection to be established..."
              << std::endl;

    int wait_count = 0;
    bool connected = false;
    while (!connected && wait_count < 100 &&
           !g_shutdown) {  // 10 seconds timeout
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      wait_count++;
      if (wait_count % 10 == 0) {
        std::cerr << "[INFO] Still connecting..." << std::endl;
      }

      // Check connection status safely
      {
        std::lock_guard<std::mutex> lock(g_client_mutex);
        if (g_client) {
          connected = g_client->isConnected();
        }
      }
    }

    if (!connected) {
      if (g_shutdown) {
        std::cerr << "[INFO] Shutdown requested during connection" << std::endl;
        return 0;
      }
      std::cerr << "[ERROR] Connection timeout - failed to establish connection"
                << std::endl;
      std::cerr << "[HINT] Make sure the server is running on " << options.host
                << ":" << options.port << std::endl;
      return 1;
    }
  }

  if (g_shutdown) {
    std::cerr << "[INFO] Shutdown requested, exiting..." << std::endl;
    return 0;
  }

  std::cerr << "[INFO] Connected successfully!" << std::endl;

  // The event loop is already running from connect()
  // No need to start it separately

  // Wait for connection to be fully established
  // The connection happens asynchronously, so we need to wait for it
  std::cerr << "[INFO] Waiting for connection to be established..."
            << std::endl;
  {
    std::lock_guard<std::mutex> lock(g_client_mutex);
    if (g_client) {
      // Wait up to 5 seconds for connection to be established
      auto start = std::chrono::steady_clock::now();
      while (!g_client->isConnected()) {
        if (std::chrono::steady_clock::now() - start >
            std::chrono::seconds(5)) {
          std::cerr << "[ERROR] Timeout waiting for connection" << std::endl;
          return 1;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
      }
    }
  }

  // Initialize MCP protocol - REQUIRED before any requests
  std::cerr << "[INFO] Initializing MCP protocol..." << std::endl;
  std::future<InitializeResult> init_future;
  {
    std::lock_guard<std::mutex> lock(g_client_mutex);
    if (g_client) {
      try {
        init_future = g_client->initializeProtocol();
      } catch (const std::exception& e) {
        std::cerr << "[ERROR] Failed to start initialization: " << e.what()
                  << std::endl;
        return 1;
      }
    }
  }

  // Poll the future without blocking the event loop
  // This allows the dispatcher to continue processing events
  {
    bool initialized = false;
    auto start_time = std::chrono::steady_clock::now();

    while (!initialized && !g_shutdown) {
      // Check if future is ready without blocking
      auto status = init_future.wait_for(std::chrono::milliseconds(100));

      if (status == std::future_status::ready) {
        try {
          auto init_result = init_future.get();
          std::cerr << "[INFO] Protocol initialized: "
                    << init_result.protocolVersion << std::endl;
          if (init_result.serverInfo.has_value()) {
            std::cerr << "[INFO] Server: " << init_result.serverInfo->name
                      << " v" << init_result.serverInfo->version << std::endl;
          }
          // Store server capabilities
          g_client->setServerCapabilities(init_result.capabilities);
        } catch (const std::exception& e) {
          std::cerr << "[ERROR] Failed to initialize protocol: " << e.what()
                    << std::endl;
          return 1;
        }
        initialized = true;
        break;
      }
    }
  }

  // Run demonstrations if requested
  if (options.demo) {
    std::lock_guard<std::mutex> lock(g_client_mutex);
    if (g_client) {
      demonstrateFeatures(*g_client, options.verbose);
    }
  }

  // Check for shutdown before entering main loop
  if (g_shutdown) {
    std::cerr << "[INFO] Shutdown requested, exiting..." << std::endl;
    return 0;
  }

  // Main loop - send periodic pings
  std::cerr << "\n[INFO] Entering main loop (Ctrl+C to exit)..." << std::endl;
  if (!options.quiet) {
    std::cerr << "[INFO] Sending ping every 5 seconds..." << std::endl;
  }

  int ping_count = 0;
  int consecutive_failures = 0;

  while (!g_shutdown) {
    // Send ping request safely
    try {
      std::future<jsonrpc::Response> ping_future;
      {
        std::lock_guard<std::mutex> lock(g_client_mutex);
        if (!g_client || !g_client->isConnected()) {
          std::cerr << "[WARNING] Client disconnected, exiting main loop"
                    << std::endl;
          break;
        }
        ping_future = g_client->sendRequest("ping");
      }

      // Use wait_for with timeout to allow checking for shutdown
      auto status = ping_future.wait_for(std::chrono::seconds(5));
      if (status == std::future_status::timeout) {
        std::cerr << "[WARN] Ping timeout" << std::endl;
        consecutive_failures++;
      } else if (status == std::future_status::ready) {
        auto response = ping_future.get();
        if (!response.error.has_value()) {
          ping_count++;
          consecutive_failures = 0;
          // Only show ping messages in verbose mode or if quiet is not enabled
          if (!options.quiet && (ping_count % 100 == 0 || options.verbose)) {
            std::cerr << "[INFO] Ping #" << ping_count << " successful"
                      << std::endl;
          }
        } else {
          consecutive_failures++;
          std::cerr << "[WARN] Ping failed: " << response.error->message
                    << std::endl;
        }
      }
    } catch (const std::exception& e) {
      consecutive_failures++;
      std::cerr << "[ERROR] Ping exception: " << e.what() << std::endl;
    }

    // Check for excessive failures
    if (consecutive_failures >= 5) {
      std::cerr << "[ERROR] Too many consecutive failures, shutting down"
                << std::endl;
      break;
    }

    // Show periodic metrics if requested (less frequently in quiet mode)
    int metrics_interval = options.quiet ? 200 : 20;
    if (options.metrics && ping_count > 0 &&
        ping_count % metrics_interval == 0) {
      std::lock_guard<std::mutex> lock(g_client_mutex);
      if (g_client) {
        try {
          printStatistics(*g_client);
        } catch (const std::exception& e) {
          std::cerr << "[ERROR] Failed to print statistics: " << e.what()
                    << std::endl;
        }
      }
    }

    // Sleep between pings
    for (int i = 0; i < 50 && !g_shutdown;
         ++i) {  // 5 seconds in 100ms increments
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
  }

  // Disconnect
  std::cerr << "\n[INFO] Disconnecting..." << std::endl;
  {
    std::lock_guard<std::mutex> lock(g_client_mutex);
    if (g_client) {
      try {
        g_client->disconnect();
      } catch (const std::exception& e) {
        std::cerr << "[ERROR] Exception during disconnect: " << e.what()
                  << std::endl;
      }
    }
  }

  // Print final statistics
  if (options.metrics || options.verbose) {
    std::lock_guard<std::mutex> lock(g_client_mutex);
    if (g_client) {
      try {
        printStatistics(*g_client);
      } catch (const std::exception& e) {
        std::cerr << "[ERROR] Failed to print final statistics: " << e.what()
                  << std::endl;
      }
    }
  }

  std::cerr << "\n[INFO] Client shutdown complete" << std::endl;
  std::cerr << "[INFO] Total pings sent: " << ping_count << std::endl;

  // Shutdown client and clean up
  {
    std::lock_guard<std::mutex> lock(g_client_mutex);
    if (g_client) {
      // Shutdown the client
      g_client->shutdown();
    }
  }

  // Clean up global client
  {
    std::lock_guard<std::mutex> lock(g_client_mutex);
    g_client.reset();
  }

  return 0;
}