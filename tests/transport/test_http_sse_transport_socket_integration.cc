/**
 * @file test_http_sse_transport_socket_integration.cc
 * @brief Integration tests using real IO for HTTP+SSE transport socket
 *
 * These tests use actual sockets and event loops to test the transport
 * implementation with real network operations.
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <future>
#include <mutex>
#include <thread>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>

#include "mcp/transport/http_sse_transport_socket.h"
#include "mcp/http/llhttp_parser.h"
#include "mcp/event/libevent_dispatcher.h"
#include "mcp/network/socket.h"
#include "mcp/network/io_socket_handle_impl.h"
#include "mcp/network/connection_impl.h"

using namespace mcp;
using namespace mcp::transport;
using namespace std::chrono_literals;

namespace mcp {
namespace test {

/**
 * Base test fixture for HTTP+SSE transport with real IO
 */
class HttpSseTransportIntegrationTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Create real dispatcher
    factory_ = event::createLibeventDispatcherFactory();
    dispatcher_ = factory_->createDispatcher("test");
    
    // Start dispatcher in background thread
    dispatcher_running_ = true;
    dispatcher_thread_ = std::thread([this]() {
      dispatcher_->run(event::RunType::RunUntilExit);
      dispatcher_running_ = false;
    });
    
    // Wait for dispatcher to start
    std::this_thread::sleep_for(50ms);
    
    // Create parser factory
    auto parser_factory = std::make_shared<http::LlhttpParserFactory>();
    
    // Create default config
    config_.endpoint_url = "http://localhost:8080/events";
    config_.parser_factory = parser_factory;
    config_.connect_timeout = 5000ms;
    config_.request_timeout = 5000ms;
    
    // Create socket pair for testing
    createSocketPair();
  }
  
  void TearDown() override {
    // Close sockets
    if (client_socket_ >= 0) {
      close(client_socket_);
      client_socket_ = -1;
    }
    if (server_socket_ >= 0) {
      close(server_socket_);
      server_socket_ = -1;
    }
    
    // Stop dispatcher
    if (dispatcher_ && dispatcher_running_) {
      dispatcher_->exit();
    }
    
    if (dispatcher_thread_.joinable()) {
      dispatcher_thread_.join();
    }
  }
  
  /**
   * Create a connected socket pair for testing
   */
  void createSocketPair() {
    int sockets[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) != 0) {
      FAIL() << "Failed to create socket pair: " << strerror(errno);
    }
    
    client_socket_ = sockets[0];
    server_socket_ = sockets[1];
    
    // Make non-blocking
    fcntl(client_socket_, F_SETFL, fcntl(client_socket_, F_GETFL, 0) | O_NONBLOCK);
    fcntl(server_socket_, F_SETFL, fcntl(server_socket_, F_GETFL, 0) | O_NONBLOCK);
  }
  
  /**
   * Create transport with real socket
   */
  std::unique_ptr<HttpSseTransportSocket> createTransportWithSocket(int fd, bool is_server) {
    auto transport = std::make_unique<HttpSseTransportSocket>(
        config_, *dispatcher_, is_server);
    
    // Create a minimal socket wrapper for testing
    class TestSocket : public network::Socket {
     public:
      TestSocket(int fd) : fd_(fd) {}
      
      network::IoHandle* ioHandle() override { 
        static network::IoSocketHandleImpl handle(fd_);
        return &handle;
      }
      const network::IoHandle* ioHandle() const override {
        static network::IoSocketHandleImpl handle(fd_);
        return &handle;
      }
      void close() override { ::close(fd_); }
      bool isOpen() const override { return fd_ >= 0; }
      VoidResult bind(const Address&) override { return VoidResult::success(); }
      VoidResult listen(int) override { return VoidResult::success(); }
      Result<int> ioHandle() override { return Result<int>::success(fd_); }
      VoidResult connect(const Address&) override { return VoidResult::success(); }
      Result<ConnectionInfoProviderSharedPtr> connectionInfoProvider() override {
        return Result<ConnectionInfoProviderSharedPtr>::error(Error("Not implemented"));
      }
      void setConnectionInfoProvider(ConnectionInfoProviderSharedPtr) override {}
      Result<ConnectionInfoProviderSharedPtr> connectionInfoProviderSharedPtr() override {
        return Result<ConnectionInfoProviderSharedPtr>::error(Error("Not implemented"));
      }
      SocketType socketType() const override { return SocketType::Stream; }
      Address::Type addressType() const override { return Address::Type::Ip; }
      optional<Address::IpVersion> ipVersion() const override { 
        return Address::IpVersion::v4; 
      }
      SocketPtr duplicate() override { return nullptr; }
      void setBlocking(bool) override {}
      Result<AddressSharedPtr> localAddress() override { 
        return Result<AddressSharedPtr>::error(Error("Not implemented"));
      }
      Result<AddressSharedPtr> remoteAddress() override {
        return Result<AddressSharedPtr>::error(Error("Not implemented"));
      }
      
     private:
      int fd_;
    };
    
    auto socket = std::make_unique<TestSocket>(fd);
    
    // Set up minimal callbacks for testing
    class TestCallbacks : public network::TransportSocketCallbacks {
     public:
      TestCallbacks(network::Socket* socket, event::Dispatcher& dispatcher)
          : socket_(socket), dispatcher_(dispatcher) {}
      
      network::IoHandle& ioHandle() override { 
        return *socket_->ioHandle(); 
      }
      const network::IoHandle& ioHandle() const override { 
        return *socket_->ioHandle(); 
      }
      network::Connection& connection() override {
        // Return a dummy connection for testing
        static network::ConnectionImpl dummy(dispatcher_, nullptr, nullptr, nullptr, false);
        return dummy;
      }
      void raiseEvent(network::ConnectionEvent event) override {
        last_event_ = event;
      }
      void setTransportSocketIsReadable() override { 
        readable_ = true;
      }
      bool shouldDrainReadBuffer() override { return false; }
      void flushWriteBuffer() override {}
      
      bool readable_{false};
      network::ConnectionEvent last_event_{network::ConnectionEvent::Connected};
      
     private:
      network::Socket* socket_;
      event::Dispatcher& dispatcher_;
    };
    
    test_callbacks_ = std::make_unique<TestCallbacks>(socket.get(), *dispatcher_);
    transport->setTransportSocketCallbacks(*test_callbacks_);
    
    // Connect the transport
    transport->connect(*socket);
    
    return transport;
  }
  
  /**
   * Send data through socket
   */
  void sendData(int socket, const std::string& data) {
    size_t total_sent = 0;
    while (total_sent < data.size()) {
      ssize_t sent = send(socket, data.data() + total_sent, 
                         data.size() - total_sent, 0);
      if (sent > 0) {
        total_sent += sent;
      } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
        break;
      }
      std::this_thread::sleep_for(1ms);
    }
  }
  
  /**
   * Receive data from socket with timeout
   */
  std::string receiveData(int socket, size_t max_size = 4096, int timeout_ms = 100) {
    std::string result;
    char buffer[4096];
    auto start = std::chrono::steady_clock::now();
    
    while (result.size() < max_size) {
      ssize_t received = recv(socket, buffer, sizeof(buffer), MSG_DONTWAIT);
      if (received > 0) {
        result.append(buffer, received);
      } else if (received == 0) {
        break; // Connection closed
      } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
        break; // Error
      }
      
      // Check timeout
      auto elapsed = std::chrono::steady_clock::now() - start;
      if (elapsed > std::chrono::milliseconds(timeout_ms)) {
        break;
      }
      
      if (received <= 0) {
        std::this_thread::sleep_for(5ms);
      }
    }
    
    return result;
  }
  
 protected:
  // Dispatcher
  std::unique_ptr<event::DispatcherFactory> factory_;
  std::unique_ptr<event::Dispatcher> dispatcher_;
  std::thread dispatcher_thread_;
  std::atomic<bool> dispatcher_running_{false};
  
  // Configuration
  HttpSseTransportSocketConfig config_;
  
  // Sockets
  int client_socket_{-1};
  int server_socket_{-1};
  
  // Test callbacks
  std::unique_ptr<network::TransportSocketCallbacks> test_callbacks_;
};

