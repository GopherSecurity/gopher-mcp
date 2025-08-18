/**
 * @file tcp_ssl_http_example.cc
 * @brief Example demonstrating the full transport stack: TCP → SSL → HTTP+SSE
 *
 * This example shows how to:
 * 1. Create an HTTPS+SSE transport factory
 * 2. Configure SSL/TLS settings
 * 3. Create layered transport sockets
 * 4. Use the transport for MCP communication
 */

#include <chrono>
#include <iostream>
#include <memory>
#include <thread>

#include "mcp/buffer.h"
#include "mcp/event/libevent_dispatcher.h"
#include "mcp/network/address_impl.h"
#include "mcp/network/connection_impl.h"
#include "mcp/transport/https_sse_transport_factory.h"

using namespace mcp;
using namespace mcp::transport;
using namespace mcp::network;
using namespace mcp::event;

// Example connection callbacks
class ExampleConnectionCallbacks : public ConnectionCallbacks {
 public:
  void onEvent(ConnectionEvent event) override {
    switch (event) {
      case ConnectionEvent::Connected:
        std::cout << "Connected successfully!" << std::endl;
        break;
      case ConnectionEvent::RemoteClose:
        std::cout << "Remote closed connection" << std::endl;
        break;
      case ConnectionEvent::LocalClose:
        std::cout << "Local closed connection" << std::endl;
        break;
      default:
        break;
    }
  }

  void onAboveWriteBufferHighWatermark() override {
    std::cout << "Write buffer above high watermark" << std::endl;
  }

  void onBelowWriteBufferLowWatermark() override {
    std::cout << "Write buffer below low watermark" << std::endl;
  }
};

// Example transport socket callbacks
class ExampleTransportCallbacks : public TransportSocketCallbacks {
 public:
  explicit ExampleTransportCallbacks(Connection& connection)
      : connection_(connection) {}

  IoHandle& ioHandle() override { return connection_.socket().ioHandle(); }

  const IoHandle& ioHandle() const override {
    return connection_.socket().ioHandle();
  }

  Connection& connection() override { return connection_; }

  bool shouldDrainReadBuffer() override { return true; }

  void setTransportSocketIsReadable() override {
    std::cout << "Transport socket is readable" << std::endl;
  }

  void raiseEvent(ConnectionEvent event) override {
    std::cout << "Transport raised event: " << static_cast<int>(event)
              << std::endl;
  }

  void flushWriteBuffer() override {
    std::cout << "Flushing write buffer" << std::endl;
  }

 private:
  Connection& connection_;
};

