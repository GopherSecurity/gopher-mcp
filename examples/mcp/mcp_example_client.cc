/**
 * @file mcp_example_client.cc
 * @brief Example usage of enterprise-grade MCP client
 * 
 * Demonstrates:
 * - Transport negotiation and connection
 * - Protocol initialization
 * - Resource operations
 * - Tool calling
 * - Batch requests
 * - Progress tracking
 * - Error handling with circuit breaker
 * - Metrics collection
 */

#include "mcp/client/mcp_client.h"
#include <iostream>
#include <signal.h>

using namespace mcp;
using namespace mcp::client;

// Global client for signal handling
std::unique_ptr<McpClient> g_client;
std::atomic<bool> g_shutdown(false);

void signal_handler(int signal) {
  std::cerr << "\n[INFO] Received signal " << signal << ", shutting down..." << std::endl;
  g_shutdown = true;
  if (g_client) {
    g_client->disconnect();
  }
}

// Demonstrate client capabilities
void demonstrateFeatures(McpClient& client) {
  std::cerr << "\n=== Demonstrating MCP Client Features ===" << std::endl;
  
  // 1. Initialize protocol
  {
    std::cerr << "\n[DEMO] Initializing protocol..." << std::endl;
    auto init_future = client.initialize();
    
    try {
      auto init_result = init_future.get();
      std::cerr << "[DEMO] Protocol initialized: " << init_result.protocolVersion << std::endl;
      std::cerr << "[DEMO] Server: " << 
          (init_result.serverInfo.has_value() ? init_result.serverInfo->name : "unknown") << std::endl;
      
      // Store server capabilities
      client.setServerCapabilities(init_result.capabilities);
    } catch (const std::exception& e) {
      std::cerr << "[ERROR] Initialization failed: " << e.what() << std::endl;
      return;
    }
  }
  
  // 2. List available resources
  {
    std::cerr << "\n[DEMO] Listing resources..." << std::endl;
    auto list_future = client.listResources();
    
    try {
      auto list_result = list_future.get();
      std::cerr << "[DEMO] Found " << list_result.resources.size() << " resources" << std::endl;
      
      for (const auto& resource : list_result.resources) {
        std::cerr << "  - " << resource.name << " (" << resource.uri << ")" << std::endl;
        
        // Read first resource as example
        if (list_result.resources.size() > 0) {
          auto read_future = client.readResource(list_result.resources[0].uri);
          try {
            auto read_result = read_future.get();
            std::cerr << "[DEMO] Read resource: " << list_result.resources[0].name << std::endl;
          } catch (const std::exception& e) {
            std::cerr << "[ERROR] Failed to read resource: " << e.what() << std::endl;
          }
        }
      }
    } catch (const std::exception& e) {
      std::cerr << "[ERROR] Failed to list resources: " << e.what() << std::endl;
    }
  }
  
  // 3. List and call tools
  {
    std::cerr << "\n[DEMO] Listing tools..." << std::endl;
    auto tools_future = client.listTools();
    
    try {
      auto tools_result = tools_future.get();
      std::cerr << "[DEMO] Found " << tools_result.tools.size() << " tools" << std::endl;
      
      for (const auto& tool : tools_result.tools) {
        std::cerr << "  - " << tool.name;
        if (tool.description.has_value()) {
          std::cerr << ": " << tool.description.value();
        }
        std::cerr << std::endl;
      }
      
      // Call a tool if available
      if (tools_result.tools.size() > 0) {
        std::cerr << "\n[DEMO] Calling tool: " << tools_result.tools[0].name << std::endl;
        
        auto args = make<Metadata>()
            .add("test_param", "test_value")
            .build();
        
        auto call_future = client.callTool(tools_result.tools[0].name, make_optional(args));
        
        try {
          auto call_result = call_future.get();
          if (!call_result.isError) {
            std::cerr << "[DEMO] Tool executed successfully" << std::endl;
          } else {
            std::cerr << "[DEMO] Tool returned error" << std::endl;
          }
        } catch (const std::exception& e) {
          std::cerr << "[ERROR] Tool call failed: " << e.what() << std::endl;
        }
      }
    } catch (const std::exception& e) {
      std::cerr << "[ERROR] Failed to list tools: " << e.what() << std::endl;
    }
  }
  
  // 4. Batch requests demonstration
  {
    std::cerr << "\n[DEMO] Sending batch requests..." << std::endl;
    
    std::vector<std::pair<std::string, optional<Metadata>>> batch;
    batch.push_back({"ping", nullopt});
    batch.push_back({"tools/list", nullopt});
    batch.push_back({"prompts/list", nullopt});
    
    auto futures = client.sendBatch(batch);
    std::cerr << "[DEMO] Sent " << futures.size() << " batch requests" << std::endl;
    
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
    
    std::cerr << "[DEMO] Batch complete: " << success_count << "/" 
              << futures.size() << " successful" << std::endl;
  }
  
  // 5. Progress tracking demonstration
  {
    std::cerr << "\n[DEMO] Setting up progress tracking..." << std::endl;
    
    // Register progress callback
    auto progress_token = make_progress_token("demo_progress");
    client.trackProgress(progress_token, [](double progress) {
      std::cerr << "[PROGRESS] " << (progress * 100) << "%" << std::endl;
    });
    
    // Simulate operation that reports progress
    // In real scenario, server would send progress notifications
  }
  
  // 6. Stress test with circuit breaker
  {
    std::cerr << "\n[DEMO] Stress testing with rapid requests..." << std::endl;
    
    std::vector<std::future<jsonrpc::Response>> stress_futures;
    for (int i = 0; i < 20; ++i) {
      auto params = make<Metadata>()
          .add("request_id", i)
          .add("test", true)
          .build();
      
      stress_futures.push_back(client.sendRequest("test/stress", make_optional(params)));
    }
    
    // Wait for completion
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
    
    std::cerr << "[DEMO] Stress test complete: " << completed << " successful, " 
              << failed << " failed" << std::endl;
  }
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
  std::cerr << "  Half-opens: " << stats.circuit_breaker_half_opens << std::endl;
  
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
    uint64_t avg_latency = stats.request_duration_ms_total / stats.requests_success;
    std::cerr << "\nLatency:" << std::endl;
    std::cerr << "  Average: " << avg_latency << " ms" << std::endl;
    std::cerr << "  Min: " << stats.request_duration_ms_min << " ms" << std::endl;
    std::cerr << "  Max: " << stats.request_duration_ms_max << " ms" << std::endl;
  }
}

