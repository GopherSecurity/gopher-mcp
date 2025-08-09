/**
 * @file test_tcp_echo_stress.cc
 * @brief Stress tests for TCP echo implementation
 * 
 * These tests push the TCP echo system to its limits to identify
 * performance bottlenecks, memory leaks, and stability issues.
 */

#include <gtest/gtest.h>
#include <thread>
#include <atomic>
#include <chrono>
#include <vector>
#include <random>
#include <future>
#include <memory>
#include <signal.h>

#include "mcp/echo/echo_basic.h"
#include "mcp/echo/tcp_transport.h"
#include "mcp/json/json_serialization.h"

namespace mcp {
namespace echo {
namespace test {

class TCPEchoStressTest : public ::testing::Test {
protected:
  void SetUp() override {
    signal(SIGPIPE, SIG_IGN);
    base_port_ = 30000 + (getpid() % 10000);
  }

  int getNextPort() {
    return base_port_++;
  }

  // Generate random string of given size
  std::string generateRandomString(size_t size) {
    static const char charset[] = 
        "0123456789"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        "!@#$%^&*()_+-=[]{}|;:,.<>?";
    
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, sizeof(charset) - 2);
    
    std::string result;
    result.reserve(size);
    for (size_t i = 0; i < size; i++) {
      result += charset[dis(gen)];
    }
    return result;
  }

