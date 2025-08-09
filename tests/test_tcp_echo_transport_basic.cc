/**
 * @file test_tcp_echo_transport_basic.cc
 * @brief APPLICATION LEVEL TESTS for TCP echo transport
 * 
 * TEST LEVEL: End-to-end application testing
 * 
 * This file tests the TCP transport implementation by creating actual
 * TCP sockets and testing communication between client and server
 * transports. It validates the complete TCP transport functionality.
 * 
 * What this tests:
 * - TCP transport creation and configuration
 * - Client-server socket connection establishment
 * - Data transmission over TCP sockets
 * - Connection handling and error scenarios
 * - Transport abstraction compatibility
 * - Thread safety and concurrent operations
 * 
 * What this does NOT test:
 * - Low-level socket implementation details
 * - OS-specific networking behavior
 * - Network packet-level communication
 * 
 * For transport-level testing, see:
 * - tests/transport/test_tcp_transport_*.cc (when created)
 */

#include <gtest/gtest.h>
#include <thread>
#include <atomic>
#include <chrono>
#include <future>
#include <string>
#include <vector>

#include "mcp/echo/tcp_transport.h"
#include "mcp/echo/echo_basic.h"

namespace mcp {
namespace echo {
namespace test {

/**
 * Test fixture for TCP transport tests
 */
class TCPTransportTest : public ::testing::Test {
protected:
  void SetUp() override {
    // Use a unique port for each test to avoid conflicts
    test_port_ = 19000 + (rand() % 1000);
  }
  
  void TearDown() override {
    // Clean up any running transports
    if (server_transport_) {
      server_transport_->stop();
    }
    if (client_transport_) {
      client_transport_->stop();
    }
  }
  
