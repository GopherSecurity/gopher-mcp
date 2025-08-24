/**
 * TCP Echo Server Basic Implementation
 *
 * This basic implementation uses the MCP network abstraction layer.
 * It demonstrates proper use of:
 * - Listener abstraction for accepting connections
 * - ConnectionManager for managing multiple connections
 * - Filter chains for connection processing
 * - Event-driven architecture with Dispatcher
 * - Transport socket abstraction for protocol handling
 */

#include <chrono>
#include <future>
#include <iostream>
#include <memory>
#include <signal.h>
#include <string>
#include <thread>
#include <unordered_map>

#include "mcp/buffer.h"
#include "mcp/echo/echo_basic.h"
#include "mcp/event/libevent_dispatcher.h"
#include "mcp/json/json_serialization.h"
#include "mcp/network/address.h"
#include "mcp/network/connection_impl.h"
#include "mcp/network/connection_manager.h"
#include "mcp/network/listener.h"
#include "mcp/network/socket_impl.h"
#include "mcp/network/socket_interface.h"
#include "mcp/network/socket_option_impl.h"
#include "mcp/network/transport_socket.h"

namespace mcp {
namespace examples {

class ServerConnectionHandler : public network::ConnectionCallbacks {
 public:
  ServerConnectionHandler(echo::EchoServerBase& server,
                          network::Connection& connection)
      : server_(server), connection_(connection) {}