int main(int argc, char* argv[]) {
  std::cout << "=== MCP C++ SDK Transport Stack Example ===" << std::endl;
  std::cout << "Demonstrating: TCP → SSL → HTTP+SSE" << std::endl << std::endl;

  // 1. Create event dispatcher
  auto dispatcher_factory = createLibeventDispatcherFactory();
  auto dispatcher = dispatcher_factory->createDispatcher("example");

  // 2. Configure HTTP+SSE transport
  HttpSseTransportSocketConfig config;

  // Example 1: Plain HTTP (no SSL)
  if (argc > 1 && std::string(argv[1]) == "--http") {
    std::cout << "Using plain HTTP transport (no SSL)" << std::endl;
    config.server_address = "localhost:8080";
    config.underlying_transport = HttpSseTransportSocketConfig::UnderlyingTransport::TCP;
  }
  // Example 2: HTTPS with SSL
  else if (argc > 1 && std::string(argv[1]) == "--https") {
    std::cout << "Using HTTPS transport with SSL" << std::endl;
    config.server_address = "localhost:8443";
    config.underlying_transport = HttpSseTransportSocketConfig::UnderlyingTransport::SSL;
    config.ssl_config = HttpSseTransportSocketConfig::SslConfig{};
    config.ssl_config->verify_peer = true;

    // In production, you'd set these to actual certificate paths
    // config.ca_cert_path = "/path/to/ca-cert.pem";
    // config.client_cert_path = "/path/to/client-cert.pem";
    // config.client_key_path = "/path/to/client-key.pem";
  }
  // Default: Show configuration
  else {
    std::cout << "Usage: " << argv[0] << " [--http | --https]" << std::endl;
    std::cout << std::endl;
    std::cout << "This example demonstrates the transport stack:" << std::endl;
    std::cout << "  --http  : TCP → HTTP+SSE" << std::endl;
    std::cout << "  --https : TCP → SSL → HTTP+SSE" << std::endl;
    std::cout << std::endl;

    // Show how the factory detects SSL from URL
    std::cout << "=== SSL Auto-Detection Example ===" << std::endl;

    HttpSseTransportSocketConfig http_config;
    http_config.server_address = "api.example.com:80";
    http_config.underlying_transport = HttpSseTransportSocketConfig::UnderlyingTransport::TCP;
    auto http_factory =
        std::make_unique<HttpsSseTransportFactory>(http_config, *dispatcher);
    std::cout << "http://api.example.com → " << http_factory->name()
              << " (SSL: " << http_factory->implementsSecureTransport() << ")"
              << std::endl;

    HttpSseTransportSocketConfig https_config;
    https_config.server_address = "api.example.com:443";
    https_config.underlying_transport = HttpSseTransportSocketConfig::UnderlyingTransport::SSL;
    https_config.ssl_config = HttpSseTransportSocketConfig::SslConfig{};
    auto https_factory =
        std::make_unique<HttpsSseTransportFactory>(https_config, *dispatcher);
    std::cout << "https://api.example.com → " << https_factory->name()
              << " (SSL: " << https_factory->implementsSecureTransport() << ")"
              << std::endl;

    return 0;
  }

  // Configure common settings
  // Note: These settings are now handled by the filter chain in the new architecture
  // config.preferred_version = http::HttpVersion::HTTP_1_1;
  // config.auto_reconnect = true;
  // config.reconnect_delay = std::chrono::milliseconds(3000);
  // config.request_timeout = std::chrono::milliseconds(30000);
  // config.keepalive_interval = std::chrono::milliseconds(30000);
  config.connect_timeout = std::chrono::milliseconds(30000);
  config.mode = HttpSseTransportSocketConfig::Mode::CLIENT;

  // SSE endpoints and headers are now configured in the filter chain
  // config.sse_endpoint_path = "/events";
  // config.request_endpoint_path = "/rpc";
  // config.headers["User-Agent"] = "MCP-CPP-SDK/1.0";
  // config.headers["Accept"] = "text/event-stream";

  // 3. Create transport factory
  std::cout << std::endl << "=== Creating Transport Factory ===" << std::endl;
  auto factory =
      std::make_unique<HttpsSseTransportFactory>(config, *dispatcher);

  std::cout << "Factory name: " << factory->name() << std::endl;
  std::cout << "Implements secure transport: "
            << (factory->implementsSecureTransport() ? "Yes" : "No")
            << std::endl;
  std::cout << "Supports ALPN: " << (factory->supportsAlpn() ? "Yes" : "No")
            << std::endl;

  if (factory->implementsSecureTransport()) {
    std::cout << "Default SNI: " << factory->defaultServerNameIndication()
              << std::endl;
  }

  // 4. Create transport socket
  std::cout << std::endl << "=== Creating Transport Socket ===" << std::endl;
  auto transport_socket = factory->createTransportSocket(nullptr);

  if (transport_socket) {
    std::cout << "Transport socket created successfully" << std::endl;
    std::cout << "Protocol: " << transport_socket->protocol() << std::endl;
    std::cout << "Can flush close: "
              << (transport_socket->canFlushClose() ? "Yes" : "No")
              << std::endl;

    // The transport socket is now ready to use
    // It contains the full stack:
    // - TCP transport (base layer)
    // - SSL transport (if HTTPS)
    // - HTTP+SSE transport (top layer)

    std::cout << std::endl << "=== Transport Stack ===" << std::endl;
    if (config.underlying_transport == HttpSseTransportSocketConfig::UnderlyingTransport::SSL) {
      std::cout << "Layer 3: HTTP+SSE (Application Protocol)" << std::endl;
      std::cout << "Layer 2: SSL/TLS (Encryption)" << std::endl;
      std::cout << "Layer 1: TCP (Network Transport)" << std::endl;
    } else {
      std::cout << "Layer 2: HTTP+SSE (Application Protocol)" << std::endl;
      std::cout << "Layer 1: TCP (Network Transport)" << std::endl;
    }

    // In a real application, you would:
    // 1. Create a connection with proper socket
    // 2. Set transport callbacks
    // 3. Connect to the server
    // 4. Send/receive data

    std::cout << std::endl
              << "=== Example Usage (Pseudo-code) ===" << std::endl;
    std::cout << "// Create connection" << std::endl;
    std::cout << "auto connection = createConnection(address);" << std::endl;
    std::cout << "// Set callbacks" << std::endl;
    std::cout << "transport_socket->setTransportSocketCallbacks(callbacks);"
              << std::endl;
    std::cout << "// Connect" << std::endl;
    std::cout
        << "auto result = transport_socket->connect(connection->socket());"
        << std::endl;
    std::cout << "// Send MCP request" << std::endl;
    std::cout << "transport_socket->doWrite(request_buffer, false);"
              << std::endl;
    std::cout << "// Receive MCP response/events" << std::endl;
    std::cout << "transport_socket->doRead(response_buffer);" << std::endl;

  } else {
    std::cerr << "Failed to create transport socket" << std::endl;
    return 1;
  }

  std::cout << std::endl << "=== Example Complete ===" << std::endl;
  std::cout << "The transport stack is configured and ready for use."
            << std::endl;

  return 0;
}