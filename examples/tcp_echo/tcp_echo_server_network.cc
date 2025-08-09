/**
 * TCP Echo Server using Network Abstractions
 * 
 * This implementation uses the MCP network abstraction layer instead of raw sockets.
 * It demonstrates proper use of:
 * - Listener abstraction for accepting connections
 * - ConnectionManager for managing multiple connections
 * - Filter chains for connection processing
 * - Event-driven architecture with Dispatcher
 * - Transport socket abstraction for protocol handling
 */

#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <signal.h>
#include <memory>
#include <unordered_map>

#include "mcp/network/address.h"
#include "mcp/network/socket_interface.h"
#include "mcp/network/listener.h"
#include "mcp/network/connection_manager.h"
#include "mcp/network/connection_impl.h"
#include "mcp/network/socket_impl.h"
#include "mcp/network/transport_socket.h"
#include "mcp/event/libevent_dispatcher.h"
#include "mcp/buffer.h"
#include "mcp/echo/echo_basic.h"
#include "mcp/json/json_serialization.h"

namespace mcp {
namespace examples {

class ServerConnectionHandler : public network::ConnectionCallbacks {
public:
  ServerConnectionHandler(echo::EchoServerBase& server,
                         network::ServerConnection& connection)
      : server_(server), connection_(connection) {}

  // ConnectionCallbacks implementation
  void onEvent(network::ConnectionEvent event) override {
    if (event == network::ConnectionEvent::Connected) {
      std::cout << "Client connected (id: " << connection_.id() << ")" << std::endl;
    } else if (event == network::ConnectionEvent::RemoteClose) {
      std::cout << "Client disconnected (id: " << connection_.id() << ")" << std::endl;
      // Connection will be cleaned up by connection manager
    }
  }

  void onAboveWriteBufferHighWatermark() override {
    // Handle backpressure
    std::cout << "Write buffer high watermark reached" << std::endl;
  }

  void onBelowWriteBufferLowWatermark() override {
    // Resume writing
    std::cout << "Write buffer below low watermark" << std::endl;
  }

  void processData() {
    // Read available data
    Buffer read_buffer;
    auto result = connection_.transportSocket().doRead(read_buffer);
    
    if (result.action_ == PostIoAction::Close) {
      connection_.close(network::ConnectionCloseType::FlushWrite);
      return;
    }

    // Process messages from buffer
    std::string data = read_buffer.toString();
    size_t pos = 0;
    while ((pos = data.find('\n')) != std::string::npos) {
      std::string message = data.substr(0, pos);
      data.erase(0, pos + 1);
      
      // Parse and handle the JSON-RPC request
      auto json_result = json::parseJson(message);
      if (json_result.has_value()) {
        auto response = server_.handleRequest(json_result.value());
        sendResponse(response);
      }
    }
    
    // Keep any partial message for next read
    if (!data.empty()) {
      pending_data_ = data;
    }
  }

