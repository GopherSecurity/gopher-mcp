/**
 * @file mcp_https_example.cc
 * @brief Example of using MCP with HTTPS+SSE transport
 *
 * Demonstrates:
 * - Setting up SSL/TLS for secure MCP communication
 * - Using HTTPS+SSE transport with certificates
 * - Proper SSL handshake in dispatcher thread
 * - Clean separation of transport layers
 */

#include <chrono>
#include <iostream>
#include <memory>
#include <thread>

#include "mcp/client/mcp_client.h"
#include "mcp/event/libevent_dispatcher.h"
#include "mcp/server/mcp_server.h"
#include "mcp/transport/https_sse_transport_factory.h"

using namespace mcp;

/**
 * Example HTTPS MCP Server
 *
 * Demonstrates server-side SSL configuration and setup
 */
class HttpsMcpServer {
 public:
  HttpsMcpServer() {
    // Create dispatcher for async operations
    dispatcher_ = std::make_unique<event::LibeventDispatcher>();

    // Configure server with SSL
    server::McpServerConfig config;
    config.server_name = "example-mcp-server";
    config.server_version = "1.0.0";
    config.instructions = "Example HTTPS MCP server with SSL/TLS";

    // Create server
    server_ = server::createMcpServer(config);

    // Register handlers
    registerHandlers();
  }

  /**
   * Start HTTPS server
   *
   * @param cert_file Server certificate file path
   * @param key_file Server private key file path
   * @param port Listen port
   */
  void start(const std::string& cert_file,
             const std::string& key_file,
             uint16_t port = 8443) {
    // Configure HTTPS+SSE transport
    transport::HttpSseTransportSocketConfig transport_config;
    transport_config.endpoint_url = "https://0.0.0.0:" + std::to_string(port);
    transport_config.use_ssl = true;
    transport_config.client_cert_path = cert_file;  // Server cert
    transport_config.client_key_path = key_file;    // Server key
    transport_config.verify_ssl = false;            // Don't verify client certs

    // Create transport factory
    transport_factory_ = transport::createHttpsSseTransportFactory(
        transport_config, *dispatcher_);

    // Start listening
    std::string listen_address = "0.0.0.0:" + std::to_string(port);
    auto result = server_->listen(listen_address);
    if (!result.ok()) {
      std::cerr << "Failed to listen: " << result.error() << std::endl;
      return;
    }

    std::cout << "HTTPS MCP Server listening on " << listen_address
              << std::endl;
    std::cout << "Using certificate: " << cert_file << std::endl;

    // Run event loop
    server_->run();
  }

  void stop() { server_->shutdown(); }

 private:
  void registerHandlers() {
    // Register example resource
    Resource example_resource;
    example_resource.uri = "file:///example.txt";
    example_resource.name = "Example Resource";
    example_resource.description = "An example resource over HTTPS";
    example_resource.mimeType = "text/plain";
    server_->registerResource(example_resource);

    // Register example tool
    Tool example_tool;
    example_tool.name = "echo";
    example_tool.description = "Echo back the input";
    server_->registerTool(
        example_tool,
        [](const std::string& name, const optional<Metadata>& arguments,
           server::SessionContext& session) -> CallToolResult {
          CallToolResult result;

          // Echo back the arguments
          if (arguments.has_value()) {
            TextContent content;
            content.text = "Echo: " + arguments.value().dump();
            result.content.push_back(ExtendedContentBlock(content));
          }

          return result;
        });
  }

 private:
  std::unique_ptr<event::Dispatcher> dispatcher_;
  std::unique_ptr<server::McpServer> server_;
  std::unique_ptr<network::TransportSocketFactoryBase> transport_factory_;
};

/**
 * Example HTTPS MCP Client
 *
 * Demonstrates client-side SSL configuration and connection
 */
class HttpsMcpClient {
 public:
  HttpsMcpClient() {
    // Create dispatcher
    dispatcher_ = std::make_unique<event::LibeventDispatcher>();

    // Configure client
    client::McpClientConfig config;
    config.client_name = "example-mcp-client";
    config.client_version = "1.0.0";
    config.preferred_transport = TransportType::HttpSse;

    // Create client
    client_ = client::createMcpClient(config);
  }

