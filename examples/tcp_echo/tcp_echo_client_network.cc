/**
 * TCP Echo Client using Network Abstractions
 * 
 * This implementation uses the MCP network abstraction layer instead of raw sockets.
 * It demonstrates proper use of:
 * - Address abstraction for network endpoints
 * - SocketInterface for socket operations
 * - Connection abstraction for managing network connections
 * - Event-driven architecture with Dispatcher
 * - Transport socket for protocol handling
 */

#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <signal.h>

#include "mcp/network/address.h"
#include "mcp/network/socket_interface.h"
#include "mcp/network/connection.h"
#include "mcp/network/connection_impl.h"
#include "mcp/network/socket_impl.h"
#include "mcp/network/transport_socket.h"
#include "mcp/event/libevent_dispatcher.h"
#include "mcp/buffer.h"
#include "mcp/echo/echo_basic.h"
#include "mcp/json/json_serialization.h"

namespace mcp {
namespace examples {

class NetworkTransportCallbacks : public network::TransportSocketCallbacks {
public:
  NetworkTransportCallbacks(echo::EchoClientBase& client) : client_(client) {}

  // TransportSocketCallbacks implementation
  IoCallbackReturn ioCallbackReturn() override {
    return IoCallbackReturn{PostIoAction::KeepOpen, 0, false};
  }

  void raiseEvent(network::ConnectionEvent event) override {
    if (event == network::ConnectionEvent::Connected) {
      std::cout << "Connected to server" << std::endl;
      connected_ = true;
    } else if (event == network::ConnectionEvent::RemoteClose ||
               event == network::ConnectionEvent::LocalClose) {
      std::cout << "Connection closed" << std::endl;
      connected_ = false;
    }
  }

  void setTransportSocketIsReadable() override {
    // Data is available to read
    if (connection_) {
      processIncomingData();
    }
  }

  const SslConnectionInfo* ssl() const override { return nullptr; }

  bool connected() const { return connected_; }

  void setConnection(network::ClientConnection* conn) {
    connection_ = conn;
  }

private:
  void processIncomingData() {
    // Read from transport socket and process
    Buffer read_buffer;
    connection_->transportSocket().doRead(read_buffer);
    
    // Process messages from buffer
    std::string data = read_buffer.toString();
    size_t pos = 0;
    while ((pos = data.find('\n')) != std::string::npos) {
      std::string message = data.substr(0, pos);
      data.erase(0, pos + 1);
      
      // Parse and handle the JSON-RPC response
      auto json_result = json::parseJson(message);
      if (json_result.has_value()) {
        client_.handleResponse(json_result.value());
      }
    }
  }

  echo::EchoClientBase& client_;
  network::ClientConnection* connection_{nullptr};
  bool connected_{false};
};

class NetworkBasedTransport : public echo::EchoTransportBase {
public:
  NetworkBasedTransport(event::Dispatcher& dispatcher,
                       network::SocketInterface& socket_interface)
      : dispatcher_(dispatcher),
        socket_interface_(socket_interface) {}

  ~NetworkBasedTransport() override {
    if (connection_) {
      connection_->close(network::ConnectionCloseType::NoFlush);
    }
  }

  bool start() override {
    // For client mode, connection happens in connect()
    return true;
  }

  void stop() override {
    if (connection_) {
      connection_->close(network::ConnectionCloseType::FlushWrite);
    }
    running_ = false;
  }

  bool isRunning() const override {
    return running_ && connection_ && callbacks_ && callbacks_->connected();
  }

  void sendMessage(const std::string& message) override {
    if (!connection_) {
      std::cerr << "No connection available" << std::endl;
      return;
    }

    // Add newline delimiter
    std::string data = message + "\n";
    Buffer write_buffer;
    write_buffer.add(data);
    
    // Write to connection
    connection_->write(write_buffer, false);
  }

  std::string receiveMessage() override {
    // This is handled asynchronously via callbacks
    // Return empty for now as we use the callback mechanism
    return "";
  }

