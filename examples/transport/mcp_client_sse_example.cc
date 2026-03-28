/**
 * @file mcp_client_sse_example.cc
 * @brief MCP Client SSE Transport Example
 *
 * This example demonstrates how to use the MCP client with HTTP+SSE
 * (Server-Sent Events) transport to:
 *   1. Connect to an MCP server via HTTPS/SSE
 *   2. Initialize the MCP protocol
 *   3. List available tools
 *   4. Call a tool with arguments
 *
 * SSE Transport Architecture:
 * ===========================
 *
 *   ┌─────────────────┐
 *   │   McpClient     │
 *   └────────┬────────┘
 *            │ connect("https://server/sse")
 *            ▼
 *   ┌─────────────────┐
 *   │ ConnectionMgr   │
 *   └────────┬────────┘
 *            │
 *   ┌────────┴────────┐
 *   │  Filter Chain   │
 *   │  ┌───────────┐  │
 *   │  │HTTP Codec │  │  ← HTTP/1.1 request/response
 *   │  └─────┬─────┘  │
 *   │  ┌─────┴─────┐  │
 *   │  │SSE Codec  │  │  ← Server-Sent Events parsing
 *   │  └─────┬─────┘  │
 *   │  ┌─────┴─────┐  │
 *   │  │JSON-RPC   │  │  ← MCP message handling
 *   │  └───────────┘  │
 *   └────────┬────────┘
 *            │
 *   ┌────────┴────────┐
 *   │  SSL Transport  │  ← TLS encryption (for HTTPS)
 *   └────────┬────────┘
 *            │
 *   ┌────────┴────────┐
 *   │  TCP Socket     │
 *   └─────────────────┘
 *
 * SSE Communication Pattern:
 * ==========================
 *
 *   Client                              Server
 *     │                                   │
 *     │──── GET /sse (SSE stream) ───────▶│
 *     │◀─── HTTP 200 + SSE events ────────│
 *     │     (endpoint: /messages)         │
 *     │                                   │
 *     │──── POST /messages ──────────────▶│  (initialize)
 *     │◀─── SSE event: init response ─────│
 *     │                                   │
 *     │──── POST /messages ──────────────▶│  (tools/list)
 *     │◀─── SSE event: tools response ────│
 *     │                                   │
 *     │──── POST /messages ──────────────▶│  (tools/call)
 *     │◀─── SSE event: call response ─────│
 *     │                                   │
 *
 * USAGE:
 *   ./mcp_client_sse_example <server_url>
 *
 * EXAMPLES:
 *   # Connect to local server
 *   ./mcp_client_sse_example http://localhost:8080/sse
 *
 *   # Connect to HTTPS server
 *   ./mcp_client_sse_example https://mcp.example.com/v1/sse
 *
 *   # Connect to test server
 *   ./mcp_client_sse_example
 * https://mcp-test.gopher.security/v1/mcp/servers/xxx/sse
 */

#include <chrono>
#include <cstring>
#include <future>
#include <iostream>
#include <string>
#include <thread>

#include "mcp/client/mcp_client.h"
#include "mcp/logging/log_macros.h"
#include "mcp/logging/log_sink.h"
#include "mcp/logging/logger_registry.h"
#include "mcp/types.h"

using namespace mcp;
using namespace mcp::client;

namespace logging = mcp::logging;

// =============================================================================
// Logging Setup
// =============================================================================

void setupLogging(bool verbose) {
  auto& registry = logging::LoggerRegistry::instance();

  // Set base log level
  if (verbose) {
    registry.setGlobalLevel(logging::LogLevel::Debug);
  } else {
    registry.setGlobalLevel(logging::LogLevel::Info);
  }

  // Create console sink
  auto console_sink = logging::SinkFactory::createStdioSink(true);

  // Create application logger
  auto logger = registry.getOrCreateLogger("sse_example");
  logger->setSink(std::move(console_sink));
  logger->setLevel(verbose ? logging::LogLevel::Debug
                           : logging::LogLevel::Info);
}

// =============================================================================
// Helper Functions
// =============================================================================

void printUsage(const char* program) {
  std::cerr << "MCP Client SSE Transport Example\n\n";
  std::cerr << "USAGE:\n";
  std::cerr << "  " << program << " <server_url> [options]\n\n";
  std::cerr << "ARGUMENTS:\n";
  std::cerr << "  server_url    MCP server URL with SSE endpoint\n";
  std::cerr << "                Examples:\n";
  std::cerr << "                  http://localhost:8080/sse\n";
  std::cerr << "                  https://mcp.example.com/v1/sse\n\n";
  std::cerr << "OPTIONS:\n";
  std::cerr << "  --verbose     Enable verbose logging\n";
  std::cerr << "  --tool <name> Tool to call (default: none)\n";
  std::cerr << "  --help        Show this help message\n\n";
  std::cerr << "EXAMPLES:\n";
  std::cerr << "  # List tools from local server\n";
  std::cerr << "  " << program << " http://localhost:8080/sse\n\n";
  std::cerr << "  # Call a specific tool with verbose output\n";
  std::cerr << "  " << program
            << " https://mcp.example.com/sse --tool calculator --verbose\n";
}