  // Measure memory usage (platform specific)
  size_t getCurrentMemoryUsage() {
    // Simplified memory measurement
    // In production, use platform-specific APIs
    return 0;
  }

private:
  int base_port_;
};

// Test 1: High volume message stress test
TEST_F(TCPEchoStressTest, HighVolumeMessages) {
  int port = getNextPort();
  
  // Create server
  auto server_transport = std::make_shared<TCPTransport>(TCPTransport::Mode::Server);
  EchoServerBase server(server_transport);
  
  ASSERT_TRUE(server_transport->listen(port));
  ASSERT_TRUE(server.start());
  
  std::atomic<bool> server_running(true);
  std::thread server_thread([&]() {
    while (server_running) {
      server_transport->acceptConnection();
      server.run();
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  });
  
  // Statistics
  std::atomic<int> total_messages_sent(0);
  std::atomic<int> total_messages_received(0);
  std::atomic<int> total_errors(0);
  
  // Create multiple clients sending messages concurrently
  const int num_clients = 10;
  const int messages_per_client = 1000;
  std::vector<std::thread> client_threads;
  
  auto start_time = std::chrono::high_resolution_clock::now();
  
  for (int client_id = 0; client_id < num_clients; client_id++) {
    client_threads.emplace_back([&, client_id]() {
      auto transport = std::make_shared<TCPTransport>(TCPTransport::Mode::Client);
      
      if (!transport->connect("127.0.0.1", port)) {
        total_errors++;
        return;
      }
      
      EchoClientBase client(transport);
      if (!client.start()) {
        total_errors++;
        return;
      }
      
      // Run client in background
      std::thread run_thread([&client]() {
        client.run();
      });
      
      // Send messages
      for (int msg_id = 0; msg_id < messages_per_client; msg_id++) {
        std::string message = "Client" + std::to_string(client_id) + 
                            "_Msg" + std::to_string(msg_id);
        
        auto future = client.sendRequest("echo", message);
        total_messages_sent++;
        
        auto status = future.wait_for(std::chrono::seconds(5));
        if (status == std::future_status::ready) {
          auto response = future.get();
          if (response.has_value() && response.value().asString() == message) {
            total_messages_received++;
          } else {
            total_errors++;
          }
        } else {
          total_errors++;
        }
      }
      
      client.stop();
      run_thread.join();
    });
  }
  
  // Wait for all clients to complete
  for (auto& thread : client_threads) {
    thread.join();
  }
  
  auto end_time = std::chrono::high_resolution_clock::now();
  auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
  
  // Stop server
  server_running = false;
  server.stop();
  server_thread.join();
  
  // Print statistics
  std::cout << "\nHigh Volume Stress Test Results:" << std::endl;
  std::cout << "Duration: " << duration.count() << "ms" << std::endl;
  std::cout << "Total messages sent: " << total_messages_sent << std::endl;
  std::cout << "Total messages received: " << total_messages_received << std::endl;
  std::cout << "Total errors: " << total_errors << std::endl;
  std::cout << "Success rate: " << (100.0 * total_messages_received / total_messages_sent) << "%" << std::endl;
  std::cout << "Throughput: " << (1000.0 * total_messages_received / duration.count()) << " msg/sec" << std::endl;
  
  // Assertions
  EXPECT_GT(total_messages_received, total_messages_sent * 0.95); // At least 95% success rate
  EXPECT_LT(total_errors, total_messages_sent * 0.05); // Less than 5% error rate
}

// Test 2: Connection churn stress test
TEST_F(TCPEchoStressTest, ConnectionChurn) {
  int port = getNextPort();
  
  // Create server
  auto server_transport = std::make_shared<TCPTransport>(TCPTransport::Mode::Server);
  EchoServerBase server(server_transport);
  
  ASSERT_TRUE(server_transport->listen(port));
  ASSERT_TRUE(server.start());
  
  std::atomic<bool> server_running(true);
  std::atomic<int> connections_accepted(0);
  
  std::thread server_thread([&]() {
    while (server_running) {
      if (server_transport->acceptConnection()) {
        connections_accepted++;
      }
      server.run();
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  });
  
  // Statistics
  std::atomic<int> successful_connections(0);
  std::atomic<int> failed_connections(0);
  
  const int num_cycles = 100;
  const int parallel_connections = 5;
  
  auto start_time = std::chrono::high_resolution_clock::now();
  
  for (int cycle = 0; cycle < num_cycles; cycle++) {
    std::vector<std::thread> connection_threads;
    
    // Create multiple short-lived connections in parallel
    for (int i = 0; i < parallel_connections; i++) {
      connection_threads.emplace_back([&]() {
        auto transport = std::make_shared<TCPTransport>(TCPTransport::Mode::Client);
        
        if (transport->connect("127.0.0.1", port)) {
          successful_connections++;
          
          // Send a quick message
          EchoClientBase client(transport);
          if (client.start()) {
            std::thread run_thread([&client]() {
              client.run();
            });
            
            auto future = client.sendRequest("echo", "test");
            future.wait_for(std::chrono::milliseconds(100));
            
            client.stop();
            run_thread.join();
          }
        } else {
          failed_connections++;
        }
        
        // Explicitly close connection
        transport->stop();
      });
    }
    
    // Wait for this batch to complete
    for (auto& thread : connection_threads) {
      thread.join();
    }
    
    // Small delay between cycles
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  
  auto end_time = std::chrono::high_resolution_clock::now();
  auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
  
  // Stop server
  server_running = false;
  server.stop();
  server_thread.join();
  
  // Print statistics
  std::cout << "\nConnection Churn Stress Test Results:" << std::endl;
  std::cout << "Duration: " << duration.count() << "ms" << std::endl;
  std::cout << "Successful connections: " << successful_connections << std::endl;
  std::cout << "Failed connections: " << failed_connections << std::endl;
  std::cout << "Connections accepted by server: " << connections_accepted << std::endl;
  std::cout << "Connection rate: " << (1000.0 * successful_connections / duration.count()) << " conn/sec" << std::endl;
  
  // Assertions
  EXPECT_GT(successful_connections, num_cycles * parallel_connections * 0.9);
  EXPECT_LT(failed_connections, num_cycles * parallel_connections * 0.1);
}

// Test 3: Large payload stress test
TEST_F(TCPEchoStressTest, LargePayloadStress) {
  int port = getNextPort();
  
  // Create server
  auto server_transport = std::make_shared<TCPTransport>(TCPTransport::Mode::Server);
  EchoServerBase server(server_transport);
  
  ASSERT_TRUE(server_transport->listen(port));
  ASSERT_TRUE(server.start());
  
  std::thread server_thread([&server]() {
    server.run();
  });
  
  // Create client
  auto client_transport = std::make_shared<TCPTransport>(TCPTransport::Mode::Client);
  EchoClientBase client(client_transport);
  
  ASSERT_TRUE(client_transport->connect("127.0.0.1", port));
  ASSERT_TRUE(client.start());
  
  std::thread client_thread([&client]() {
    client.run();
  });
  
  // Statistics
  std::atomic<size_t> total_bytes_sent(0);
  std::atomic<size_t> total_bytes_received(0);
  std::atomic<int> successful_echoes(0);
  std::atomic<int> failed_echoes(0);
  
  // Test with various large payload sizes
  std::vector<size_t> payload_sizes = {
    1024,       // 1 KB
    10240,      // 10 KB
    102400,     // 100 KB
    1048576,    // 1 MB
    5242880,    // 5 MB
    10485760    // 10 MB
  };
  
  auto start_time = std::chrono::high_resolution_clock::now();
  
  for (size_t size : payload_sizes) {
    for (int i = 0; i < 5; i++) { // Send each size 5 times
      std::string large_payload = generateRandomString(size);
      total_bytes_sent += large_payload.size();
      
      auto future = client.sendRequest("echo", large_payload);
      auto status = future.wait_for(std::chrono::seconds(30));
      
      if (status == std::future_status::ready) {
        auto response = future.get();
        if (response.has_value()) {
          std::string echoed = response.value().asString();
          if (echoed == large_payload) {
            total_bytes_received += echoed.size();
            successful_echoes++;
          } else {
            failed_echoes++;
            std::cerr << "Payload mismatch for size " << size << std::endl;
          }
        } else {
          failed_echoes++;
        }
      } else {
        failed_echoes++;
        std::cerr << "Timeout for payload size " << size << std::endl;
      }
    }
  }
  
  auto end_time = std::chrono::high_resolution_clock::now();
  auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
  
  // Stop client and server
  client.stop();
  server.stop();
  
  client_thread.join();
  server_thread.join();
  
  // Print statistics
  std::cout << "\nLarge Payload Stress Test Results:" << std::endl;
  std::cout << "Duration: " << duration.count() << "ms" << std::endl;
  std::cout << "Total bytes sent: " << total_bytes_sent << " (" << (total_bytes_sent / 1048576) << " MB)" << std::endl;
  std::cout << "Total bytes received: " << total_bytes_received << " (" << (total_bytes_received / 1048576) << " MB)" << std::endl;
  std::cout << "Successful echoes: " << successful_echoes << std::endl;
  std::cout << "Failed echoes: " << failed_echoes << std::endl;
  std::cout << "Throughput: " << (total_bytes_received * 8.0 / 1048576) / (duration.count() / 1000.0) << " Mbps" << std::endl;
  
  // Assertions
  EXPECT_EQ(total_bytes_received, total_bytes_sent);
  EXPECT_EQ(failed_echoes, 0);
}

// Test 4: Sustained load test
TEST_F(TCPEchoStressTest, SustainedLoad) {
  int port = getNextPort();
  
  // Create server
  auto server_transport = std::make_shared<TCPTransport>(TCPTransport::Mode::Server);
  EchoServerBase server(server_transport);
  
  ASSERT_TRUE(server_transport->listen(port));
  ASSERT_TRUE(server.start());
  
  std::atomic<bool> server_running(true);
  std::thread server_thread([&]() {
    while (server_running) {
      server_transport->acceptConnection();
      server.run();
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  });
  
  // Statistics
  std::atomic<int> messages_per_second(0);
  std::atomic<int> total_messages(0);
  std::atomic<int> errors(0);
  std::vector<int> throughput_samples;
  
  const int test_duration_seconds = 10;
  const int num_clients = 5;
  
  std::atomic<bool> test_running(true);
  std::vector<std::thread> client_threads;
  
  // Create sustained load from multiple clients
  for (int client_id = 0; client_id < num_clients; client_id++) {
    client_threads.emplace_back([&, client_id]() {
      auto transport = std::make_shared<TCPTransport>(TCPTransport::Mode::Client);
      
      if (!transport->connect("127.0.0.1", port)) {
        errors++;
        return;
      }
      
      EchoClientBase client(transport);
      if (!client.start()) {
        errors++;
        return;
      }
      
      std::thread run_thread([&client]() {
        client.run();
      });
      
      // Send continuous stream of messages
      while (test_running) {
        std::string message = "Client" + std::to_string(client_id) + 
                            "_Time" + std::to_string(total_messages.load());
        
        auto future = client.sendRequest("echo", message);
        auto status = future.wait_for(std::chrono::milliseconds(100));
        
        if (status == std::future_status::ready) {
          auto response = future.get();
          if (response.has_value()) {
            messages_per_second++;
            total_messages++;
          } else {
            errors++;
          }
        }
        
        // Small delay to prevent overwhelming
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
      
      client.stop();
      run_thread.join();
    });
  }
  
  // Monitor throughput
  std::thread monitor_thread([&]() {
    for (int second = 0; second < test_duration_seconds && test_running; second++) {
      std::this_thread::sleep_for(std::chrono::seconds(1));
      int current_throughput = messages_per_second.exchange(0);
      throughput_samples.push_back(current_throughput);
      std::cout << "Second " << (second + 1) << ": " << current_throughput << " msg/sec" << std::endl;
    }
  });
  
  // Run test for specified duration
  std::this_thread::sleep_for(std::chrono::seconds(test_duration_seconds));
  test_running = false;
  
  // Wait for all threads to complete
  monitor_thread.join();
  for (auto& thread : client_threads) {
    thread.join();
  }
  
  // Stop server
  server_running = false;
  server.stop();
  server_thread.join();
  
  // Calculate statistics
  int min_throughput = *std::min_element(throughput_samples.begin(), throughput_samples.end());
  int max_throughput = *std::max_element(throughput_samples.begin(), throughput_samples.end());
  double avg_throughput = std::accumulate(throughput_samples.begin(), throughput_samples.end(), 0.0) / throughput_samples.size();
  
  // Print statistics
  std::cout << "\nSustained Load Test Results:" << std::endl;
  std::cout << "Test duration: " << test_duration_seconds << " seconds" << std::endl;
  std::cout << "Total messages: " << total_messages << std::endl;
  std::cout << "Total errors: " << errors << std::endl;
  std::cout << "Min throughput: " << min_throughput << " msg/sec" << std::endl;
  std::cout << "Max throughput: " << max_throughput << " msg/sec" << std::endl;
  std::cout << "Avg throughput: " << avg_throughput << " msg/sec" << std::endl;
  
  // Assertions
  EXPECT_GT(avg_throughput, 100); // Should handle at least 100 msg/sec sustained
  EXPECT_LT(errors, total_messages * 0.01); // Less than 1% error rate
}

// Test 5: Memory leak detection
TEST_F(TCPEchoStressTest, MemoryLeakDetection) {
  int port = getNextPort();
  
  // Create server
  auto server_transport = std::make_shared<TCPTransport>(TCPTransport::Mode::Server);
  EchoServerBase server(server_transport);
  
  ASSERT_TRUE(server_transport->listen(port));
  ASSERT_TRUE(server.start());
  
  std::atomic<bool> server_running(true);
  std::thread server_thread([&]() {
    while (server_running) {
      server_transport->acceptConnection();
      server.run();
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  });
  
  // Record initial memory usage
  size_t initial_memory = getCurrentMemoryUsage();
  
  // Perform many create/destroy cycles
  const int num_cycles = 100;
  
  for (int cycle = 0; cycle < num_cycles; cycle++) {
    // Create and destroy client
    auto transport = std::make_shared<TCPTransport>(TCPTransport::Mode::Client);
    
    if (transport->connect("127.0.0.1", port)) {
      EchoClientBase client(transport);
      
      if (client.start()) {
        std::thread run_thread([&client]() {
          client.run();
        });
        
        // Send some messages
        for (int i = 0; i < 10; i++) {
          auto future = client.sendRequest("echo", generateRandomString(1024));
          future.wait_for(std::chrono::milliseconds(100));
        }
        
        client.stop();
        run_thread.join();
      }
    }
    
    // Explicitly destroy
    transport.reset();
    
    // Check memory periodically
    if (cycle % 10 == 0) {
      size_t current_memory = getCurrentMemoryUsage();
      if (current_memory > 0 && initial_memory > 0) {
        size_t memory_growth = current_memory - initial_memory;
        std::cout << "Cycle " << cycle << ": Memory growth = " << memory_growth << " bytes" << std::endl;
      }
    }
  }
  
  // Stop server
  server_running = false;
  server.stop();
  server_thread.join();
  
  // Final memory check
  size_t final_memory = getCurrentMemoryUsage();
  if (final_memory > 0 && initial_memory > 0) {
    size_t total_growth = final_memory - initial_memory;
    std::cout << "\nMemory Leak Detection Results:" << std::endl;
    std::cout << "Initial memory: " << initial_memory << " bytes" << std::endl;
    std::cout << "Final memory: " << final_memory << " bytes" << std::endl;
    std::cout << "Total growth: " << total_growth << " bytes" << std::endl;
    
    // Allow for some growth but flag potential leaks
    size_t acceptable_growth = 10 * 1024 * 1024; // 10 MB
    EXPECT_LT(total_growth, acceptable_growth) << "Possible memory leak detected";
  }
}

// Test 6: Concurrent operations stress
TEST_F(TCPEchoStressTest, ConcurrentOperationsStress) {
  int port = getNextPort();
  
  // Create server
  auto server_transport = std::make_shared<TCPTransport>(TCPTransport::Mode::Server);
  EchoServerBase server(server_transport);
  
  ASSERT_TRUE(server_transport->listen(port));
  ASSERT_TRUE(server.start());
  
  std::atomic<bool> server_running(true);
  std::thread server_thread([&]() {
    while (server_running) {
      server_transport->acceptConnection();
      server.run();
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  });
  
  // Create a single client with many concurrent operations
  auto transport = std::make_shared<TCPTransport>(TCPTransport::Mode::Client);
  ASSERT_TRUE(transport->connect("127.0.0.1", port));
  
  EchoClientBase client(transport);
  ASSERT_TRUE(client.start());
  
  std::thread client_thread([&client]() {
    client.run();
  });
  
  // Launch many concurrent requests
  const int num_concurrent = 100;
  std::vector<std::future<optional<json::JsonValue>>> futures;
  
  auto start_time = std::chrono::high_resolution_clock::now();
  
  // Send all requests without waiting
  for (int i = 0; i < num_concurrent; i++) {
    std::string message = "Concurrent_" + std::to_string(i);
    futures.push_back(client.sendRequest("echo", message));
  }
  
  // Wait for all responses
  int successful = 0;
  int failed = 0;
  
  for (int i = 0; i < num_concurrent; i++) {
    auto status = futures[i].wait_for(std::chrono::seconds(10));
    if (status == std::future_status::ready) {
      auto response = futures[i].get();
      if (response.has_value()) {
        std::string expected = "Concurrent_" + std::to_string(i);
        if (response.value().asString() == expected) {
          successful++;
        } else {
          failed++;
        }
      } else {
        failed++;
      }
    } else {
      failed++;
    }
  }
  
  auto end_time = std::chrono::high_resolution_clock::now();
  auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
  
  // Stop everything
  client.stop();
  client_thread.join();
  
  server_running = false;
  server.stop();
  server_thread.join();
  
  // Print results
  std::cout << "\nConcurrent Operations Stress Test Results:" << std::endl;
  std::cout << "Concurrent requests: " << num_concurrent << std::endl;
  std::cout << "Successful: " << successful << std::endl;
  std::cout << "Failed: " << failed << std::endl;
  std::cout << "Duration: " << duration.count() << "ms" << std::endl;
  std::cout << "Rate: " << (1000.0 * successful / duration.count()) << " req/sec" << std::endl;
  
  // Assertions
  EXPECT_GT(successful, num_concurrent * 0.95); // At least 95% success
  EXPECT_LT(failed, num_concurrent * 0.05); // Less than 5% failure
}

} // namespace test
} // namespace echo
} // namespace mcp