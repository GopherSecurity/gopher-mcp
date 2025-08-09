/**
 * @file tcp_echo_client_basic.cc
 * @brief Basic MCP echo client using TCP transport
 * 
 * This demonstrates the transport-agnostic echo client working with
 * TCP transport instead of stdio transport, showing how the same
 * base classes work transparently with different transports.
 * 
 * Architecture:
 * - Uses same EchoClientBase as stdio version
 * - TCP transport handles network socket communication
 * - Async request/response tracking with futures
 * - Demonstrates transport abstraction in action
 * 
 * Usage:
 *   ./tcp_echo_client_basic [host] [port] [requests]
 *   Default: localhost 8080 5
 * 
 * Example:
 *   ./tcp_echo_client_basic localhost 9000 10
 */

#include "mcp/echo/echo_basic.h"
#include "mcp/echo/tcp_transport.h"
#include "mcp/json/json_serialization.h"
#include <iostream>
#include <signal.h>
#include <memory>
#include <thread>
#include <chrono>
#include <vector>
#include <string>
#include <cstdlib>

namespace mcp {
namespace examples {

// Global client instance for signal handling
std::unique_ptr<echo::EchoClientBase> g_client;
std::atomic<bool> g_shutdown(false);

void signalHandler(int signal) {
  if (signal == SIGINT || signal == SIGTERM) {
    std::cerr << "\n[INFO] Received signal " << signal 
              << ", shutting down..." << std::endl;
    g_shutdown = true;
    if (g_client) {
      g_client->stop();
    }
  }
}

/**
 * Custom TCP Echo Client
 * 
 * Inherits from EchoClientBase and adds TCP-specific functionality
 */
class TCPEchoClient : public echo::EchoClientBase {
public:
  TCPEchoClient(echo::EchoTransportBasePtr transport, 
                const std::string& host, int port)
      : echo::EchoClientBase(std::move(transport), createConfig(host, port)),
        host_(host), port_(port) {}

protected:
  void handleNotification(const jsonrpc::Notification& notification) override {
    logInfo("Received notification: " + notification.method);
    
    // Handle server.ready notification
    if (notification.method == "server.ready") {
      logInfo("Server is ready for requests");
    }
  }

private:
  static Config createConfig(const std::string& host, int port) {
    Config config;
    config.client_name = "TCP Echo Client (" + host + ":" + std::to_string(port) + ")";
    config.enable_logging = true;
    config.request_timeout = std::chrono::milliseconds(10000); // 10 second timeout
    return config;
  }
  
  std::string host_;
  int port_;
};

void printUsage(const char* program_name) {
  std::cout << "Usage: " << program_name << " [host] [port] [requests]\n"
            << "  host:     Server hostname or IP (default: localhost)\n"
            << "  port:     Server TCP port (default: 8080)\n"
            << "  requests: Number of test requests to send (default: 5)\n"
            << "\n"
            << "Example:\n"
            << "  " << program_name << " localhost 9000 10\n"
            << "\n"
            << "The client will connect to the TCP echo server and send test requests.\n";
}

void runInteractiveMode(echo::EchoClientBase& client) {
  std::cout << "\n=== Interactive Mode ===" << std::endl;
  std::cout << "Enter JSON-RPC requests (or 'quit' to exit):" << std::endl;
  std::cout << "Example: {\"jsonrpc\":\"2.0\",\"method\":\"test\",\"id\":1}" << std::endl;
  
  std::string line;
  int request_id = 1000; // Start from 1000 for user requests
  
  while (!g_shutdown && std::getline(std::cin, line)) {
    if (line.empty()) continue;
    if (line == "quit" || line == "exit") break;
    
    try {
      // Parse as JSON to validate
      auto json_val = json::JsonValue::parse(line);
      
      if (json_val.contains("method")) {
        std::string method = json_val["method"].toString();
        
        if (json_val.contains("id")) {
          // Request - use the provided ID or generate one
          auto future = client.sendRequest(method, {});
          
          std::cout << "Sent request, waiting for response..." << std::endl;
          
          try {
            auto response = future.get();
            std::cout << "Response: " << json::to_json(response).toString() << std::endl;
          } catch (const std::exception& e) {
            std::cout << "Request failed: " << e.what() << std::endl;
          }
        } else {
          // Notification
          client.sendNotification(method, {});
          std::cout << "Sent notification: " << method << std::endl;
        }
      }
    } catch (const std::exception& e) {
      std::cout << "Invalid JSON: " << e.what() << std::endl;
    }
  }
}

} // namespace examples
} // namespace mcp