  void run() override {
    // Run the event loop
    running_ = true;
    while (running_ && !dispatcher_.isTerminated()) {
      dispatcher_.run(event::Dispatcher::RunType::NonBlock);
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
  }

  bool connect(const std::string& host, int port) {
    // Parse address
    auto address = network::Address::parseInternetAddress(host, port);
    if (!address) {
      std::cerr << "Failed to parse address: " << host << ":" << port << std::endl;
      return false;
    }

    // Create socket
    auto socket_result = socket_interface_.socket(
        network::SocketType::Stream,
        address->type(),
        address->ip() ? optional<network::Address::IpVersion>(address->ip()->version()) : nullopt,
        false);
    
    if (!socket_result.ok()) {
      std::cerr << "Failed to create socket" << std::endl;
      return false;
    }

    // Create IoHandle
    auto io_handle = socket_interface_.ioHandleForFd(socket_result.value(), false);
    if (!io_handle) {
      std::cerr << "Failed to create IO handle" << std::endl;
      return false;
    }

    // Create Socket wrapper
    auto socket = std::make_unique<network::SocketImpl>(
        std::move(io_handle), address, network::SocketCreationOptions{});

    // Create transport socket (raw TCP, no SSL)
    auto transport_socket = std::make_unique<network::RawBufferSocket>();

    // Create connection
    connection_ = network::ConnectionImpl::createClientConnection(
        dispatcher_,
        std::move(socket),
        std::move(transport_socket),
        stream_info::StreamInfoImpl());

    // Set up callbacks
    if (client_) {
      callbacks_ = std::make_unique<NetworkTransportCallbacks>(*client_);
      callbacks_->setConnection(connection_.get());
      connection_->addConnectionCallbacks(*callbacks_);
    }

    // Connect
    connection_->connect();

    // Wait for connection with timeout
    auto start = std::chrono::steady_clock::now();
    while (!callbacks_->connected()) {
      dispatcher_.run(event::Dispatcher::RunType::NonBlock);
      
      auto elapsed = std::chrono::steady_clock::now() - start;
      if (elapsed > std::chrono::seconds(5)) {
        std::cerr << "Connection timeout" << std::endl;
        return false;
      }
      
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    return true;
  }

  void setClient(echo::EchoClientBase* client) {
    client_ = client;
  }

private:
  event::Dispatcher& dispatcher_;
  network::SocketInterface& socket_interface_;
  network::ClientConnectionPtr connection_;
  std::unique_ptr<NetworkTransportCallbacks> callbacks_;
  echo::EchoClientBase* client_{nullptr};
  bool running_{false};
  stream_info::StreamInfoImpl stream_info_;
};

} // namespace examples
} // namespace mcp

// Signal handler
std::atomic<bool> g_shutdown(false);

void signalHandler(int signal) {
  if (signal == SIGINT || signal == SIGTERM) {
    std::cout << "\nShutting down..." << std::endl;
    g_shutdown = true;
  }
}

int main(int argc, char* argv[]) {
  // Set up signal handlers
  signal(SIGINT, signalHandler);
  signal(SIGTERM, signalHandler);

  // Parse command line arguments
  std::string host = "localhost";
  int port = 3000;
  bool interactive = false;

  for (int i = 1; i < argc; i++) {
    std::string arg = argv[i];
    if (arg == "--host" && i + 1 < argc) {
      host = argv[++i];
    } else if (arg == "--port" && i + 1 < argc) {
      port = std::stoi(argv[++i]);
    } else if (arg == "--interactive") {
      interactive = true;
    } else if (arg == "--help") {
      std::cout << "TCP Echo Client using Network Abstractions\n"
                << "Usage: " << argv[0] << " [options]\n"
                << "Options:\n"
                << "  --host <address>  Server address (default: localhost)\n"
                << "  --port <port>     Server port (default: 3000)\n"
                << "  --interactive     Interactive mode\n"
                << "  --help           Show this help message\n";
      return 0;
    }
  }

  try {
    // Create event dispatcher
    auto dispatcher = std::make_unique<event::LibeventDispatcher>();
    
    // Get socket interface
    auto& socket_interface = network::socketInterface();
    
    // Create network-based transport
    auto transport = std::make_shared<mcp::examples::NetworkBasedTransport>(
        *dispatcher, socket_interface);

    // Create echo client
    mcp::echo::EchoClientBase client(transport);
    transport->setClient(&client);

    // Connect to server
    std::cout << "Connecting to " << host << ":" << port << "..." << std::endl;
    if (!transport->connect(host, port)) {
      std::cerr << "Failed to connect to server" << std::endl;
      return 1;
    }

    std::cout << "Connected successfully!" << std::endl;

    // Start the client
    if (!client.start()) {
      std::cerr << "Failed to start client" << std::endl;
      return 1;
    }

    if (interactive) {
      std::cout << "Interactive mode. Type messages to send, or 'quit' to exit." << std::endl;
      
      // Run client in background thread
      std::thread client_thread([&client]() {
        client.run();
      });

      // Read input from user
      std::string input;
      while (!g_shutdown && std::getline(std::cin, input)) {
        if (input == "quit") {
          break;
        }
        
        if (!input.empty()) {
          client.sendRequest("echo", input);
        }
      }

      g_shutdown = true;
      client.stop();
      
      if (client_thread.joinable()) {
        client_thread.join();
      }
    } else {
      // Non-interactive mode - send a test message
      client.sendRequest("echo", "Hello from network-based TCP client!");
      
      // Run for a short time
      auto start = std::chrono::steady_clock::now();
      while (!g_shutdown && 
             std::chrono::steady_clock::now() - start < std::chrono::seconds(2)) {
        dispatcher->run(event::Dispatcher::RunType::NonBlock);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
      }
      
      client.stop();
    }

  } catch (const std::exception& e) {
    std::cerr << "Error: " << e.what() << std::endl;
    return 1;
  }

  return 0;
}