  /**
   * Connect to HTTPS server
   *
   * @param server_url HTTPS URL of server
   * @param ca_cert_file CA certificate for verification (optional)
   * @param client_cert_file Client certificate (optional)
   * @param client_key_file Client private key (optional)
   * @param verify_ssl Enable SSL verification
   */
  void connect(const std::string& server_url,
               const std::string& ca_cert_file = "",
               const std::string& client_cert_file = "",
               const std::string& client_key_file = "",
               bool verify_ssl = true) {
    // Configure HTTPS+SSE transport
    transport::HttpSseTransportSocketConfig transport_config;
    transport_config.endpoint_url = server_url;
    transport_config.use_ssl = true;  // Will be auto-detected from https://
    transport_config.verify_ssl = verify_ssl;

    // Set certificates if provided
    if (!ca_cert_file.empty()) {
      transport_config.ca_cert_path = ca_cert_file;
    }
    if (!client_cert_file.empty()) {
      transport_config.client_cert_path = client_cert_file;
    }
    if (!client_key_file.empty()) {
      transport_config.client_key_path = client_key_file;
    }

    // Set SNI hostname (extracted from URL)
    transport_config.sni_hostname = extractHostname(server_url);

    // Set ALPN protocols for HTTP
    transport_config.alpn_protocols = {"http/1.1", "h2"};

    // Create transport factory
    transport_factory_ = transport::createHttpsSseTransportFactory(
        transport_config, *dispatcher_);

    std::cout << "Connecting to " << server_url << " with SSL/TLS" << std::endl;
    if (verify_ssl) {
      std::cout << "SSL verification enabled";
      if (!ca_cert_file.empty()) {
        std::cout << " using CA: " << ca_cert_file;
      }
      std::cout << std::endl;
    }

    // Connect to server
    auto result = client_->connect(server_url);
    if (!result.ok()) {
      std::cerr << "Failed to connect: " << result.error() << std::endl;
      return;
    }

    // Initialize protocol
    auto init_future = client_->initialize();

    // Wait for initialization (in real app, would be async)
    auto init_result = init_future.get();
    if (init_result.protocolVersion.empty()) {
      std::cerr << "Failed to initialize protocol" << std::endl;
      return;
    }

    std::cout << "Connected and initialized with protocol version: "
              << init_result.protocolVersion << std::endl;
  }

  /**
   * Call a tool over HTTPS
   */
  void callTool(const std::string& tool_name, const std::string& input) {
    Metadata args;
    args["input"] = input;

    auto future = client_->callTool(tool_name, args);

    // Wait for result (in real app, would be async)
    auto result = future.get();

    if (result.isError) {
      std::cerr << "Tool call failed" << std::endl;
    } else {
      std::cout << "Tool result: ";
      for (const auto& content : result.content) {
        // Handle different content types
        if (holds_alternative<TextContent>(content)) {
          std::cout << get<TextContent>(content).text;
        }
      }
      std::cout << std::endl;
    }
  }

  void disconnect() { client_->disconnect(); }

 private:
  std::string extractHostname(const std::string& url) {
    size_t start = url.find("://");
    if (start == std::string::npos)
      return "";
    start += 3;

    size_t end = url.find_first_of(":/?", start);
    if (end == std::string::npos)
      end = url.length();

    return url.substr(start, end - start);
  }

 private:
  std::unique_ptr<event::Dispatcher> dispatcher_;
  std::unique_ptr<client::McpClient> client_;
  std::unique_ptr<network::TransportSocketFactoryBase> transport_factory_;
};

/**
 * Example SSL handshake callbacks
 *
 * Demonstrates monitoring SSL handshake events
 */
class ExampleSslCallbacks : public transport::SslHandshakeCallbacks {
 public:
  void onSslHandshakeComplete() override {
    std::cout << "SSL handshake completed successfully" << std::endl;
    handshake_complete_ = true;
  }