  // ConnectionCallbacks implementation
  void onEvent(network::ConnectionEvent event) override {
    switch (event) {
      case network::ConnectionEvent::Connected:
        std::cout << "Client connected (id: " << connection_.id() << ")"
                  << std::endl;
        connected_ = true;
        break;
      case network::ConnectionEvent::RemoteClose:
        std::cout << "Client disconnected (id: " << connection_.id() << ")"
                  << std::endl;
        connected_ = false;
        break;
      case network::ConnectionEvent::LocalClose:
        std::cout << "Connection closed locally (id: " << connection_.id()
                  << ")" << std::endl;
        connected_ = false;
        break;
      case network::ConnectionEvent::ConnectedZeroRtt:
        // For non-TLS connections, this shouldn't happen
        break;
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

  // Note: TransportSocketCallbacks are handled by the Connection itself
  // We only implement ConnectionCallbacks here

  void processData() {
    if (!connected_) {
      return;
    }

    // Read available data
    OwnedBuffer read_buffer;
    auto result = connection_.transportSocket().doRead(read_buffer);

    if (result.action_ == TransportIoResult::CLOSE) {
      connection_.close(network::ConnectionCloseType::FlushWrite);
      connected_ = false;
      return;
    }

    if (result.bytes_processed_ > 0) {
      // Append to pending data for proper message framing
      pending_data_.append(read_buffer.toString());

      // Process complete messages
      size_t pos = 0;
      while ((pos = pending_data_.find('\n')) != std::string::npos) {
        std::string message = pending_data_.substr(0, pos);
        pending_data_.erase(0, pos + 1);

        // For now, just echo back the message as-is
        // Create a simple echo response
        std::string echo_response = "{\"echo\": \"" + message + "\"}";
        sendResponse(echo_response);
      }
    }
  }

  void sendResponse(const std::string& response) {
    if (!connected_) {
      return;
    }

    try {
      std::string message = response + "\n";
      OwnedBuffer write_buffer;
      write_buffer.add(message);
      connection_.write(write_buffer, false);
    } catch (const std::exception& e) {
      std::cerr << "Failed to send response to client " << connection_.id()
                << ": " << e.what() << std::endl;
    }
  }

  bool isConnected() const { return connected_; }

 private:
  echo::EchoServerBase& server_;
  network::Connection& connection_;
  std::string pending_data_;  // Buffer for incomplete messages
  bool connected_{false};
};

class BasicServerTransport : public echo::EchoTransportBase,
                             public network::ListenerCallbacks {
 public:
  BasicServerTransport(event::Dispatcher& dispatcher,
                       network::SocketInterface& socket_interface,
                       int port)
      : dispatcher_(dispatcher),
        socket_interface_(socket_interface),
        port_(port) {}

  ~BasicServerTransport() override { stop(); }

  // EchoTransportBase implementation
  void send(const std::string& data) override {
    // Server doesn't send unsolicited messages in echo protocol
    // Messages are sent as responses through connection handlers
  }

  void setDataCallback(DataCallback callback) override {
    data_callback_ = callback;
  }

  void setConnectionCallback(ConnectionCallback callback) override {
    connection_callback_ = callback;
  }

  bool isConnected() const override {
    // Server is "connected" if it's running and has connections
    return running_ && !connection_handlers_.empty();
  }

  std::string getTransportType() const override { return "TCP Network Server"; }

  bool start() override {
    // Create bind address (can be done outside dispatcher thread)
    // Try binding to localhost instead of INADDR_ANY
    auto address = network::Address::parseInternetAddress("127.0.0.1", port_);
    if (!address) {
      std::cerr << "Failed to create bind address" << std::endl;
      return false;
    }

    // All listener creation must happen in dispatcher thread
    std::promise<bool> start_promise;
    auto start_future = start_promise.get_future();

    dispatcher_.post([this, address, &start_promise]() {
      // Create listener manager in dispatcher thread
      listener_manager_ = std::make_unique<network::ListenerManagerImpl>(
          dispatcher_, socket_interface_);

      // Create listener config with production settings
      network::ListenerConfig config;
      config.name = "tcp_echo_server";
      config.address = address;
      config.bind_to_port = true;
      config.backlog = 128;  // Standard backlog size
      config.per_connection_buffer_limit = 1024 * 1024;  // 1MB per connection
      // SO_REUSEADDR is set by default in createListenSocket

      // Add listener (this creates ActiveListener which needs dispatcher
      // thread)
      auto result = listener_manager_->addListener(std::move(config), *this);
      if (holds_alternative<Error>(result)) {
        const auto& error = get<Error>(result);
        std::cerr << "Failed to add listener: " << error.message
                  << " (code: " << error.code << ")" << std::endl;
        start_promise.set_value(false);
        return;
      }

      // The listener is already listening (done in addListener)
      // Just verify we got the listener
      auto* listener = listener_manager_->getListener("tcp_echo_server");
      if (!listener) {
        std::cerr << "Failed to get listener" << std::endl;
        start_promise.set_value(false);
        return;
      }

      std::cout << "Server listening on port " << port_ << std::endl;
      running_ = true;
      start_promise.set_value(true);
    });

    // Run dispatcher until the task completes
    while (start_future.wait_for(std::chrono::milliseconds(0)) !=
           std::future_status::ready) {
      dispatcher_.run(event::RunType::NonBlock);
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // Get the result
    return start_future.get();
  }

  void stop() override {
    running_ = false;

    // Stop accepting new connections
    if (listener_manager_) {
      listener_manager_->stopListeners();
    }

    // Close all existing connections gracefully
    for (auto& [id, handler] : connection_handlers_) {
      if (handler.first) {
        handler.first->close(network::ConnectionCloseType::FlushWrite);
      }
    }
    connection_handlers_.clear();

    // Clean up listener manager
    listener_manager_.reset();
  }

  bool isRunning() const { return running_; }

  void sendMessage(const std::string& message) {
    // Server doesn't send unsolicited messages in echo protocol
    // Messages are sent as responses in the connection handlers
  }

  std::string receiveMessage() {
    // Messages are received asynchronously via callbacks
    return "";
  }

  void run() {
    while (running_) {
      dispatcher_.run(event::RunType::Block);
    }
  }

  // ListenerCallbacks implementation
  void onAccept(network::ConnectionSocketPtr&& socket) override {
    // onAccept is called from dispatcher thread during accept event
    // Connection creation should also happen in dispatcher thread

    // Create transport socket for the connection
    auto transport_socket =
        std::make_unique<network::RawBufferTransportSocket>();

    // Create server connection with stream info
    stream_info::StreamInfoImpl stream_info;
    auto connection = network::ConnectionImpl::createServerConnection(
        dispatcher_, std::move(socket), std::move(transport_socket),
        stream_info);

    if (connection) {
      onNewConnection(std::move(connection));
    }
  }

  void onNewConnection(network::ConnectionPtr&& connection) override {
    if (!running_) {
      // Server is shutting down, reject new connections
      connection->close(network::ConnectionCloseType::NoFlush);
      return;
    }

    // Use the connection as-is since we can't cast through virtual base
    auto* server_conn = connection.get();
    if (!server_conn) {
      std::cerr << "Failed to cast to ServerConnection" << std::endl;
      connection->close(network::ConnectionCloseType::NoFlush);
      return;
    }

    // Create connection handler
    auto handler =
        std::make_unique<ServerConnectionHandler>(*server_, *server_conn);

    // Register callbacks
    connection->addConnectionCallbacks(*handler);

    // Connection will handle its own transport socket callbacks

    // Store connection and handler
    uint64_t conn_id = connection->id();
    connection_handlers_[conn_id] = std::make_pair(
        std::unique_ptr<network::Connection>(connection.release()),
        std::move(handler));

    std::cout << "New connection accepted (id: " << conn_id
              << ", total: " << connection_handlers_.size() << ")" << std::endl;
  }

  void setServer(echo::EchoServerBase* server) { server_ = server; }

 private:
  event::Dispatcher& dispatcher_;
  network::SocketInterface& socket_interface_;
  int port_;
  bool running_{false};

  std::unique_ptr<network::ListenerManager> listener_manager_;
  echo::EchoServerBase* server_{nullptr};

  // Map of connection ID to (connection, handler) pair
  std::unordered_map<uint64_t,
                     std::pair<std::unique_ptr<network::Connection>,
                               std::unique_ptr<ServerConnectionHandler>>>
      connection_handlers_;

  // Callbacks for EchoTransportBase
  DataCallback data_callback_;
  ConnectionCallback connection_callback_;
};

}  // namespace examples
}  // namespace mcp

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
    auto dispatcher =
        std::make_unique<mcp::event::LibeventDispatcher>("tcp_echo_server");

    // Get socket interface
    auto& socket_interface = mcp::network::socketInterface();

    // Create network-based transport
    auto transport = std::make_unique<mcp::examples::BasicServerTransport>(
        *dispatcher, socket_interface, port);

    // Keep raw pointer for our use
    auto* transport_ptr = transport.get();

    // Create echo server (takes ownership of transport)
    mcp::echo::EchoServerBase server(std::move(transport));
    transport_ptr->setServer(&server);

    // Start the server
    if (!server.start()) {
      std::cerr << "Failed to start server" << std::endl;
      return 1;
    }

    std::cout << "TCP Echo Server (Network Abstraction) running on port "
              << port << std::endl;
    std::cout << "Press Ctrl+C to stop..." << std::endl;

    // Run the transport's event loop in a separate thread
    std::thread server_thread([transport_ptr]() { transport_ptr->run(); });

    // Wait for shutdown signal
    while (!g_shutdown && server.isRunning()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // Stop the transport which will exit the run() loop
    if (server.isRunning()) {
      server.stop();
    }

    // Wait for server thread to finish
    if (server_thread.joinable()) {
      server_thread.join();
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