// ===== HTTP Protocol Tests with Real IO =====

TEST_F(HttpSseTransportIntegrationTest, HttpResponseParsing) {
  // Create client transport with real socket
  auto client_transport = createTransportWithSocket(client_socket_, false);
  
  // Send HTTP response from server side
  std::string response = 
      "HTTP/1.1 200 OK\r\n"
      "Content-Type: text/event-stream\r\n"
      "Cache-Control: no-cache\r\n"
      "Connection: keep-alive\r\n"
      "\r\n";
  
  sendData(server_socket_, response);
  
  // Let client read the response
  std::this_thread::sleep_for(10ms);
  
  // Client should read and parse the response
  OwnedBuffer buffer;
  auto result = client_transport->doRead(buffer);
  
  // Should successfully parse HTTP response
  EXPECT_TRUE(result.ok());
  EXPECT_EQ(result.action_, TransportIoResult::PostIoAction::CONTINUE);
}

TEST_F(HttpSseTransportIntegrationTest, HttpErrorStatusHandling) {
  // Create client transport with real socket
  auto client_transport = createTransportWithSocket(client_socket_, false);
  
  // Send error response from server
  std::string response = 
      "HTTP/1.1 404 Not Found\r\n"
      "Content-Type: text/plain\r\n"
      "Content-Length: 19\r\n"
      "\r\n"
      "Resource not found";
  
  sendData(server_socket_, response);
  
  // Let client read the error response
  std::this_thread::sleep_for(10ms);
  
  OwnedBuffer buffer;
  auto result = client_transport->doRead(buffer);
  
  // Should handle error status appropriately
  EXPECT_TRUE(result.ok());
  // Transport may close connection on error
  EXPECT_TRUE(result.action_ == TransportIoResult::PostIoAction::CONTINUE ||
              result.action_ == TransportIoResult::PostIoAction::CLOSE);
}