void printSeparator(const std::string& title) {
  std::cout << "\n";
  std::cout
      << "═══════════════════════════════════════════════════════════════\n";
  std::cout << "  " << title << "\n";
  std::cout
      << "═══════════════════════════════════════════════════════════════\n";
}

void printToolResult(const CallToolResult& result) {
  if (result.isError) {
    std::cout << "  [ERROR] ";
  } else {
    std::cout << "  [RESULT] ";
  }

  for (const auto& content : result.content) {
    if (holds_alternative<TextContent>(content)) {
      std::cout << get<TextContent>(content).text;
    } else if (holds_alternative<ImageContent>(content)) {
      std::cout << "[Image: " << get<ImageContent>(content).mimeType << "]";
    }
  }
  std::cout << "\n";
}

// =============================================================================
// Main Example
// =============================================================================

int main(int argc, char* argv[]) {
  // Parse arguments
  std::string server_url;
  std::string tool_to_call;
  bool verbose = false;

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];

    if (arg == "--help" || arg == "-h") {
      printUsage(argv[0]);
      return 0;
    } else if (arg == "--verbose" || arg == "-v") {
      verbose = true;
    } else if ((arg == "--tool" || arg == "-t") && i + 1 < argc) {
      tool_to_call = argv[++i];
    } else if (arg[0] != '-') {
      server_url = arg;
    } else {
      std::cerr << "Unknown option: " << arg << "\n";
      printUsage(argv[0]);
      return 1;
    }
  }

  if (server_url.empty()) {
    std::cerr << "Error: Server URL is required\n\n";
    printUsage(argv[0]);
    return 1;
  }

  // Setup logging
  setupLogging(verbose);

  std::cout << "MCP Client SSE Transport Example\n";
  std::cout << "Server: " << server_url << "\n";

  // =========================================================================
  // Step 1: Configure the MCP Client
  // =========================================================================
  printSeparator("Step 1: Configure MCP Client");

  McpClientConfig config;

  // Client identification
  config.client_name = "mcp-sse-example";
  config.client_version = "1.0.0";
  config.protocol_version = "2024-11-05";

  // Transport settings
  // The transport type is auto-detected from the URL:
  //   - http:// or https:// with /sse or /events path → HttpSse
  //   - http:// or https:// with other paths → StreamableHttp
  //   - stdio:// → Stdio
  config.preferred_transport = TransportType::HttpSse;
  config.auto_negotiate_transport = true;

  // Timeout settings
  config.request_timeout = std::chrono::milliseconds(30000);  // 30 seconds
  config.protocol_initialization_timeout = std::chrono::milliseconds(30000);
  config.protocol_connection_timeout = std::chrono::milliseconds(10000);

  // Retry settings
  config.max_retries = 3;
  config.initial_retry_delay = std::chrono::milliseconds(1000);
  config.retry_backoff_multiplier = 2.0;
  config.max_retry_delay = std::chrono::milliseconds(30000);

  // Circuit breaker settings (for fault tolerance)
  config.circuit_breaker_threshold = 5;
  config.circuit_breaker_timeout = std::chrono::milliseconds(30000);

  std::cout << "  Client: " << config.client_name << " v"
            << config.client_version << "\n";
  std::cout << "  Protocol: " << config.protocol_version << "\n";
  std::cout << "  Timeout: " << config.request_timeout.count() << "ms\n";

  // =========================================================================
  // Step 2: Create and Connect the Client
  // =========================================================================
  printSeparator("Step 2: Connect to Server");

  auto client = std::make_unique<McpClient>(config);

  std::cout << "  Connecting to: " << server_url << "\n";

  auto connect_result = client->connect(server_url);

  if (is_error<std::nullptr_t>(connect_result)) {
    auto error = get_error<std::nullptr_t>(connect_result);
    std::cerr << "  [FAILED] Connection failed: " << error->message << "\n";
    return 1;
  }

  std::cout << "  [OK] Connection established\n";

  // Wait for connection to stabilize
  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  // =========================================================================
  // Step 3: Initialize MCP Protocol
  // =========================================================================
  printSeparator("Step 3: Initialize MCP Protocol");

  std::cout << "  Sending initialize request...\n";

  try {
    auto init_future = client->initializeProtocol();

    // Wait for initialization with timeout
    auto status = init_future.wait_for(std::chrono::seconds(30));

    if (status == std::future_status::timeout) {
      std::cerr << "  [FAILED] Initialize timed out after 30 seconds\n";
      client->shutdown();
      return 1;
    }

    auto init_result = init_future.get();

    std::cout << "  [OK] Protocol initialized\n";
    std::cout << "  Protocol Version: " << init_result.protocolVersion << "\n";

    if (init_result.serverInfo.has_value()) {
      std::cout << "  Server: " << init_result.serverInfo->name << " v"
                << init_result.serverInfo->version << "\n";
    }

    // Display server capabilities
    std::cout << "  Capabilities:\n";
    if (init_result.capabilities.tools.has_value()) {
      std::cout << "    - tools: "
                << (init_result.capabilities.tools.value() ? "yes" : "no")
                << "\n";
    }
    if (init_result.capabilities.resources.has_value()) {
      std::cout << "    - resources: yes\n";
    }
    if (init_result.capabilities.prompts.has_value()) {
      std::cout << "    - prompts: "
                << (init_result.capabilities.prompts.value() ? "yes" : "no")
                << "\n";
    }

    // Store capabilities for later use
    client->setServerCapabilities(init_result.capabilities);

  } catch (const std::exception& e) {
    std::cerr << "  [FAILED] Initialize error: " << e.what() << "\n";
    client->shutdown();
    return 1;
  }

  // =========================================================================
  // Step 4: List Available Tools
  // =========================================================================
  printSeparator("Step 4: List Available Tools");

  std::cout << "  Requesting tools list...\n";

  try {
    auto tools_future = client->listTools();

    auto status = tools_future.wait_for(std::chrono::seconds(30));

    if (status == std::future_status::timeout) {
      std::cerr << "  [FAILED] listTools timed out\n";
      client->shutdown();
      return 1;
    }

    auto tools_result = tools_future.get();

    std::cout << "  [OK] Found " << tools_result.tools.size() << " tools\n\n";

    for (const auto& tool : tools_result.tools) {
      std::cout << "  ┌─ " << tool.name << "\n";
      if (tool.description.has_value()) {
        std::cout << "  │  Description: " << tool.description.value() << "\n";
      }
      if (tool.inputSchema.has_value()) {
        std::cout << "  │  Input Schema: (defined)\n";
      }
      std::cout << "  └──────────────────────────────\n";
    }

  } catch (const std::exception& e) {
    std::cerr << "  [FAILED] listTools error: " << e.what() << "\n";
    client->shutdown();
    return 1;
  }

  // =========================================================================
  // Step 5: Call a Tool (if specified)
  // =========================================================================
  if (!tool_to_call.empty()) {
    printSeparator("Step 5: Call Tool '" + tool_to_call + "'");

    std::cout << "  Calling tool: " << tool_to_call << "\n";

    try {
      // Build tool arguments
      // For this example, we'll use common argument patterns
      auto args = make<Metadata>();

      // Add example arguments based on common tool patterns
      if (tool_to_call == "calculator") {
        args.add("operation", "add");
        args.add("a", static_cast<int64_t>(10));
        args.add("b", static_cast<int64_t>(20));
        std::cout << "  Arguments: operation=add, a=10, b=20\n";
      } else if (tool_to_call == "echo") {
        args.add("message", "Hello from SSE client!");
        std::cout << "  Arguments: message=\"Hello from SSE client!\"\n";
      } else if (tool_to_call == "system_info") {
        // No arguments needed
        std::cout << "  Arguments: (none)\n";
      } else {
        // Generic - no arguments
        std::cout << "  Arguments: (none)\n";
      }

      auto call_future =
          client->callTool(tool_to_call, mcp::make_optional(args.build()));

      auto status = call_future.wait_for(std::chrono::seconds(30));

      if (status == std::future_status::timeout) {
        std::cerr << "  [FAILED] callTool timed out\n";
        client->shutdown();
        return 1;
      }

      auto call_result = call_future.get();

      std::cout << "\n";
      printToolResult(call_result);

    } catch (const std::exception& e) {
      std::cerr << "  [FAILED] callTool error: " << e.what() << "\n";
      client->shutdown();
      return 1;
    }
  }

  // =========================================================================
  // Step 6: Cleanup
  // =========================================================================
  printSeparator("Cleanup");

  std::cout << "  Disconnecting...\n";
  client->disconnect();
  std::cout << "  Shutting down...\n";
  client->shutdown();
  std::cout << "  [OK] Done\n";

  return 0;
}
