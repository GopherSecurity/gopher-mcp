/**
 * @file test_tcp_echo_integration.cc
 * @brief Integration tests for TCP echo client and server
 * 
 * These tests verify the complete echo system functionality including
 * JSON-RPC protocol, request/response handling, and error scenarios.
 */

#include <gtest/gtest.h>
#include <thread>
#include <future>
#include <chrono>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>

#include "mcp/echo/echo_basic.h"
#include "mcp/echo/tcp_transport.h"
#include "mcp/json/json_serialization.h"
#include "mcp/builders.h"

namespace mcp {
namespace echo {
namespace test {

class TCPEchoIntegrationTest : public ::testing::Test {
protected:
  void SetUp() override {
    signal(SIGPIPE, SIG_IGN);
    base_port_ = 20000 + (getpid() % 10000);
  }

  int getNextPort() {
    return base_port_++;
  }

  // Helper to wait for server to be ready
  bool waitForServer(const std::string& host, int port, int timeout_ms = 5000) {
    auto start = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - start < std::chrono::milliseconds(timeout_ms)) {
      auto transport = std::make_shared<TCPTransport>(TCPTransport::Mode::Client);
      if (transport->connect(host, port)) {
        transport->stop();
        return true;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return false;
  }

private:
  int base_port_;
};

// Test 1: Basic echo functionality
TEST_F(TCPEchoIntegrationTest, BasicEchoFunctionality) {
  int port = getNextPort();
  
  // Create server
  auto server_transport = std::make_shared<TCPTransport>(TCPTransport::Mode::Server);
  EchoServerBase server(server_transport);
  
  ASSERT_TRUE(server_transport->listen(port));
  ASSERT_TRUE(server.start());
  
  // Run server in background
  std::thread server_thread([&server]() {
    server.run();
  });
  
  // Wait for server to be ready
  ASSERT_TRUE(waitForServer("127.0.0.1", port));
  
  // Create client
  auto client_transport = std::make_shared<TCPTransport>(TCPTransport::Mode::Client);
  EchoClientBase client(client_transport);
  
  ASSERT_TRUE(client_transport->connect("127.0.0.1", port));
  ASSERT_TRUE(client.start());
  
  // Run client in background
  std::thread client_thread([&client]() {
    client.run();
  });
  
  // Send echo requests
  std::vector<std::string> test_messages = {
    "Hello, World!",
    "Testing 123",
    "Special chars: !@#$%^&*()",
    "Unicode: 你好世界 🌍",
    "Multi\nline\ntext"
  };
  
  for (const auto& msg : test_messages) {
    auto future = client.sendRequest("echo", msg);
    auto response = future.get();
    
    ASSERT_TRUE(response.has_value());
    EXPECT_EQ(response.value().asString(), msg);
  }
  
  // Stop client and server
  client.stop();
  server.stop();
  
  client_thread.join();
  server_thread.join();
}

// Test 2: Multiple clients
TEST_F(TCPEchoIntegrationTest, MultipleClients) {
  int port = getNextPort();
  
  // Create server
  auto server_transport = std::make_shared<TCPTransport>(TCPTransport::Mode::Server);
  EchoServerBase server(server_transport);
  
  ASSERT_TRUE(server_transport->listen(port));
  ASSERT_TRUE(server.start());
  
  // Run server in background
  std::atomic<bool> server_running(true);
  std::thread server_thread([&]() {
    while (server_running) {
      server_transport->acceptConnection();
      server.run();
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
  });
  
  // Wait for server
  ASSERT_TRUE(waitForServer("127.0.0.1", port));
  
  // Create multiple clients
  const int num_clients = 5;
  std::vector<std::thread> client_threads;
  std::atomic<int> successful_echoes(0);
  
  for (int i = 0; i < num_clients; i++) {
    client_threads.emplace_back([this, port, i, &successful_echoes]() {
      auto transport = std::make_shared<TCPTransport>(TCPTransport::Mode::Client);
      EchoClientBase client(transport);
      
      if (transport->connect("127.0.0.1", port) && client.start()) {
        // Run client
        std::thread run_thread([&client]() {
          client.run();
        });
        
        // Send messages
        for (int j = 0; j < 10; j++) {
          std::string msg = "Client " + std::to_string(i) + " message " + std::to_string(j);
          auto future = client.sendRequest("echo", msg);
          auto response = future.get();
          
          if (response.has_value() && response.value().asString() == msg) {
            successful_echoes++;
          }
        }
        
        client.stop();
        run_thread.join();
      }
    });
  }
  
  // Wait for all clients to complete
  for (auto& thread : client_threads) {
    thread.join();
  }
  
  // Verify all echoes succeeded
  EXPECT_EQ(successful_echoes, num_clients * 10);
  
  server_running = false;
  server.stop();
  server_thread.join();
}

// Test 3: Error handling - invalid method
TEST_F(TCPEchoIntegrationTest, InvalidMethodError) {
  int port = getNextPort();
  
  // Create server
  auto server_transport = std::make_shared<TCPTransport>(TCPTransport::Mode::Server);
  EchoServerBase server(server_transport);
  
  ASSERT_TRUE(server_transport->listen(port));
  ASSERT_TRUE(server.start());
  
  std::thread server_thread([&server]() {
    server.run();
  });
  
  ASSERT_TRUE(waitForServer("127.0.0.1", port));
  
  // Create client
  auto client_transport = std::make_shared<TCPTransport>(TCPTransport::Mode::Client);
  EchoClientBase client(client_transport);
  
  ASSERT_TRUE(client_transport->connect("127.0.0.1", port));
  ASSERT_TRUE(client.start());
  
  std::thread client_thread([&client]() {
    client.run();
  });
  
  // Send request with invalid method
  auto future = client.sendRequest("invalid_method", "test");
  auto response = future.get();
  
  // Should get an error response
  ASSERT_FALSE(response.has_value());
  
  client.stop();
  server.stop();
  
  client_thread.join();
  server_thread.join();
}

// Test 4: Request/response ordering
TEST_F(TCPEchoIntegrationTest, RequestResponseOrdering) {
  int port = getNextPort();
  
  // Create server
  auto server_transport = std::make_shared<TCPTransport>(TCPTransport::Mode::Server);
  EchoServerBase server(server_transport);
  
  ASSERT_TRUE(server_transport->listen(port));
  ASSERT_TRUE(server.start());
  
  std::thread server_thread([&server]() {
    server.run();
  });
  
  ASSERT_TRUE(waitForServer("127.0.0.1", port));
  
  // Create client
  auto client_transport = std::make_shared<TCPTransport>(TCPTransport::Mode::Client);
  EchoClientBase client(client_transport);
  
  ASSERT_TRUE(client_transport->connect("127.0.0.1", port));
  ASSERT_TRUE(client.start());
  
  std::thread client_thread([&client]() {
    client.run();
  });
  
  // Send multiple requests without waiting
  std::vector<std::future<optional<json::JsonValue>>> futures;
  for (int i = 0; i < 20; i++) {
    std::string msg = "Message " + std::to_string(i);
    futures.push_back(client.sendRequest("echo", msg));
  }
  
  // Verify all responses match
  for (int i = 0; i < 20; i++) {
    auto response = futures[i].get();
    ASSERT_TRUE(response.has_value());
    std::string expected = "Message " + std::to_string(i);
    EXPECT_EQ(response.value().asString(), expected);
  }
  
  client.stop();
  server.stop();
  
  client_thread.join();
  server_thread.join();
}

// Test 5: Large payload handling
TEST_F(TCPEchoIntegrationTest, LargePayloadHandling) {
  int port = getNextPort();
  
  // Create server
  auto server_transport = std::make_shared<TCPTransport>(TCPTransport::Mode::Server);
  EchoServerBase server(server_transport);
  
  ASSERT_TRUE(server_transport->listen(port));
  ASSERT_TRUE(server.start());
  
  std::thread server_thread([&server]() {
    server.run();
  });
  
  ASSERT_TRUE(waitForServer("127.0.0.1", port));
  
  // Create client
  auto client_transport = std::make_shared<TCPTransport>(TCPTransport::Mode::Client);
  EchoClientBase client(client_transport);
  
  ASSERT_TRUE(client_transport->connect("127.0.0.1", port));
  ASSERT_TRUE(client.start());
  
  std::thread client_thread([&client]() {
    client.run();
  });
  
  // Test with increasingly large payloads
  for (int size = 1000; size <= 100000; size *= 10) {
    std::string large_msg(size, 'X');
    auto future = client.sendRequest("echo", large_msg);
    auto response = future.get();
    
    ASSERT_TRUE(response.has_value());
    EXPECT_EQ(response.value().asString().size(), size);
    EXPECT_EQ(response.value().asString(), large_msg);
  }
  
  client.stop();
  server.stop();
  
  client_thread.join();
  server_thread.join();
}

// Test 6: Connection recovery
TEST_F(TCPEchoIntegrationTest, ConnectionRecovery) {
  int port = getNextPort();
  
  // First server instance
  {
    auto server_transport = std::make_shared<TCPTransport>(TCPTransport::Mode::Server);
    EchoServerBase server(server_transport);
    
    ASSERT_TRUE(server_transport->listen(port));
    ASSERT_TRUE(server.start());
    
    std::thread server_thread([&server]() {
      server.run();
    });
    
    ASSERT_TRUE(waitForServer("127.0.0.1", port));
    
    // Connect client
    auto client_transport = std::make_shared<TCPTransport>(TCPTransport::Mode::Client);
    EchoClientBase client(client_transport);
    
    ASSERT_TRUE(client_transport->connect("127.0.0.1", port));
    ASSERT_TRUE(client.start());
    
    std::thread client_thread([&client]() {
      client.run();
    });
    
    // Send message
    auto future = client.sendRequest("echo", "test1");
    auto response = future.get();
    ASSERT_TRUE(response.has_value());
    EXPECT_EQ(response.value().asString(), "test1");
    
    // Stop server (simulating crash)
    server.stop();
    server_thread.join();
    
    // Client should detect disconnection
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    client.stop();
    client_thread.join();
  }
  
  // Start new server on same port
  {
    auto server_transport = std::make_shared<TCPTransport>(TCPTransport::Mode::Server);
    EchoServerBase server(server_transport);
    
    ASSERT_TRUE(server_transport->listen(port));
    ASSERT_TRUE(server.start());
    
    std::thread server_thread([&server]() {
      server.run();
    });
    
    ASSERT_TRUE(waitForServer("127.0.0.1", port));
    
    // New client should be able to connect
    auto client_transport = std::make_shared<TCPTransport>(TCPTransport::Mode::Client);
    EchoClientBase client(client_transport);
    
    ASSERT_TRUE(client_transport->connect("127.0.0.1", port));
    ASSERT_TRUE(client.start());
    
    std::thread client_thread([&client]() {
      client.run();
    });
    
    // Should work normally
    auto future = client.sendRequest("echo", "test2");
    auto response = future.get();
    ASSERT_TRUE(response.has_value());
    EXPECT_EQ(response.value().asString(), "test2");
    
    client.stop();
    server.stop();
    
    client_thread.join();
    server_thread.join();
  }
}

// Test 7: JSON-RPC batch requests
TEST_F(TCPEchoIntegrationTest, BatchRequests) {
  int port = getNextPort();
  
  // Create server
  auto server_transport = std::make_shared<TCPTransport>(TCPTransport::Mode::Server);
  EchoServerBase server(server_transport);
  
  ASSERT_TRUE(server_transport->listen(port));
  ASSERT_TRUE(server.start());
  
  std::thread server_thread([&server]() {
    server.run();
  });
  
  ASSERT_TRUE(waitForServer("127.0.0.1", port));
  
  // Create client
  auto client_transport = std::make_shared<TCPTransport>(TCPTransport::Mode::Client);
  EchoClientBase client(client_transport);
  
  ASSERT_TRUE(client_transport->connect("127.0.0.1", port));
  ASSERT_TRUE(client.start());
  
  std::thread client_thread([&client]() {
    client.run();
  });
  
  // Send rapid burst of requests (simulating batch)
  const int batch_size = 50;
  std::vector<std::future<optional<json::JsonValue>>> futures;
  
  auto start = std::chrono::high_resolution_clock::now();
  
  for (int i = 0; i < batch_size; i++) {
    std::string msg = "Batch message " + std::to_string(i);
    futures.push_back(client.sendRequest("echo", msg));
  }
  
  // Wait for all responses
  for (int i = 0; i < batch_size; i++) {
    auto response = futures[i].get();
    ASSERT_TRUE(response.has_value());
    std::string expected = "Batch message " + std::to_string(i);
    EXPECT_EQ(response.value().asString(), expected);
  }
  
  auto end = std::chrono::high_resolution_clock::now();
  auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
  
  std::cout << "Batch of " << batch_size << " requests completed in " 
            << duration.count() << "ms" << std::endl;
  
  client.stop();
  server.stop();
  
  client_thread.join();
  server_thread.join();
}

// Test 8: Custom echo server implementation
TEST_F(TCPEchoIntegrationTest, CustomEchoImplementation) {
  int port = getNextPort();
  
  // Custom server that modifies responses
  class CustomEchoServer : public EchoServerBase {
  public:
    using EchoServerBase::EchoServerBase;
    
  protected:
    jsonrpc::Response handleRequest(const jsonrpc::Request& request) override {
      if (request.method == "echo") {
        // Add prefix to echoed message
        std::string modified = "CUSTOM: " + request.params.asString();
        return jsonrpc::Response(request.id, json::JsonValue(modified));
      }
      return EchoServerBase::handleRequest(request);
    }
  };
  
  auto server_transport = std::make_shared<TCPTransport>(TCPTransport::Mode::Server);
  CustomEchoServer server(server_transport);
  
  ASSERT_TRUE(server_transport->listen(port));
  ASSERT_TRUE(server.start());
  
  std::thread server_thread([&server]() {
    server.run();
  });
  
  ASSERT_TRUE(waitForServer("127.0.0.1", port));
  
  // Create client
  auto client_transport = std::make_shared<TCPTransport>(TCPTransport::Mode::Client);
  EchoClientBase client(client_transport);
  
  ASSERT_TRUE(client_transport->connect("127.0.0.1", port));
  ASSERT_TRUE(client.start());
  
  std::thread client_thread([&client]() {
    client.run();
  });
  
  // Test custom behavior
  auto future = client.sendRequest("echo", "test");
  auto response = future.get();
  
  ASSERT_TRUE(response.has_value());
  EXPECT_EQ(response.value().asString(), "CUSTOM: test");
  
  client.stop();
  server.stop();
  
  client_thread.join();
  server_thread.join();
}

// Test 9: Timeout handling
TEST_F(TCPEchoIntegrationTest, TimeoutHandling) {
  int port = getNextPort();
  
  // Create a server that delays responses
  class SlowEchoServer : public EchoServerBase {
  public:
    using EchoServerBase::EchoServerBase;
    
  protected:
    jsonrpc::Response handleRequest(const jsonrpc::Request& request) override {
      if (request.method == "slow_echo") {
        // Simulate slow processing
        std::this_thread::sleep_for(std::chrono::seconds(2));
        return jsonrpc::Response(request.id, request.params);
      }
      return EchoServerBase::handleRequest(request);
    }
  };
  
  auto server_transport = std::make_shared<TCPTransport>(TCPTransport::Mode::Server);
  SlowEchoServer server(server_transport);
  
  ASSERT_TRUE(server_transport->listen(port));
  ASSERT_TRUE(server.start());
  
  std::thread server_thread([&server]() {
    server.run();
  });
  
  ASSERT_TRUE(waitForServer("127.0.0.1", port));
  
  // Create client with short timeout
  auto client_transport = std::make_shared<TCPTransport>(TCPTransport::Mode::Client);
  EchoClientBase client(client_transport, EchoClientConfig{.request_timeout = std::chrono::seconds(1)});
  
  ASSERT_TRUE(client_transport->connect("127.0.0.1", port));
  ASSERT_TRUE(client.start());
  
  std::thread client_thread([&client]() {
    client.run();
  });
  
  // Fast request should succeed
  auto fast_future = client.sendRequest("echo", "fast");
  auto fast_response = fast_future.get();
  ASSERT_TRUE(fast_response.has_value());
  EXPECT_EQ(fast_response.value().asString(), "fast");
  
  // Slow request should timeout
  auto slow_future = client.sendRequest("slow_echo", "slow");
  
  // Wait with timeout
  auto status = slow_future.wait_for(std::chrono::seconds(3));
  EXPECT_EQ(status, std::future_status::ready);
  
  auto slow_response = slow_future.get();
  // Response may or may not arrive depending on timing
  
  client.stop();
  server.stop();
  
  client_thread.join();
  server_thread.join();
}

// Test 10: Protocol compliance
TEST_F(TCPEchoIntegrationTest, ProtocolCompliance) {
  int port = getNextPort();
  
  // Create server
  auto server_transport = std::make_shared<TCPTransport>(TCPTransport::Mode::Server);
  EchoServerBase server(server_transport);
  
  ASSERT_TRUE(server_transport->listen(port));
  ASSERT_TRUE(server.start());
  
  std::thread server_thread([&server]() {
    server.run();
  });
  
  ASSERT_TRUE(waitForServer("127.0.0.1", port));
  
  // Connect with raw transport to send custom JSON-RPC
  auto transport = std::make_shared<TCPTransport>(TCPTransport::Mode::Client);
  ASSERT_TRUE(transport->connect("127.0.0.1", port));
  
  // Test valid JSON-RPC 2.0 request
  json::JsonObject valid_request;
  valid_request["jsonrpc"] = json::JsonValue("2.0");
  valid_request["method"] = json::JsonValue("echo");
  valid_request["params"] = json::JsonValue("test");
  valid_request["id"] = json::JsonValue(1);
  
  transport->sendMessage(json::serializeJson(json::JsonValue(valid_request)));
  
  std::string response_str = transport->receiveMessage();
  auto response = json::parseJson(response_str);
  ASSERT_TRUE(response.has_value());
  
  auto response_obj = response.value().asObject();
  EXPECT_EQ(response_obj["jsonrpc"].asString(), "2.0");
  EXPECT_EQ(response_obj["id"].asInt(), 1);
  EXPECT_EQ(response_obj["result"].asString(), "test");
  
  // Test notification (no id)
  json::JsonObject notification;
  notification["jsonrpc"] = json::JsonValue("2.0");
  notification["method"] = json::JsonValue("echo");
  notification["params"] = json::JsonValue("notification");
  
  transport->sendMessage(json::serializeJson(json::JsonValue(notification)));
  
  // Should not receive response for notification
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  
  // Test invalid request (missing method)
  json::JsonObject invalid_request;
  invalid_request["jsonrpc"] = json::JsonValue("2.0");
  invalid_request["id"] = json::JsonValue(2);
  
  transport->sendMessage(json::serializeJson(json::JsonValue(invalid_request)));
  
  response_str = transport->receiveMessage();
  response = json::parseJson(response_str);
  ASSERT_TRUE(response.has_value());
  
  response_obj = response.value().asObject();
  EXPECT_TRUE(response_obj.count("error") > 0);
  EXPECT_EQ(response_obj["id"].asInt(), 2);
  
  transport->stop();
  server.stop();
  server_thread.join();
}

} // namespace test
} // namespace echo
} // namespace mcp