int main(int argc, char* argv[]) {
  // Install signal handlers
  signal(SIGINT, signal_handler);
  signal(SIGTERM, signal_handler);
  
  std::cerr << "=====================================================" << std::endl;
  std::cerr << "MCP Client Example - Enterprise Features Demo" << std::endl;
  std::cerr << "=====================================================" << std::endl;
  
  // Parse command line arguments
  std::string server_uri = "stdio://localhost";
  if (argc > 1) {
    server_uri = argv[1];
  }
  
  // Configure client with enterprise features
  McpClientConfig config;
  
  // Protocol settings
  config.protocol_version = "2024-11-05";
  config.client_name = "mcp-example-client";
  config.client_version = "1.0.0";
  
  // Transport settings
  config.auto_negotiate_transport = true;
  
  // Connection pool settings
  config.connection_pool_size = 5;
  config.max_idle_connections = 2;
  
  // Circuit breaker settings
  config.circuit_breaker_threshold = 3;
  config.circuit_breaker_timeout = std::chrono::seconds(10);
  config.circuit_breaker_error_rate = 0.5;
  
  // Retry settings
  config.max_retries = 3;
  config.initial_retry_delay = std::chrono::milliseconds(500);
  config.retry_backoff_multiplier = 2.0;
  
  // Request management
  config.request_timeout = std::chrono::seconds(30);
  config.max_concurrent_requests = 50;
  
  // Worker threads
  config.num_workers = 2;
  
  // Flow control
  config.buffer_high_watermark = 1024 * 1024;  // 1MB
  config.buffer_low_watermark = 256 * 1024;    // 256KB
  
  // Observability
  config.enable_metrics = true;
  config.metrics_interval = std::chrono::seconds(10);
  
  // Client capabilities
  config.capabilities = ClientCapabilities();
  // TODO: Set specific capabilities
  
  // Create client
  std::cerr << "[INFO] Creating MCP client..." << std::endl;
  g_client = createMcpClient(config);
  
  // Connect to server
  std::cerr << "[INFO] Connecting to: " << server_uri << std::endl;
  auto connect_result = g_client->connect(server_uri);
  
  if (is_error<std::nullptr_t>(connect_result)) {
    std::cerr << "[ERROR] Failed to connect: " 
              << get_error<std::nullptr_t>(connect_result)->message << std::endl;
    return 1;
  }
  
  // Connection initiated - for HTTP+SSE, wait for actual connection
  // The connect() call returns immediately for async transports
  std::cerr << "[INFO] Connection initiated, waiting for connection..." << std::endl;
  
  // Wait for connection to be established (with timeout)
  int wait_count = 0;
  while (!g_client->isConnected() && wait_count < 50) {  // 5 seconds timeout
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    wait_count++;
  }
  
  if (!g_client->isConnected()) {
    std::cerr << "[ERROR] Connection timeout - failed to establish connection" << std::endl;
    return 1;
  }
  
  std::cerr << "[INFO] Connected successfully" << std::endl;
  
  // Run demonstrations
  if (argc > 2 && std::string(argv[2]) == "--demo") {
    demonstrateFeatures(*g_client);
  }
  
  // Main loop - send periodic pings
  std::cerr << "\n[INFO] Entering main loop (Ctrl+C to exit)..." << std::endl;
  
  int ping_count = 0;
  while (!g_shutdown) {
    // Send ping request
    auto ping_future = g_client->sendRequest("ping");
    
    try {
      auto response = ping_future.get();
      if (!response.error.has_value()) {
        ping_count++;
        if (ping_count % 10 == 0) {
          std::cerr << "[INFO] Ping #" << ping_count << " successful" << std::endl;
        }
      }
    } catch (...) {
      // Ping failed, will be retried by client
    }
    
    // Sleep between pings
    std::this_thread::sleep_for(std::chrono::seconds(5));
  }
  
  // Disconnect
  std::cerr << "\n[INFO] Disconnecting..." << std::endl;
  g_client->disconnect();
  
  // Print final statistics
  printStatistics(*g_client);
  
  std::cerr << "\n[INFO] Client shutdown complete" << std::endl;
  
  return 0;
}