int main(int argc, char* argv[]) {
  using namespace mcp::examples;
  
  // Parse command line arguments
  std::string host = "localhost";
  int port = 8080;
  int num_requests = 5;
  
  if (argc > 4) {
    std::cerr << "Error: Too many arguments\n" << std::endl;
    printUsage(argv[0]);
    return 1;
  }
  
  if (argc >= 2) {
    if (std::string(argv[1]) == "--help" || std::string(argv[1]) == "-h") {
      printUsage(argv[0]);
      return 0;
    }
    host = argv[1];
  }
  
  if (argc >= 3) {
    port = std::atoi(argv[2]);
    if (port <= 0 || port > 65535) {
      std::cerr << "Error: Invalid port number: " << argv[2] << std::endl;
      return 1;
    }
  }
  
  if (argc >= 4) {
    num_requests = std::atoi(argv[3]);
    if (num_requests < 0) {
      std::cerr << "Error: Invalid request count: " << argv[3] << std::endl;
      return 1;
    }
  }
  
  // Setup signal handling
  signal(SIGINT, signalHandler);
  signal(SIGTERM, signalHandler);
  
  try {
    // Create TCP transport
    auto transport = mcp::echo::createTCPClientTransport();
    auto* tcp_transport = static_cast<mcp::echo::TCPTransport*>(transport.get());
    
    // Configure for client mode
    if (!tcp_transport->connect(host, port)) {
      std::cerr << "Failed to configure TCP client for " << host << ":" << port << std::endl;
      return 1;
    }
    
    // Create client
    g_client = std::make_unique<TCPEchoClient>(std::move(transport), host, port);
    
    std::cout << "Starting TCP Echo Client..." << std::endl;
    std::cout << "Connecting to " << host << ":" << port << std::endl;
    
    // Start client
    if (!g_client->start()) {
      std::cerr << "Failed to start TCP echo client" << std::endl;
      return 1;
    }
    
    // Wait for connection
    std::cout << "Waiting for connection..." << std::endl;
    for (int i = 0; i < 50 && !g_client->isConnected() && !g_shutdown; ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    if (!g_client->isConnected()) {
      std::cerr << "Failed to connect to server" << std::endl;
      return 1;
    }
    
    std::cout << "Connected! Sending " << num_requests << " test requests..." << std::endl;
    
    // Send test requests
    std::vector<std::future<mcp::jsonrpc::Response>> futures;
    
    for (int i = 1; i <= num_requests && !g_shutdown; ++i) {
      mcp::Metadata params = mcp::make<mcp::Metadata>()
          .add("request_number", i)
          .add("message", "Hello from TCP client!")
          .build();
      
      auto future = g_client->sendRequest("test_request", params);
      futures.push_back(std::move(future));
      
      // Small delay between requests
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    // Collect responses
    int successful = 0;
    int failed = 0;
    
    for (size_t i = 0; i < futures.size() && !g_shutdown; ++i) {
      try {
        auto response = futures[i].get();
        if (response.error.has_value()) {
          std::cout << "Request " << (i + 1) << " failed: " 
                    << response.error->message << std::endl;
          failed++;
        } else {
          std::cout << "Request " << (i + 1) << " succeeded" << std::endl;
          successful++;
        }
      } catch (const std::exception& e) {
        std::cout << "Request " << (i + 1) << " exception: " << e.what() << std::endl;
        failed++;
      }
    }
    
    std::cout << "\n=== Results ===" << std::endl;
    std::cout << "Successful: " << successful << std::endl;
    std::cout << "Failed: " << failed << std::endl;
    std::cout << "Total: " << (successful + failed) << std::endl;
    
    // Interactive mode if no shutdown signal received
    if (!g_shutdown && num_requests > 0) {
      runInteractiveMode(*g_client);
    }
    
    std::cout << "\nShutting down..." << std::endl;
    
  } catch (const std::exception& e) {
    std::cerr << "Error: " << e.what() << std::endl;
    return 1;
  }
  
  return 0;
}