  int test_port_;
  std::unique_ptr<TCPTransport> server_transport_;
  std::unique_ptr<TCPTransport> client_transport_;
};

/**
 * Test basic TCP transport creation and configuration
 */
TEST_F(TCPTransportTest, TransportCreation) {
  // Test server transport creation
  auto server = createTCPServerTransport();
  ASSERT_NE(server, nullptr);
  EXPECT_EQ(server->getTransportType(), "tcp-server");
  EXPECT_FALSE(server->isConnected());
  
  // Test client transport creation
  auto client = createTCPClientTransport();
  ASSERT_NE(client, nullptr);
  EXPECT_EQ(client->getTransportType(), "tcp-client");
  EXPECT_FALSE(client->isConnected());
}

/**
 * Test server configuration and startup
 */
TEST_F(TCPTransportTest, ServerConfiguration) {
  server_transport_ = std::unique_ptr<TCPTransport>(
      static_cast<TCPTransport*>(createTCPServerTransport().release()));
  
  // Configure server
  EXPECT_TRUE(server_transport_->listen(test_port_));
  
  // Start server
  EXPECT_TRUE(server_transport_->start());
  
  // Give server time to start listening
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  
  // Verify server is not yet connected (no client)
  EXPECT_FALSE(server_transport_->isConnected());
}

/**
 * Test client configuration
 */
TEST_F(TCPTransportTest, ClientConfiguration) {
  client_transport_ = std::unique_ptr<TCPTransport>(
      static_cast<TCPTransport*>(createTCPClientTransport().release()));
  
  // Configure client
  EXPECT_TRUE(client_transport_->connect("127.0.0.1", test_port_));
  
  // Client should start successfully even without server
  EXPECT_TRUE(client_transport_->start());
  
  // Client should not be connected without server
  EXPECT_FALSE(client_transport_->isConnected());
}

/**
 * Test client-server connection establishment
 */
TEST_F(TCPTransportTest, ClientServerConnection) {
  // Create and configure server
  server_transport_ = std::unique_ptr<TCPTransport>(
      static_cast<TCPTransport*>(createTCPServerTransport().release()));
  ASSERT_TRUE(server_transport_->listen(test_port_));
  
  // Create and configure client  
  client_transport_ = std::unique_ptr<TCPTransport>(
      static_cast<TCPTransport*>(createTCPClientTransport().release()));
  ASSERT_TRUE(client_transport_->connect("127.0.0.1", test_port_));
  
  // Setup connection callbacks
  std::promise<bool> server_connected_promise;
  std::promise<bool> client_connected_promise;
  
  server_transport_->setConnectionCallback([&](bool connected) {
    if (connected) {
      server_connected_promise.set_value(true);
    }
  });
  
  client_transport_->setConnectionCallback([&](bool connected) {
    if (connected) {
      client_connected_promise.set_value(true);
    }
  });
  
  // Start server first
  ASSERT_TRUE(server_transport_->start());
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  
  // Start client
  ASSERT_TRUE(client_transport_->start());
  
  // Wait for connections
  auto server_future = server_connected_promise.get_future();
  auto client_future = client_connected_promise.get_future();
  
  EXPECT_TRUE(server_future.wait_for(std::chrono::seconds(5)) == std::future_status::ready);
  EXPECT_TRUE(client_future.wait_for(std::chrono::seconds(5)) == std::future_status::ready);
  
  EXPECT_TRUE(server_future.get());
  EXPECT_TRUE(client_future.get());
  
  // Verify both sides show connected
  EXPECT_TRUE(server_transport_->isConnected());
  EXPECT_TRUE(client_transport_->isConnected());
}

/**
 * Test data transmission from client to server
 */
TEST_F(TCPTransportTest, DataTransmissionClientToServer) {
  // Setup server and client connection (reusing previous test logic)
  server_transport_ = std::unique_ptr<TCPTransport>(
      static_cast<TCPTransport*>(createTCPServerTransport().release()));
  ASSERT_TRUE(server_transport_->listen(test_port_));
  
  client_transport_ = std::unique_ptr<TCPTransport>(
      static_cast<TCPTransport*>(createTCPClientTransport().release()));
  ASSERT_TRUE(client_transport_->connect("127.0.0.1", test_port_));
  
  // Setup data callback for server
  std::promise<std::string> data_promise;
  server_transport_->setDataCallback([&](const std::string& data) {
    data_promise.set_value(data);
  });
  
  // Setup connection callbacks and wait for connection
  std::promise<void> connection_promise;
  std::atomic<int> connections_ready(0);
  
  auto connection_callback = [&](bool connected) {
    if (connected && (++connections_ready == 2)) {
      connection_promise.set_value();
    }
  };
  
  server_transport_->setConnectionCallback(connection_callback);
  client_transport_->setConnectionCallback(connection_callback);
  
  // Start both transports
  ASSERT_TRUE(server_transport_->start());
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  ASSERT_TRUE(client_transport_->start());
  
  // Wait for connections
  auto conn_future = connection_promise.get_future();
  ASSERT_TRUE(conn_future.wait_for(std::chrono::seconds(5)) == std::future_status::ready);
  
  // Send data from client to server
  const std::string test_data = "Hello from TCP client!";
  client_transport_->send(test_data);
  
  // Wait for data to arrive at server
  auto data_future = data_promise.get_future();
  ASSERT_TRUE(data_future.wait_for(std::chrono::seconds(5)) == std::future_status::ready);
  
  EXPECT_EQ(data_future.get(), test_data);
}

/**
 * Test bidirectional data transmission
 */
TEST_F(TCPTransportTest, BidirectionalDataTransmission) {
  // Setup server and client
  server_transport_ = std::unique_ptr<TCPTransport>(
      static_cast<TCPTransport*>(createTCPServerTransport().release()));
  ASSERT_TRUE(server_transport_->listen(test_port_));
  
  client_transport_ = std::unique_ptr<TCPTransport>(
      static_cast<TCPTransport*>(createTCPClientTransport().release()));
  ASSERT_TRUE(client_transport_->connect("127.0.0.1", test_port_));
  
  // Setup data collection
  std::vector<std::string> server_received;
  std::vector<std::string> client_received;
  std::mutex server_data_mutex, client_data_mutex;
  
  server_transport_->setDataCallback([&](const std::string& data) {
    std::lock_guard<std::mutex> lock(server_data_mutex);
    server_received.push_back(data);
  });
  
  client_transport_->setDataCallback([&](const std::string& data) {
    std::lock_guard<std::mutex> lock(client_data_mutex);
    client_received.push_back(data);
  });
  
  // Wait for connection
  std::promise<void> connection_promise;
  std::atomic<int> connections_ready(0);
  
  auto connection_callback = [&](bool connected) {
    if (connected && (++connections_ready == 2)) {
      connection_promise.set_value();
    }
  };
  
  server_transport_->setConnectionCallback(connection_callback);
  client_transport_->setConnectionCallback(connection_callback);
  
  ASSERT_TRUE(server_transport_->start());
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  ASSERT_TRUE(client_transport_->start());
  
  auto conn_future = connection_promise.get_future();
  ASSERT_TRUE(conn_future.wait_for(std::chrono::seconds(5)) == std::future_status::ready);
  
  // Send data in both directions
  const std::string client_to_server = "Client to Server";
  const std::string server_to_client = "Server to Client";
  
  client_transport_->send(client_to_server);
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  
  server_transport_->send(server_to_client);
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  
  // Verify data received
  {
    std::lock_guard<std::mutex> lock(server_data_mutex);
    EXPECT_EQ(server_received.size(), 1);
    if (!server_received.empty()) {
      EXPECT_EQ(server_received[0], client_to_server);
    }
  }
  
  {
    std::lock_guard<std::mutex> lock(client_data_mutex);
    EXPECT_EQ(client_received.size(), 1);
    if (!client_received.empty()) {
      EXPECT_EQ(client_received[0], server_to_client);
    }
  }
}

/**
 * Test JSON-RPC message transmission (integration with echo system)
 */
TEST_F(TCPTransportTest, JSONRPCIntegration) {
  // This test verifies that TCP transport works with the echo system
  // by sending actual JSON-RPC messages
  
  server_transport_ = std::unique_ptr<TCPTransport>(
      static_cast<TCPTransport*>(createTCPServerTransport().release()));
  ASSERT_TRUE(server_transport_->listen(test_port_));
  
  client_transport_ = std::unique_ptr<TCPTransport>(
      static_cast<TCPTransport*>(createTCPClientTransport().release()));
  ASSERT_TRUE(client_transport_->connect("127.0.0.1", test_port_));
  
  // Setup to capture JSON-RPC messages
  std::promise<std::string> json_promise;
  server_transport_->setDataCallback([&](const std::string& data) {
    json_promise.set_value(data);
  });
  
  // Wait for connection
  std::promise<void> connection_promise;
  std::atomic<int> connections_ready(0);
  
  auto connection_callback = [&](bool connected) {
    if (connected && (++connections_ready == 2)) {
      connection_promise.set_value();
    }
  };
  
  server_transport_->setConnectionCallback(connection_callback);
  client_transport_->setConnectionCallback(connection_callback);
  
  ASSERT_TRUE(server_transport_->start());
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  ASSERT_TRUE(client_transport_->start());
  
  auto conn_future = connection_promise.get_future();
  ASSERT_TRUE(conn_future.wait_for(std::chrono::seconds(5)) == std::future_status::ready);
  
  // Send a JSON-RPC request
  const std::string json_request = R"({"jsonrpc":"2.0","method":"test","id":1})";
  client_transport_->send(json_request + "\n");
  
  // Verify JSON-RPC message received
  auto json_future = json_promise.get_future();
  ASSERT_TRUE(json_future.wait_for(std::chrono::seconds(5)) == std::future_status::ready);
  
  std::string received = json_future.get();
  EXPECT_EQ(received, json_request + "\n");
}

/**
 * Test transport error handling
 */
TEST_F(TCPTransportTest, ErrorHandling) {
  client_transport_ = std::unique_ptr<TCPTransport>(
      static_cast<TCPTransport*>(createTCPClientTransport().release()));
  
  // Try to connect to non-existent server
  EXPECT_TRUE(client_transport_->connect("127.0.0.1", test_port_));
  EXPECT_TRUE(client_transport_->start());
  
  // Should not be connected
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  EXPECT_FALSE(client_transport_->isConnected());
  
  // Sending data should not crash when not connected
  client_transport_->send("test data");
  EXPECT_FALSE(client_transport_->isConnected());
}

} // namespace test
} // namespace echo
} // namespace mcp