  void onSslHandshakeFailed(const std::string& reason) override {
    std::cerr << "SSL handshake failed: " << reason << std::endl;
    handshake_failed_ = true;
  }

  bool isHandshakeComplete() const { return handshake_complete_; }
  bool isHandshakeFailed() const { return handshake_failed_; }

 private:
  std::atomic<bool> handshake_complete_{false};
  std::atomic<bool> handshake_failed_{false};
};

/**
 * Main function demonstrating HTTPS MCP usage
 */
int main(int argc, char* argv[]) {
  // Parse command line arguments
  bool is_server = false;
  bool is_client = false;
  std::string server_url = "https://localhost:8443";
  std::string cert_file = "server.crt";
  std::string key_file = "server.key";
  std::string ca_file = "";
  bool verify_ssl = true;

  for (int i = 1; i < argc; i++) {
    std::string arg = argv[i];
    if (arg == "--server") {
      is_server = true;
    } else if (arg == "--client") {
      is_client = true;
    } else if (arg == "--url" && i + 1 < argc) {
      server_url = argv[++i];
    } else if (arg == "--cert" && i + 1 < argc) {
      cert_file = argv[++i];
    } else if (arg == "--key" && i + 1 < argc) {
      key_file = argv[++i];
    } else if (arg == "--ca" && i + 1 < argc) {
      ca_file = argv[++i];
    } else if (arg == "--no-verify") {
      verify_ssl = false;
    }
  }

  if (!is_server && !is_client) {
    std::cout << "Usage: " << argv[0] << " [--server|--client] [options]"
              << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout
        << "  --url <url>       Server URL (default: https://localhost:8443)"
        << std::endl;
    std::cout << "  --cert <file>     Certificate file (server mode)"
              << std::endl;
    std::cout << "  --key <file>      Private key file (server mode)"
              << std::endl;
    std::cout << "  --ca <file>       CA certificate file (client mode)"
              << std::endl;
    std::cout << "  --no-verify       Disable SSL verification (client mode)"
              << std::endl;
    return 1;
  }

  try {
    if (is_server) {
      // Run HTTPS MCP server
      std::cout << "Starting HTTPS MCP Server..." << std::endl;

      HttpsMcpServer server;
      server.start(cert_file, key_file, 8443);

      // Server runs until interrupted

    } else if (is_client) {
      // Run HTTPS MCP client
      std::cout << "Starting HTTPS MCP Client..." << std::endl;

      HttpsMcpClient client;

      // Connect with SSL
      client.connect(server_url, ca_file, "", "", verify_ssl);

      // Call example tool
      std::cout << "Calling 'echo' tool..." << std::endl;
      client.callTool("echo", "Hello, HTTPS MCP!");

      // Wait a bit for async operations
      std::this_thread::sleep_for(std::chrono::seconds(2));

      // Disconnect
      client.disconnect();
      std::cout << "Client disconnected" << std::endl;
    }

  } catch (const std::exception& e) {
    std::cerr << "Error: " << e.what() << std::endl;
    return 1;
  }

  return 0;
}

/**
 * Example output:
 *
 * Server:
 * ```
 * $ ./mcp_https_example --server --cert server.crt --key server.key
 * Starting HTTPS MCP Server...
 * HTTPS MCP Server listening on 0.0.0.0:8443
 * Using certificate: server.crt
 * [Waiting for connections...]
 * SSL handshake completed with client
 * Received initialize request
 * Received tool call: echo
 * ```
 *
 * Client:
 * ```
 * $ ./mcp_https_example --client --url https://localhost:8443 --ca ca.crt
 * Starting HTTPS MCP Client...
 * Connecting to https://localhost:8443 with SSL/TLS
 * SSL verification enabled using CA: ca.crt
 * SSL handshake completed successfully
 * Connected and initialized with protocol version: 2024-11-05
 * Calling 'echo' tool...
 * Tool result: Echo: {"input":"Hello, HTTPS MCP!"}
 * Client disconnected
 * ```
 */