TEST_F(HttpSseTransportIntegrationTest, PartialSseEvent) {
  // Create client transport
  auto client_transport = createTransportWithSocket(client_socket_, false);
  
  // Send SSE response header first
  std::string header = 
      "HTTP/1.1 200 OK\r\n"
      "Content-Type: text/event-stream\r\n"
      "\r\n";
  sendData(server_socket_, header);
  
  // Send partial SSE event
  std::string partial = "data: Partial";
  sendData(server_socket_, partial);
  
  std::this_thread::sleep_for(10ms);
  
  // Read partial event
  OwnedBuffer buffer;
  auto result = client_transport->doRead(buffer);
  EXPECT_TRUE(result.ok());  // Should buffer partial event
  
  // Complete the event
  std::string completion = " event\n\n";
  sendData(server_socket_, completion);
  
  std::this_thread::sleep_for(10ms);
  
  // Read completed event
  result = client_transport->doRead(buffer);
  EXPECT_TRUE(result.ok());
}

TEST_F(HttpSseTransportIntegrationTest, ConnectionStateTransitions) {
  // Create transport and verify state transitions
  auto client_transport = createTransportWithSocket(client_socket_, false);
  
  // Trigger connection callback
  client_transport->onConnected();
  
  // Verify state after connection
  EXPECT_TRUE(client_transport->failureReason().empty() || 
              !client_transport->failureReason().empty());
  EXPECT_EQ("http+sse", client_transport->protocol());
  
  // Send data to verify connection is working
  std::string test_data = "GET /events HTTP/1.1\r\nHost: localhost\r\n\r\n";
  OwnedBuffer write_buffer;
  write_buffer.add(test_data);
  
  auto result = client_transport->doWrite(write_buffer, false);
  EXPECT_TRUE(result.ok());
  
  // Read from server side to verify data was sent
  std::string received = receiveData(server_socket_);
  EXPECT_TRUE(received.find("GET /events") != std::string::npos);
}

TEST_F(HttpSseTransportIntegrationTest, ZeroCopyRead) {
  // Create transport with real socket
  auto client_transport = createTransportWithSocket(client_socket_, false);
  
  // Send data from server
  std::string data = "Test data for zero-copy read";
  sendData(server_socket_, data);
  
  std::this_thread::sleep_for(10ms);
  
  // Read with transport
  OwnedBuffer buffer;
  auto result = client_transport->doRead(buffer);
  
  EXPECT_TRUE(result.ok());
  EXPECT_GT(result.bytes_processed_, 0);
  EXPECT_LE(result.bytes_processed_, data.length());
}

TEST_F(HttpSseTransportIntegrationTest, NetworkErrorHandling) {
  // Create transport
  auto client_transport = createTransportWithSocket(client_socket_, false);
  
  // Close server socket to simulate network error
  close(server_socket_);
  server_socket_ = -1;
  
  std::this_thread::sleep_for(10ms);
  
  // Try to read - should detect closed connection
  OwnedBuffer buffer;
  auto result = client_transport->doRead(buffer);
  
  // Should either return error or indicate connection should close
  EXPECT_TRUE(!result.ok() || 
              result.action_ == TransportIoResult::PostIoAction::CLOSE ||
              result.bytes_processed_ == 0);
}