  void sendResponse(const json::JsonValue& response) {
    std::string message = json::serializeJson(response) + "\n";
    Buffer write_buffer;
    write_buffer.add(message);
    connection_.write(write_buffer, false);
  }

private:
  echo::EchoServerBase& server_;
  network::ServerConnection& connection_;
  std::string pending_data_;
};

class NetworkServerTransport : public echo::EchoTransportBase,
                              public network::ListenerCallbacks {
public:
  NetworkServerTransport(event::Dispatcher& dispatcher,
                        network::SocketInterface& socket_interface,
                        int port)
      : dispatcher_(dispatcher),
        socket_interface_(socket_interface),
        port_(port) {}

  ~NetworkServerTransport() override {
    stop();
  }

  bool start() override {
    // Create listener manager
    listener_manager_ = std::make_unique<network::ListenerManagerImpl>(
        dispatcher_, socket_interface_);

    // Create bind address
    auto address = network::Address::anyAddress(
        network::Address::IpVersion::v4, port_);
    if (!address) {
      std::cerr << "Failed to create bind address" << std::endl;
      return false;
    }

    // Create listener config
    network::ListenerConfig config;
    config.name = "tcp_echo_server";
    config.address = address;
    config.bind_to_port = true;
    config.backlog = 128;
    config.per_connection_buffer_limit = 1024 * 1024; // 1MB

    // Add listener
    auto result = listener_manager_->addListener(std::move(config), *this);
    if (!result.ok()) {
      std::cerr << "Failed to add listener: " << result.error().message << std::endl;
      return false;
    }

    // Get the listener and start listening
    auto* listener = listener_manager_->getListener("tcp_echo_server");
    if (!listener) {
      std::cerr << "Failed to get listener" << std::endl;
      return false;
    }

    auto listen_result = listener->listen();
    if (!listen_result.ok()) {
      std::cerr << "Failed to listen: " << listen_result.error().message << std::endl;
      return false;
    }

    std::cout << "Server listening on port " << port_ << std::endl;
    running_ = true;
    return true;
  }

  void stop() override {
    if (listener_manager_) {
      listener_manager_->stopListeners();
    }
    
    // Close all connections
    for (auto& [id, handler] : connection_handlers_) {
      handler.first->close(network::ConnectionCloseType::FlushWrite);
    }
    connection_handlers_.clear();
    
    running_ = false;
  }

  bool isRunning() const override {
    return running_;
  }

  void sendMessage(const std::string& message) override {
    // Server doesn't send unsolicited messages in echo protocol
    // Messages are sent as responses in the connection handlers
  }

  std::string receiveMessage() override {
    // Messages are received asynchronously via callbacks
    return "";
  }

  void run() override {
    while (running_ && !dispatcher_.isTerminated()) {
      dispatcher_.run(event::Dispatcher::RunType::Block);
    }
  }

  // ListenerCallbacks implementation
  void onAccept(network::ConnectionSocketPtr&& socket) override {
    // Create transport socket for the connection
    auto transport_socket = std::make_unique<network::RawBufferSocket>();
    
    // Create server connection
    auto connection = network::ConnectionImpl::createServerConnection(
        dispatcher_,
        std::move(socket),
        std::move(transport_socket),
        stream_info::StreamInfoImpl());

    if (connection) {
      onNewConnection(std::move(connection));
    }
  }

  void onNewConnection(network::ConnectionPtr&& connection) override {
    // Cast to ServerConnection
    auto* server_conn = dynamic_cast<network::ServerConnection*>(connection.get());
    if (!server_conn) {
      std::cerr << "Failed to cast to ServerConnection" << std::endl;
      return;
    }

    // Create connection handler
    auto handler = std::make_unique<ServerConnectionHandler>(*server_, *server_conn);
    
    // Register callbacks
    server_conn->addConnectionCallbacks(*handler);
    
    // Set up read callback
    server_conn->transportSocket().setTransportSocketCallbacks(*handler);
    
    // Store connection and handler
    uint64_t conn_id = server_conn->id();
    connection_handlers_[conn_id] = std::make_pair(
        std::unique_ptr<network::ServerConnection>(
            static_cast<network::ServerConnection*>(connection.release())),
        std::move(handler));
    
    std::cout << "New connection accepted (id: " << conn_id << ")" << std::endl;
  }

  void setServer(echo::EchoServerBase* server) {
    server_ = server;
  }

private:
  event::Dispatcher& dispatcher_;
  network::SocketInterface& socket_interface_;
  int port_;
  bool running_{false};
  
  std::unique_ptr<network::ListenerManager> listener_manager_;
  echo::EchoServerBase* server_{nullptr};
  
  // Map of connection ID to (connection, handler) pair
  std::unordered_map<uint64_t, 
                     std::pair<std::unique_ptr<network::ServerConnection>,
                              std::unique_ptr<ServerConnectionHandler>>> connection_handlers_;
  
  stream_info::StreamInfoImpl stream_info_;
};

} // namespace examples
} // namespace mcp

// Signal handler
std::atomic<bool> g_shutdown(false);

void signalHandler(int signal) {
  if (signal == SIGINT || signal == SIGTERM) {
    std::cout << "\nShutting down server..." << std::endl;
    g_shutdown = true;
  }
}

int main(int argc, char* argv[]) {
  // Set up signal handlers
  signal(SIGINT, signalHandler);
  signal(SIGTERM, signalHandler);

  // Parse command line arguments
  int port = 3000;

  for (int i = 1; i < argc; i++) {
    std::string arg = argv[i];
    if (arg == "--port" && i + 1 < argc) {
      port = std::stoi(argv[++i]);
    } else if (arg == "--help") {
      std::cout << "TCP Echo Server using Network Abstractions\n"
                << "Usage: " << argv[0] << " [options]\n"
                << "Options:\n"
                << "  --port <port>  Server port (default: 3000)\n"
                << "  --help        Show this help message\n";
      return 0;
    }
  }

  try {
    // Create event dispatcher
    auto dispatcher = std::make_unique<event::LibeventDispatcher>();
    
    // Get socket interface
    auto& socket_interface = network::socketInterface();
    
    // Create network-based transport
    auto transport = std::make_shared<mcp::examples::NetworkServerTransport>(
        *dispatcher, socket_interface, port);

    // Create echo server
    mcp::echo::EchoServerBase server(transport);
    transport->setServer(&server);

    // Start the server
    if (!server.start()) {
      std::cerr << "Failed to start server" << std::endl;
      return 1;
    }

    std::cout << "TCP Echo Server (Network Abstraction) running on port " << port << std::endl;
    std::cout << "Press Ctrl+C to stop..." << std::endl;

    // Run server in main thread
    while (!g_shutdown && server.isRunning()) {
      dispatcher->run(event::Dispatcher::RunType::NonBlock);
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // Cleanup
    server.stop();
    std::cout << "Server stopped" << std::endl;

  } catch (const std::exception& e) {
    std::cerr << "Error: " << e.what() << std::endl;
    return 1;
  }

  return 0;
}