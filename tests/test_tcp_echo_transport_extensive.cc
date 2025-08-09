/**
 * @file test_tcp_echo_transport_extensive.cc
 * @brief Extensive unit tests for TCP transport implementation
 * 
 * This file provides comprehensive testing of the TCP transport layer,
 * including edge cases, error conditions, performance scenarios, and
 * stress testing.
 */

#include <gtest/gtest.h>
#include <thread>
#include <atomic>
#include <chrono>
#include <vector>
#include <random>
#include <future>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <signal.h>

#include "mcp/echo/tcp_transport.h"
#include "mcp/json/json_serialization.h"

namespace mcp {
namespace echo {
namespace test {

// Test fixture with helper methods
class TCPTransportExtensiveTest : public ::testing::Test {
protected:
  void SetUp() override {
    // Ignore SIGPIPE to prevent crashes on broken connections
    signal(SIGPIPE, SIG_IGN);
  }

  // Helper to get a random available port
  int getRandomPort() {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(10000, 60000);
    return dis(gen);
  }

  // Helper to check if port is in use
  bool isPortInUse(int port) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return false;
    
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;
    
    int reuse = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    
    bool in_use = (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0);
    close(sock);
    return in_use;
  }

  // Get an available port
  int getAvailablePort() {
    for (int i = 0; i < 100; i++) {
      int port = getRandomPort();
      if (!isPortInUse(port)) {
        return port;
      }
    }
    return 0;
  }

  // Helper to create large test data
  std::string generateLargeData(size_t size) {
    std::string data;
    data.reserve(size);
    for (size_t i = 0; i < size; i++) {
      data.push_back('A' + (i % 26));
    }
    return data;
  }

