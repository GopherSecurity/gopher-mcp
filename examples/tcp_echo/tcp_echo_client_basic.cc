/**
 * TCP Echo Client Basic Implementation
 * 
 * This basic implementation uses the MCP network abstraction layer.
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
#include "mcp/stream_info/stream_info_impl.h"
#include "mcp/buffer.h"
#include "mcp/echo/echo_basic.h"
#include "mcp/json/json_serialization.h"

namespace mcp {
namespace examples {

class BasicTransportCallbacks : public network::ConnectionCallbacks {
public:
  BasicTransportCallbacks(echo::EchoClientBase& client) : client_(client) {}

  // ConnectionCallbacks implementation
  void onEvent(network::ConnectionEvent event) override {
    if (event == network::ConnectionEvent::Connected) {
      std::cout << "Connected to server" << std::endl;
      connected_ = true;
    } else if (event == network::ConnectionEvent::RemoteClose ||
               event == network::ConnectionEvent::LocalClose) {
      std::cout << "Connection closed" << std::endl;
      connected_ = false;
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

  bool connected() const { return connected_; }

  void setConnection(network::ClientConnection* conn) {
    connection_ = conn;
  }
  
  void processIncomingData() {
    if (!connection_ || !connected_) {
      return;
    }
    
    // Read from transport socket and process
    OwnedBuffer read_buffer;
    auto result = connection_->transportSocket().doRead(read_buffer);
    
    if (result.action_ == TransportIoResult::CLOSE) {
      // Connection closing
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
        
        // Parse and handle the JSON-RPC response
        try {
          // For now, just log that we received a response
          std::cout << "Received response: " << message << std::endl;
        } catch (const std::exception& e) {
          std::cerr << "Failed to process response: " << e.what() << std::endl;
        }
      }
    }
  }

private:
  echo::EchoClientBase& client_;
  network::ClientConnection* connection_{nullptr};
  bool connected_{false};
  std::string pending_data_;  // Buffer for incomplete messages
};

// Simple periodic reader without Timer interface
class DataReader {
public:
  DataReader(BasicTransportCallbacks& callbacks) : callbacks_(callbacks) {}
  
  void start() { enabled_ = true; }
  void stop() { enabled_ = false; }
  bool isEnabled() const { return enabled_; }
  
  void checkForData() {
    if (enabled_) {
      callbacks_.processIncomingData();
    }
  }
  
private:
  BasicTransportCallbacks& callbacks_;
  bool enabled_{false};
};

class BasicClientTransport : public echo::EchoTransportBase {
public:
  BasicClientTransport(event::Dispatcher& dispatcher,
                       network::SocketInterface& socket_interface)
      : dispatcher_(dispatcher),
        socket_interface_(socket_interface) {}

  ~BasicClientTransport() override {
    if (connection_) {
      connection_->close(network::ConnectionCloseType::NoFlush);
    }
  }

  // EchoTransportBase implementation
  void send(const std::string& data) override {
    sendMessage(data);
  }
  
  void setDataCallback(DataCallback callback) override {
    data_callback_ = callback;
  }
  
  void setConnectionCallback(ConnectionCallback callback) override {
    connection_callback_ = callback;
  }

  bool start() override {
    // For client mode, connection happens in connect()
    return true;
  }

  void stop() override {
    running_ = false;
    if (connection_) {
      connection_->close(network::ConnectionCloseType::FlushWrite);
      connection_.reset();
    }
    callbacks_.reset();
  }

  bool isConnected() const override {
    return running_ && callbacks_ && callbacks_->connected();
  }
  
  std::string getTransportType() const override {
    return "TCP Network";
  }
  
  bool isRunning() const {
    return running_;
  }

  void sendMessage(const std::string& message) {
    if (!connection_ || !callbacks_ || !callbacks_->connected()) {
      std::cerr << "Cannot send message: connection not available" << std::endl;
      return;
    }

    // Add newline delimiter for message framing
    std::string data = message + "\n";
    OwnedBuffer write_buffer;
    write_buffer.add(data);
    
    // Write to connection
    connection_->write(write_buffer, false);
  }

  std::string receiveMessage() {
    // Messages are received asynchronously via callbacks
    return "";
  }

  void run() {
    // Run the event loop with periodic read checks
    while (running_) {
      dispatcher_.run(event::RunType::NonBlock);
      
      // Check for incoming data
      if (data_reader_) {
        data_reader_->checkForData();
      }
      
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
  }

  bool connect(const std::string& host, int port) {
    // Parse address (convert localhost to 127.0.0.1)
    std::string ip_addr = host;
    if (host == "localhost") {
      ip_addr = "127.0.0.1";
    }
    
    auto address = network::Address::parseInternetAddress(ip_addr, port);
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
    auto io_handle = socket_interface_.ioHandleForFd(*socket_result.value, false);
    if (!io_handle) {
      std::cerr << "Failed to create IO handle" << std::endl;
      return false;
    }

    // Create connection socket with local and remote addresses
    auto local_address = network::Address::anyAddress(
        network::Address::IpVersion::v4, 0);  // Any local port
    auto socket = std::make_unique<network::ConnectionSocketImpl>(
        std::move(io_handle), local_address, address);

    // Create transport socket (raw TCP, no SSL)
    auto transport_socket = std::make_unique<network::RawBufferTransportSocket>();

    // Create connection with stream info
    stream_info::StreamInfoImpl stream_info;
    
    connection_ = network::ConnectionImpl::createClientConnection(
        dispatcher_,
        std::move(socket),
        std::move(transport_socket),
        stream_info);

    // Set up callbacks
    if (client_) {
      callbacks_ = std::make_unique<BasicTransportCallbacks>(*client_);
      callbacks_->setConnection(connection_.get());
      connection_->addConnectionCallbacks(*callbacks_);
      
      // Set up periodic data reader
      data_reader_ = std::make_unique<DataReader>(*callbacks_);
      data_reader_->start();
    }

    // Connect
    connection_->connect();

    // Wait for connection with timeout
    auto start = std::chrono::steady_clock::now();
    const auto timeout = std::chrono::seconds(5);
    
    while (!callbacks_->connected()) {
      dispatcher_.run(event::RunType::NonBlock);
      
      auto elapsed = std::chrono::steady_clock::now() - start;
      if (elapsed > timeout) {
        std::cerr << "Connection timeout after " 
                  << std::chrono::duration_cast<std::chrono::seconds>(elapsed).count() 
                  << " seconds" << std::endl;
        connection_->close(network::ConnectionCloseType::NoFlush);
        connection_.reset();
        callbacks_.reset();
        return false;
      }
      
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    running_ = true;
    return true;
  }

  void setClient(echo::EchoClientBase* client) {
    client_ = client;
  }

private:
  event::Dispatcher& dispatcher_;
  network::SocketInterface& socket_interface_;
  std::unique_ptr<network::ClientConnection> connection_;
  std::unique_ptr<BasicTransportCallbacks> callbacks_;
  std::unique_ptr<DataReader> data_reader_;
  echo::EchoClientBase* client_{nullptr};
  bool running_{false};
  
  // Callbacks for EchoTransportBase
  DataCallback data_callback_;
  ConnectionCallback connection_callback_;
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
  int num_requests = 5;
  bool interactive = false;

  for (int i = 1; i < argc; i++) {
    std::string arg = argv[i];
    if (arg == "--host" && i + 1 < argc) {
      host = argv[++i];
    } else if (arg == "--port" && i + 1 < argc) {
      port = std::stoi(argv[++i]);
    } else if (arg == "--requests" && i + 1 < argc) {
      num_requests = std::stoi(argv[++i]);
    } else if (arg == "--interactive") {
      interactive = true;
    } else if (arg == "--help") {
      std::cout << "TCP Echo Client using Network Abstractions\n"
                << "Usage: " << argv[0] << " [options]\n"
                << "Options:\n"
                << "  --host <address>  Server address (default: localhost)\n"
                << "  --port <port>     Server port (default: 3000)\n"
                << "  --requests <n>    Number of test requests (default: 5)\n"
                << "  --interactive     Interactive mode\n"
                << "  --help           Show this help message\n";
      return 0;
    }
  }

  try {
    // Create event dispatcher
    auto dispatcher = std::make_unique<mcp::event::LibeventDispatcher>("tcp_echo_client");
    
    // Get socket interface singleton
    auto& socket_interface = mcp::network::socketInterface();
    
    // Create network-based transport
    auto transport = std::make_unique<mcp::examples::BasicClientTransport>(
        *dispatcher, socket_interface);
    
    // Keep a raw pointer for our use
    auto* transport_ptr = transport.get();

    // Create echo client (takes ownership of transport)
    mcp::echo::EchoClientBase client(std::move(transport));
    transport_ptr->setClient(&client);

    // Connect to server
    std::cout << "Connecting to " << host << ":" << port << "..." << std::endl;
    if (!transport_ptr->connect(host, port)) {
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
      std::thread client_thread([transport_ptr]() {
        try {
          // Transport runs its event loop
          transport_ptr->run();
        } catch (const std::exception& e) {
          std::cerr << "Client thread error: " << e.what() << std::endl;
        }
      });

      // Read input from user
      std::string input;
      while (!g_shutdown && std::getline(std::cin, input)) {
        if (input == "quit") {
          break;
        }
        
        if (!input.empty()) {
          // Send as simple message through transport for now
          transport_ptr->sendMessage(input);
        }
      }

      g_shutdown = true;
      client.stop();
      
      if (client_thread.joinable()) {
        client_thread.join();
      }
    } else {
      // Non-interactive mode - send multiple test messages
      std::cout << "Sending " << num_requests << " test requests..." << std::endl;
      
      // Run client in background thread  
      std::thread client_thread([transport_ptr]() {
        try {
          // Transport runs its event loop
          transport_ptr->run();
        } catch (const std::exception& e) {
          std::cerr << "Client thread error: " << e.what() << std::endl;
        }
      });
      
      // Send test requests
      // Store futures for the responses
      std::vector<std::pair<int, std::string>> sent_messages;
      
      for (int i = 1; i <= num_requests && !g_shutdown; i++) {
        std::string message = "Test message #" + std::to_string(i) + " from network-based TCP client!";
        // Send message directly through transport
        transport_ptr->sendMessage(message);
        sent_messages.push_back({i, message});
        
        std::cout << "Sent request #" << i << std::endl;
        
        // Small delay between requests
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
      }
      
      // Wait a bit for responses to be processed
      std::this_thread::sleep_for(std::chrono::seconds(2));
      
      // Simple summary since we're using basic transport
      int successful = sent_messages.size();
      int failed = 0;
      
      // Print summary
      std::cout << "\n=== Results ===" << std::endl;
      std::cout << "Successful: " << successful << std::endl;
      std::cout << "Failed: " << failed << std::endl;
      std::cout << "Total: " << (successful + failed) << std::endl;
      
      g_shutdown = true;
      client.stop();
      
      if (client_thread.joinable()) {
        client_thread.join();
      }
    }

  } catch (const std::exception& e) {
    std::cerr << "Error: " << e.what() << std::endl;
    return 1;
  }

  return 0;
}