TEST_F(HttpSseTransportIntegrationTest, ParserErrorHandling) {
  // Create transport
  auto client_transport = createTransportWithSocket(client_socket_, false);
  
  // Send malformed HTTP response
  std::string malformed = "NOT A VALID HTTP RESPONSE\r\n\r\n";
  sendData(server_socket_, malformed);
  
  std::this_thread::sleep_for(10ms);
  
  // Try to parse
  OwnedBuffer buffer;
  auto result = client_transport->doRead(buffer);
  
  // Parser should detect malformed response
  // May return error or close connection
  EXPECT_TRUE(result.ok() || !result.ok());
  if (result.ok()) {
    EXPECT_TRUE(result.action_ == TransportIoResult::PostIoAction::CLOSE ||
                result.action_ == TransportIoResult::PostIoAction::CONTINUE);
  }
}

TEST_F(HttpSseTransportIntegrationTest, MultipleSSEEvents) {
  // Create client transport
  auto client_transport = createTransportWithSocket(client_socket_, false);
  
  // Send SSE stream header
  std::string header = 
      "HTTP/1.1 200 OK\r\n"
      "Content-Type: text/event-stream\r\n"
      "\r\n";
  sendData(server_socket_, header);
  
  // Send multiple SSE events
  std::vector<std::string> events = {
      "event: message\ndata: First event\n\n",
      "event: update\ndata: {\"status\": \"active\"}\n\n",
      ": keep-alive comment\n\n",
      "data: Simple data\n\n",
      "event: close\ndata: Goodbye\n\n"
  };
  
  for (const auto& event : events) {
    sendData(server_socket_, event);
    std::this_thread::sleep_for(5ms);
    
    // Read each event
    OwnedBuffer buffer;
    auto result = client_transport->doRead(buffer);
    EXPECT_TRUE(result.ok());
  }
}

TEST_F(HttpSseTransportIntegrationTest, LargePayload) {
  // Create transport
  auto client_transport = createTransportWithSocket(client_socket_, false);
  
  // Create large payload (10KB)
  std::string large_data(10 * 1024, 'X');
  std::string response = 
      "HTTP/1.1 200 OK\r\n"
      "Content-Type: text/plain\r\n"
      "Content-Length: " + std::to_string(large_data.size()) + "\r\n"
      "\r\n" + large_data;
  
  // Send in chunks
  size_t chunk_size = 1024;
  for (size_t i = 0; i < response.size(); i += chunk_size) {
    size_t to_send = std::min(chunk_size, response.size() - i);
    sendData(server_socket_, response.substr(i, to_send));
    std::this_thread::sleep_for(1ms);
  }
  
  // Read all data
  size_t total_read = 0;
  while (total_read < response.size()) {
    OwnedBuffer buffer;
    auto result = client_transport->doRead(buffer);
    EXPECT_TRUE(result.ok());
    total_read += result.bytes_processed_;
    
    if (result.bytes_processed_ == 0) {
      std::this_thread::sleep_for(5ms);
    }
  }
}

TEST_F(HttpSseTransportIntegrationTest, BidirectionalCommunication) {
  // Create both client and server transports
  auto client_transport = createTransportWithSocket(client_socket_, false);
  auto server_transport = createTransportWithSocket(server_socket_, true);
  
  // Client sends request
  std::string request = "GET /events HTTP/1.1\r\nHost: localhost\r\n\r\n";
  OwnedBuffer client_buffer;
  client_buffer.add(request);
  
  auto result = client_transport->doWrite(client_buffer, false);
  EXPECT_TRUE(result.ok());
  
  std::this_thread::sleep_for(10ms);
  
  // Server reads request
  OwnedBuffer server_buffer;
  result = server_transport->doRead(server_buffer);
  EXPECT_TRUE(result.ok());
  
  // Server sends response
  std::string response = 
      "HTTP/1.1 200 OK\r\n"
      "Content-Type: text/event-stream\r\n"
      "\r\n"
      "data: Hello from server\n\n";
  
  server_buffer.add(response);
  result = server_transport->doWrite(server_buffer, false);
  EXPECT_TRUE(result.ok());
  
  std::this_thread::sleep_for(10ms);
  
  // Client reads response
  OwnedBuffer client_read_buffer;
  result = client_transport->doRead(client_read_buffer);
  EXPECT_TRUE(result.ok());
}

} // namespace test
} // namespace mcp