  // Helper to create random JSON data
  json::JsonValue generateRandomJson(int depth = 3) {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> type_dis(0, 3);
    static std::uniform_int_distribution<> int_dis(0, 1000);
    static std::uniform_real_distribution<> float_dis(0.0, 1000.0);
    
    if (depth <= 0) {
      // Base case - return simple value
      switch (type_dis(gen)) {
        case 0: return json::JsonValue(int_dis(gen));
        case 1: return json::JsonValue(float_dis(gen));
        case 2: return json::JsonValue(true);
        default: return json::JsonValue("test_string_" + std::to_string(int_dis(gen)));
      }
    }
    
    // Create object with random fields
    json::JsonObject obj;
    int num_fields = 3 + (int_dis(gen) % 5);
    for (int i = 0; i < num_fields; i++) {
      std::string key = "field_" + std::to_string(i);
      obj[key] = generateRandomJson(depth - 1);
    }
    return json::JsonValue(std::move(obj));
  }
};

// Test 1: Multiple simultaneous connections
TEST_F(TCPTransportExtensiveTest, MultipleSimultaneousConnections) {
  int port = getAvailablePort();
  ASSERT_NE(port, 0) << "No available port found";

  // Create server
  auto server = std::make_unique<TCPTransport>(TCPTransport::Mode::Server);
  ASSERT_TRUE(server->listen(port));

  // Accept connections in background
  std::atomic<int> connections_accepted(0);
  std::thread server_thread([&]() {
    while (connections_accepted < 5) {
      if (server->acceptConnection()) {
        connections_accepted++;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
  });

  // Create multiple clients
  std::vector<std::unique_ptr<TCPTransport>> clients;
  for (int i = 0; i < 5; i++) {
    auto client = std::make_unique<TCPTransport>(TCPTransport::Mode::Client);
    ASSERT_TRUE(client->connect("127.0.0.1", port));
    clients.push_back(std::move(client));
  }

  // Wait for all connections to be accepted
  auto start = std::chrono::steady_clock::now();
  while (connections_accepted < 5) {
    if (std::chrono::steady_clock::now() - start > std::chrono::seconds(5)) {
      FAIL() << "Timeout waiting for connections";
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  server_thread.join();
  EXPECT_EQ(connections_accepted, 5);
}

// Test 2: Large message handling
TEST_F(TCPTransportExtensiveTest, LargeMessageHandling) {
  int port = getAvailablePort();
  ASSERT_NE(port, 0);

  auto server = std::make_unique<TCPTransport>(TCPTransport::Mode::Server);
  auto client = std::make_unique<TCPTransport>(TCPTransport::Mode::Client);

  ASSERT_TRUE(server->listen(port));
  
  std::thread server_thread([&]() {
    server->acceptConnection();
  });

  ASSERT_TRUE(client->connect("127.0.0.1", port));
  server_thread.join();

  // Test various large message sizes
  std::vector<size_t> sizes = {1024, 4096, 16384, 65536, 262144, 1048576};
  
  for (size_t size : sizes) {
    std::string large_data = generateLargeData(size);
    
    // Send from client
    client->sendMessage(large_data);
    
    // Receive on server
    std::string received = server->receiveMessage();
    EXPECT_EQ(received, large_data) << "Failed for size: " << size;
    
    // Echo back
    server->sendMessage(received);
    
    // Verify on client
    std::string echoed = client->receiveMessage();
    EXPECT_EQ(echoed, large_data) << "Failed echo for size: " << size;
  }
}

// Test 3: Rapid connect/disconnect cycles
TEST_F(TCPTransportExtensiveTest, RapidConnectDisconnectCycles) {
  int port = getAvailablePort();
  ASSERT_NE(port, 0);

  auto server = std::make_unique<TCPTransport>(TCPTransport::Mode::Server);
  ASSERT_TRUE(server->listen(port));

  // Run server accept loop
  std::atomic<bool> server_running(true);
  std::thread server_thread([&]() {
    while (server_running) {
      server->acceptConnection();
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  });

  // Rapid connect/disconnect cycles
  for (int i = 0; i < 50; i++) {
    auto client = std::make_unique<TCPTransport>(TCPTransport::Mode::Client);
    ASSERT_TRUE(client->connect("127.0.0.1", port)) << "Failed on iteration " << i;
    
    // Send a quick message
    client->sendMessage("test_" + std::to_string(i));
    
    // Explicitly close
    client->stop();
    client.reset();
    
    // Small delay to prevent overwhelming the system
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  server_running = false;
  server_thread.join();
}

// Test 4: Concurrent read/write operations
TEST_F(TCPTransportExtensiveTest, ConcurrentReadWriteOperations) {
  int port = getAvailablePort();
  ASSERT_NE(port, 0);

  auto server = std::make_unique<TCPTransport>(TCPTransport::Mode::Server);
  auto client = std::make_unique<TCPTransport>(TCPTransport::Mode::Client);

  ASSERT_TRUE(server->listen(port));
  
  std::thread server_accept([&]() {
    server->acceptConnection();
  });

  ASSERT_TRUE(client->connect("127.0.0.1", port));
  server_accept.join();

  std::atomic<int> messages_sent(0);
  std::atomic<int> messages_received(0);
  std::atomic<bool> running(true);

  // Client sender thread
  std::thread client_sender([&]() {
    while (running) {
      std::string msg = "client_msg_" + std::to_string(messages_sent++);
      client->sendMessage(msg);
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  });

  // Server sender thread
  std::thread server_sender([&]() {
    while (running) {
      std::string msg = "server_msg_" + std::to_string(messages_sent++);
      server->sendMessage(msg);
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  });

  // Client receiver thread
  std::thread client_receiver([&]() {
    while (running) {
      std::string msg = client->receiveMessage();
      if (!msg.empty()) {
        messages_received++;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  });

  // Server receiver thread
  std::thread server_receiver([&]() {
    while (running) {
      std::string msg = server->receiveMessage();
      if (!msg.empty()) {
        messages_received++;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  });

  // Run for 2 seconds
  std::this_thread::sleep_for(std::chrono::seconds(2));
  running = false;

  client_sender.join();
  server_sender.join();
  client_receiver.join();
  server_receiver.join();

  EXPECT_GT(messages_sent, 100) << "Should have sent many messages";
  EXPECT_GT(messages_received, 100) << "Should have received many messages";
}

// Test 5: Message fragmentation and reassembly
TEST_F(TCPTransportExtensiveTest, MessageFragmentationReassembly) {
  int port = getAvailablePort();
  ASSERT_NE(port, 0);

  auto server = std::make_unique<TCPTransport>(TCPTransport::Mode::Server);
  auto client = std::make_unique<TCPTransport>(TCPTransport::Mode::Client);

  ASSERT_TRUE(server->listen(port));
  
  std::thread server_thread([&]() {
    server->acceptConnection();
  });

  ASSERT_TRUE(client->connect("127.0.0.1", port));
  server_thread.join();

  // Send multiple messages rapidly without waiting for responses
  std::vector<std::string> messages;
  for (int i = 0; i < 100; i++) {
    std::string msg = "Message_" + std::to_string(i) + "_with_some_content";
    messages.push_back(msg);
    client->sendMessage(msg);
  }

  // Receive all messages on server
  std::vector<std::string> received;
  auto start = std::chrono::steady_clock::now();
  while (received.size() < messages.size()) {
    std::string msg = server->receiveMessage();
    if (!msg.empty()) {
      received.push_back(msg);
    }
    
    if (std::chrono::steady_clock::now() - start > std::chrono::seconds(5)) {
      break;
    }
  }

  EXPECT_EQ(received.size(), messages.size());
  for (size_t i = 0; i < std::min(messages.size(), received.size()); i++) {
    EXPECT_EQ(received[i], messages[i]) << "Mismatch at index " << i;
  }
}

// Test 6: Error recovery - connection loss
TEST_F(TCPTransportExtensiveTest, ConnectionLossRecovery) {
  int port = getAvailablePort();
  ASSERT_NE(port, 0);

  auto server = std::make_unique<TCPTransport>(TCPTransport::Mode::Server);
  ASSERT_TRUE(server->listen(port));

  // First connection
  {
    auto client = std::make_unique<TCPTransport>(TCPTransport::Mode::Client);
    
    std::thread server_thread([&]() {
      server->acceptConnection();
    });

    ASSERT_TRUE(client->connect("127.0.0.1", port));
    server_thread.join();

    // Exchange messages
    client->sendMessage("test1");
    EXPECT_EQ(server->receiveMessage(), "test1");

    // Client disconnects abruptly
    client.reset();
  }

  // Server should be able to accept new connection
  {
    auto client2 = std::make_unique<TCPTransport>(TCPTransport::Mode::Client);
    
    std::thread server_thread([&]() {
      server->acceptConnection();
    });

    ASSERT_TRUE(client2->connect("127.0.0.1", port));
    server_thread.join();

    // Should work with new client
    client2->sendMessage("test2");
    EXPECT_EQ(server->receiveMessage(), "test2");
  }
}

// Test 7: Complex JSON message exchange
TEST_F(TCPTransportExtensiveTest, ComplexJsonMessageExchange) {
  int port = getAvailablePort();
  ASSERT_NE(port, 0);

  auto server = std::make_unique<TCPTransport>(TCPTransport::Mode::Server);
  auto client = std::make_unique<TCPTransport>(TCPTransport::Mode::Client);

  ASSERT_TRUE(server->listen(port));
  
  std::thread server_thread([&]() {
    server->acceptConnection();
  });

  ASSERT_TRUE(client->connect("127.0.0.1", port));
  server_thread.join();

  // Generate and exchange complex JSON messages
  for (int i = 0; i < 10; i++) {
    auto json_obj = generateRandomJson(4);
    std::string json_str = json::serializeJson(json_obj);
    
    // Send from client
    client->sendMessage(json_str);
    
    // Receive and parse on server
    std::string received_str = server->receiveMessage();
    auto received_json = json::parseJson(received_str);
    ASSERT_TRUE(received_json.has_value());
    
    // Verify serialization is consistent
    std::string reserialized = json::serializeJson(received_json.value());
    EXPECT_EQ(json_str, reserialized);
    
    // Echo back
    server->sendMessage(reserialized);
    
    // Verify on client
    std::string echoed = client->receiveMessage();
    EXPECT_EQ(echoed, json_str);
  }
}

// Test 8: Maximum connections stress test
TEST_F(TCPTransportExtensiveTest, MaximumConnectionsStress) {
  int port = getAvailablePort();
  ASSERT_NE(port, 0);

  auto server = std::make_unique<TCPTransport>(TCPTransport::Mode::Server);
  ASSERT_TRUE(server->listen(port));

  const int max_clients = 20;
  std::vector<std::unique_ptr<TCPTransport>> clients;
  std::atomic<int> connected_count(0);

  // Server accept loop
  std::thread server_thread([&]() {
    for (int i = 0; i < max_clients; i++) {
      if (server->acceptConnection()) {
        connected_count++;
      }
    }
  });

  // Create many clients
  for (int i = 0; i < max_clients; i++) {
    auto client = std::make_unique<TCPTransport>(TCPTransport::Mode::Client);
    if (client->connect("127.0.0.1", port)) {
      clients.push_back(std::move(client));
    }
    
    // Small delay to avoid overwhelming
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  server_thread.join();

  EXPECT_EQ(clients.size(), max_clients);
  EXPECT_EQ(connected_count, max_clients);

  // Send message from each client
  for (size_t i = 0; i < clients.size(); i++) {
    clients[i]->sendMessage("client_" + std::to_string(i));
  }

  // Server should receive all messages (though order may vary)
  std::set<std::string> received_messages;
  auto start = std::chrono::steady_clock::now();
  while (received_messages.size() < clients.size()) {
    std::string msg = server->receiveMessage();
    if (!msg.empty()) {
      received_messages.insert(msg);
    }
    
    if (std::chrono::steady_clock::now() - start > std::chrono::seconds(5)) {
      break;
    }
  }

  EXPECT_EQ(received_messages.size(), clients.size());
}

// Test 9: Binary data handling
TEST_F(TCPTransportExtensiveTest, BinaryDataHandling) {
  int port = getAvailablePort();
  ASSERT_NE(port, 0);

  auto server = std::make_unique<TCPTransport>(TCPTransport::Mode::Server);
  auto client = std::make_unique<TCPTransport>(TCPTransport::Mode::Client);

  ASSERT_TRUE(server->listen(port));
  
  std::thread server_thread([&]() {
    server->acceptConnection();
  });

  ASSERT_TRUE(client->connect("127.0.0.1", port));
  server_thread.join();

  // Test with binary data including null bytes
  std::string binary_data;
  for (int i = 0; i < 256; i++) {
    binary_data.push_back(static_cast<char>(i));
  }

  // Send binary data
  client->sendMessage(binary_data);
  
  // Receive and verify
  std::string received = server->receiveMessage();
  EXPECT_EQ(received.size(), binary_data.size());
  EXPECT_EQ(received, binary_data);

  // Echo back
  server->sendMessage(received);
  
  // Verify echo
  std::string echoed = client->receiveMessage();
  EXPECT_EQ(echoed, binary_data);
}

// Test 10: Performance benchmark
TEST_F(TCPTransportExtensiveTest, PerformanceBenchmark) {
  int port = getAvailablePort();
  ASSERT_NE(port, 0);

  auto server = std::make_unique<TCPTransport>(TCPTransport::Mode::Server);
  auto client = std::make_unique<TCPTransport>(TCPTransport::Mode::Client);

  ASSERT_TRUE(server->listen(port));
  
  std::thread server_thread([&]() {
    server->acceptConnection();
  });

  ASSERT_TRUE(client->connect("127.0.0.1", port));
  server_thread.join();

  const int num_messages = 1000;
  const std::string test_message = "Performance test message with some content";

  auto start = std::chrono::high_resolution_clock::now();

  // Send messages
  for (int i = 0; i < num_messages; i++) {
    client->sendMessage(test_message);
  }

  // Receive and echo back
  for (int i = 0; i < num_messages; i++) {
    std::string received = server->receiveMessage();
    server->sendMessage(received);
  }

  // Receive echoes
  for (int i = 0; i < num_messages; i++) {
    client->receiveMessage();
  }

  auto end = std::chrono::high_resolution_clock::now();
  auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

  double messages_per_second = (num_messages * 2.0) / (duration.count() / 1000.0);
  
  std::cout << "Performance: " << messages_per_second << " messages/second" << std::endl;
  std::cout << "Total time: " << duration.count() << "ms for " << (num_messages * 2) << " messages" << std::endl;

  // Expect reasonable performance (at least 1000 messages per second)
  EXPECT_GT(messages_per_second, 1000);
}

// Test 11: Connection timeout handling
TEST_F(TCPTransportExtensiveTest, ConnectionTimeoutHandling) {
  int port = getAvailablePort();
  ASSERT_NE(port, 0);

  // Try to connect to a server that doesn't exist
  auto client = std::make_unique<TCPTransport>(TCPTransport::Mode::Client);
  
  auto start = std::chrono::steady_clock::now();
  bool connected = client->connect("127.0.0.1", port);
  auto duration = std::chrono::steady_clock::now() - start;

  EXPECT_FALSE(connected);
  // Should timeout within 2 seconds (default is 1 second)
  EXPECT_LT(duration, std::chrono::seconds(2));
}

// Test 12: IPv6 support
TEST_F(TCPTransportExtensiveTest, IPv6Support) {
  int port = getAvailablePort();
  ASSERT_NE(port, 0);

  auto server = std::make_unique<TCPTransport>(TCPTransport::Mode::Server);
  
  // Try to listen on IPv6
  bool ipv6_supported = server->listen(port, "::");
  
  if (ipv6_supported) {
    auto client = std::make_unique<TCPTransport>(TCPTransport::Mode::Client);
    
    std::thread server_thread([&]() {
      server->acceptConnection();
    });

    // Connect using IPv6 loopback
    ASSERT_TRUE(client->connect("::1", port));
    server_thread.join();

    // Test message exchange
    client->sendMessage("IPv6 test");
    EXPECT_EQ(server->receiveMessage(), "IPv6 test");
  } else {
    GTEST_SKIP() << "IPv6 not supported on this system";
  }
}

} // namespace test
} // namespace echo
} // namespace mcp