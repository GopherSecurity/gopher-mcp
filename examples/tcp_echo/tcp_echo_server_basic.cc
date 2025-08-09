/**
 * @file tcp_echo_server_basic.cc
 * @brief Basic MCP echo server using TCP transport
 * 
 * This demonstrates the transport-agnostic echo server working with
 * TCP transport instead of stdio transport, showing how the same
 * base classes work transparently with different transports.
 * 
 * Architecture:
 * - Uses same EchoServerBase as stdio version
 * - TCP transport handles network socket communication
 * - JSON-RPC messages are newline-delimited over TCP
 * - Demonstrates transport abstraction in action
 * 
 * Usage:
 *   ./tcp_echo_server_basic [port]
 *   Default port: 8080
 * 
 * Example:
 *   ./tcp_echo_server_basic 9000
 *   
 * Then connect with:
 *   telnet localhost 9000
 *   or ./tcp_echo_client_basic localhost 9000
 */

#include "mcp/echo/echo_basic.h"
#include "mcp/echo/tcp_transport.h"
#include <iostream>
#include <signal.h>
#include <memory>
#include <string>
#include <cstdlib>

namespace mcp {
namespace examples {

// Global server instance for signal handling
std::unique_ptr<echo::EchoServerBase> g_server;

void signalHandler(int signal) {
  if (signal == SIGINT || signal == SIGTERM) {
    std::cerr << "\n[INFO] Received signal " << signal 
              << ", shutting down..." << std::endl;
    if (g_server) {
      g_server->stop();
    }
  }
}

/**
 * Custom TCP Echo Server
 * 
 * Inherits from EchoServerBase and adds TCP-specific functionality
 */
class TCPEchoServer : public echo::EchoServerBase {
public:
  TCPEchoServer(echo::EchoTransportBasePtr transport, int port)
      : echo::EchoServerBase(std::move(transport), createConfig(port)), 
        port_(port) {}

protected:
  jsonrpc::Response handleRequest(const jsonrpc::Request& request) override {
    // Add TCP server info to echo response
    auto response = make<jsonrpc::Response>(request.id)
        .result(jsonrpc::ResponseResult(make<Metadata>()
            .add("echo", true)
            .add("method", request.method)
            .add("server", "TCP Echo Server")
            .add("transport", "tcp")
            .add("port", port_)
            .add("timestamp", std::chrono::system_clock::now().time_since_epoch().count())
            .build()))
        .build();
    
    return response;
  }

private:
  static Config createConfig(int port) {
    Config config;
    config.server_name = "TCP Echo Server (port " + std::to_string(port) + ")";
    config.enable_logging = true;
    config.echo_notifications = true;
    return config;
  }
  
  int port_;
};

void printUsage(const char* program_name) {
  std::cout << "Usage: " << program_name << " [port]\n"
            << "  port: TCP port to listen on (default: 8080)\n"
            << "\n"
            << "Example:\n"
            << "  " << program_name << " 9000\n"
            << "\n"
            << "The server will accept TCP connections and echo JSON-RPC messages.\n"
            << "Test with telnet or the TCP echo client:\n"
            << "  telnet localhost 9000\n"
            << "  ./tcp_echo_client_basic localhost 9000\n";
}

} // namespace examples
} // namespace mcp

int main(int argc, char* argv[]) {
  using namespace mcp::examples;
  
  // Parse command line arguments
  int port = 8080; // Default port
  
  if (argc > 2) {
    std::cerr << "Error: Too many arguments\n" << std::endl;
    printUsage(argv[0]);
    return 1;
  }
  
  if (argc == 2) {
    if (std::string(argv[1]) == "--help" || std::string(argv[1]) == "-h") {
      printUsage(argv[0]);
      return 0;
    }
    
    port = std::atoi(argv[1]);
    if (port <= 0 || port > 65535) {
      std::cerr << "Error: Invalid port number: " << argv[1] << std::endl;
      return 1;
    }
  }
  
  // Setup signal handling
  signal(SIGINT, signalHandler);
  signal(SIGTERM, signalHandler);
  
  try {
    // Create TCP transport
    auto transport = mcp::echo::createTCPServerTransport();
    auto* tcp_transport = static_cast<mcp::echo::TCPTransport*>(transport.get());
    
    // Configure for server mode
    if (!tcp_transport->listen(port)) {
      std::cerr << "Failed to configure TCP server on port " << port << std::endl;
      return 1;
    }
    
    // Create server
    g_server = std::make_unique<TCPEchoServer>(std::move(transport), port);
    
    std::cout << "Starting TCP Echo Server on port " << port << "..." << std::endl;
    std::cout << "Press Ctrl+C to stop." << std::endl;
    
    // Start server
    if (!g_server->start()) {
      std::cerr << "Failed to start TCP echo server" << std::endl;
      return 1;
    }
    
    // Wait for server to stop
    while (g_server->isRunning()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    std::cout << "TCP Echo Server stopped." << std::endl;
    
  } catch (const std::exception& e) {
    std::cerr << "Error: " << e.what() << std::endl;
    return 1;
  }
  
  return 0;
}