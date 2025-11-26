/**
 * @file benchmark_jwt_validation.cc
 * @brief Performance benchmarks for JWT validation
 *
 * This file contains performance tests for:
 * - JWT token validation throughput
 * - Memory usage during validation
 * - Concurrent validation scenarios
 * - Cache performance impact
 */

#include <chrono>
#include <thread>
#include <vector>
#include <atomic>
#include <iostream>
#include <iomanip>
#include <memory>
#include <random>
#include <sstream>

#include "gtest/gtest.h"
#include "mcp/auth/auth_c_api.h"
#include "mcp/auth/memory_cache.h"
#include "mcp/auth/jwt_validator.h"

namespace mcp::auth::test {

/**
 * @brief Utility class for measuring performance metrics
 */
class PerformanceMetrics {
public:
  void startTimer() {
    start_time_ = std::chrono::high_resolution_clock::now();
  }
  
  void stopTimer() {
    end_time_ = std::chrono::high_resolution_clock::now();
  }
  
  double getElapsedMs() const {
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
      end_time_ - start_time_
    );
    return duration.count() / 1000.0;
  }
  
  double getOpsPerSecond(size_t operations) const {
    double elapsed_seconds = getElapsedMs() / 1000.0;
    return operations / elapsed_seconds;
  }
  
  static size_t getCurrentMemoryUsage() {
    // Platform-specific memory measurement
    // This is a simplified version - real implementation would use
    // platform-specific APIs
    return 0; // Placeholder
  }

private:
  std::chrono::high_resolution_clock::time_point start_time_;
  std::chrono::high_resolution_clock::time_point end_time_;
};

/**
 * @brief Generate a sample JWT token for testing
 */
std::string generateTestToken(int id = 0) {
  // This would normally generate a proper JWT
  // For benchmarking, we use a consistent format
  std::stringstream ss;
  ss << "eyJhbGciOiJSUzI1NiIsInR5cCI6IkpXVCJ9."
     << "eyJzdWIiOiJ1c2VyXyI" << std::setfill('0') << std::setw(6) << id
     << "IiwiaXNzIjoiaHR0cHM6Ly9hdXRoLmV4YW1wbGUuY29tIiwiYXVkIjoiYXBpLmV4YW1wbGUuY29tIiwi"
     << "ZXhwIjoxNzM1MTcxMjAwLCJpYXQiOjE3MzUwODQ4MDAsInNjb3BlcyI6InJlYWQgd3JpdGUifQ."
     << "signature_placeholder_" << id;
  return ss.str();
}

class JwtValidationBenchmark : public ::testing::Test {
protected:
  void SetUp() override {
    // Initialize auth library
    mcp_auth_init();
    
    // Create auth client for benchmarks
    mcp_auth_error_t result = mcp_auth_client_create(
      &client_,
      "https://auth.example.com/.well-known/jwks.json",
      "https://auth.example.com"
    );
    ASSERT_EQ(result, MCP_AUTH_SUCCESS);
    
    // Create validation options
    result = mcp_auth_validation_options_create(&options_);
    ASSERT_EQ(result, MCP_AUTH_SUCCESS);
    
    mcp_auth_validation_options_set_scopes(options_, "read write");
    mcp_auth_validation_options_set_audience(options_, "api.example.com");
  }
  
  void TearDown() override {
    if (options_) {
      mcp_auth_validation_options_destroy(options_);
    }
    if (client_) {
      mcp_auth_client_destroy(client_);
    }
    mcp_auth_shutdown();
  }
  
  mcp_auth_client_t client_ = nullptr;
  mcp_auth_validation_options_t options_ = nullptr;
};

/**
 * @brief Benchmark single-threaded JWT validation performance
 */
TEST_F(JwtValidationBenchmark, SingleThreadedValidation) {
  const size_t num_validations = 10000;
  std::vector<std::string> tokens;
  
  // Generate test tokens
  for (size_t i = 0; i < num_validations; ++i) {
    tokens.push_back(generateTestToken(i));
  }
  
  PerformanceMetrics metrics;
  size_t successful_validations = 0;
  
  // Measure validation performance
  metrics.startTimer();
  
  for (const auto& token : tokens) {
    mcp_auth_validation_result_t result;
    mcp_auth_error_t error = mcp_auth_validate_token(
      client_,
      token.c_str(),
      options_,
      &result
    );
    
    if (error == MCP_AUTH_SUCCESS && result.valid) {
      successful_validations++;
    }
  }
  
  metrics.stopTimer();
  
  // Report results
  std::cout << "\n=== Single-Threaded Validation Performance ===" << std::endl;
  std::cout << "Total validations: " << num_validations << std::endl;
  std::cout << "Successful validations: " << successful_validations << std::endl;
  std::cout << "Time elapsed: " << metrics.getElapsedMs() << " ms" << std::endl;
  std::cout << "Throughput: " << std::fixed << std::setprecision(2) 
            << metrics.getOpsPerSecond(num_validations) << " ops/sec" << std::endl;
  std::cout << "Average latency: " << std::fixed << std::setprecision(3)
            << metrics.getElapsedMs() / num_validations << " ms/op" << std::endl;
  
  // Performance assertions (baseline requirements)
  EXPECT_GT(metrics.getOpsPerSecond(num_validations), 1000.0) 
    << "Validation throughput should exceed 1000 ops/sec";
  EXPECT_LT(metrics.getElapsedMs() / num_validations, 10.0)
    << "Average validation latency should be under 10ms";
}

/**
 * @brief Benchmark concurrent JWT validation performance
 */
TEST_F(JwtValidationBenchmark, ConcurrentValidation) {
  const size_t num_threads = 4;
  const size_t validations_per_thread = 2500;
  const size_t total_validations = num_threads * validations_per_thread;
  
  std::atomic<size_t> successful_validations(0);
  std::atomic<size_t> failed_validations(0);
  
  PerformanceMetrics metrics;
  
  auto validation_worker = [this, &successful_validations, &failed_validations](
    size_t thread_id, 
    size_t num_validations
  ) {
    for (size_t i = 0; i < num_validations; ++i) {
      std::string token = generateTestToken(thread_id * 1000 + i);
      mcp_auth_validation_result_t result;
      
      mcp_auth_error_t error = mcp_auth_validate_token(
        client_,
        token.c_str(),
        options_,
        &result
      );
      
      if (error == MCP_AUTH_SUCCESS && result.valid) {
        successful_validations++;
      } else {
        failed_validations++;
      }
    }
  };
  
  // Start concurrent validation
  metrics.startTimer();
  
  std::vector<std::thread> threads;
  for (size_t i = 0; i < num_threads; ++i) {
    threads.emplace_back(validation_worker, i, validations_per_thread);
  }
  
  // Wait for all threads to complete
  for (auto& thread : threads) {
    thread.join();
  }
  
  metrics.stopTimer();
  
  // Report results
  std::cout << "\n=== Concurrent Validation Performance ===" << std::endl;
  std::cout << "Number of threads: " << num_threads << std::endl;
  std::cout << "Total validations: " << total_validations << std::endl;
  std::cout << "Successful validations: " << successful_validations << std::endl;
  std::cout << "Failed validations: " << failed_validations << std::endl;
  std::cout << "Time elapsed: " << metrics.getElapsedMs() << " ms" << std::endl;
  std::cout << "Throughput: " << std::fixed << std::setprecision(2)
            << metrics.getOpsPerSecond(total_validations) << " ops/sec" << std::endl;
  std::cout << "Average latency: " << std::fixed << std::setprecision(3)
            << metrics.getElapsedMs() / total_validations << " ms/op" << std::endl;
  
  // Performance assertions
  EXPECT_GT(metrics.getOpsPerSecond(total_validations), num_threads * 800.0)
    << "Concurrent throughput should scale with thread count";
  EXPECT_EQ(successful_validations + failed_validations, total_validations)
    << "All validations should complete";
}

/**
 * @brief Benchmark memory cache performance
 */
TEST_F(JwtValidationBenchmark, CachePerformance) {
  const size_t cache_size = 1000;
  const size_t num_operations = 100000;
  
  // Create memory cache
  MemoryCache<std::string, std::string> cache(cache_size);
  
  // Generate test data
  std::vector<std::string> keys;
  std::vector<std::string> values;
  for (size_t i = 0; i < cache_size * 2; ++i) {
    keys.push_back("key_" + std::to_string(i));
    values.push_back(generateTestToken(i));
  }
  
  PerformanceMetrics metrics;
  
  // Benchmark cache writes
  metrics.startTimer();
  for (size_t i = 0; i < num_operations; ++i) {
    size_t index = i % keys.size();
    cache.put(keys[index], values[index], std::chrono::seconds(300));
  }
  metrics.stopTimer();
  
  std::cout << "\n=== Cache Write Performance ===" << std::endl;
  std::cout << "Cache size: " << cache_size << std::endl;
  std::cout << "Write operations: " << num_operations << std::endl;
  std::cout << "Time elapsed: " << metrics.getElapsedMs() << " ms" << std::endl;
  std::cout << "Write throughput: " << std::fixed << std::setprecision(2)
            << metrics.getOpsPerSecond(num_operations) << " ops/sec" << std::endl;
  
  // Benchmark cache reads (with hits and misses)
  size_t cache_hits = 0;
  metrics.startTimer();
  for (size_t i = 0; i < num_operations; ++i) {
    size_t index = i % keys.size();
    auto value = cache.get(keys[index]);
    if (value.has_value()) {
      cache_hits++;
    }
  }
  metrics.stopTimer();
  
  double hit_rate = (cache_hits * 100.0) / num_operations;
  
  std::cout << "\n=== Cache Read Performance ===" << std::endl;
  std::cout << "Read operations: " << num_operations << std::endl;
  std::cout << "Cache hits: " << cache_hits << std::endl;
  std::cout << "Hit rate: " << std::fixed << std::setprecision(2) << hit_rate << "%" << std::endl;
  std::cout << "Time elapsed: " << metrics.getElapsedMs() << " ms" << std::endl;
  std::cout << "Read throughput: " << std::fixed << std::setprecision(2)
            << metrics.getOpsPerSecond(num_operations) << " ops/sec" << std::endl;
  
  // Performance assertions
  EXPECT_GT(metrics.getOpsPerSecond(num_operations), 100000.0)
    << "Cache operations should exceed 100k ops/sec";
  EXPECT_GT(hit_rate, 30.0)
    << "Cache hit rate should be reasonable given cache size";
}

/**
 * @brief Benchmark memory usage during validation
 */
TEST_F(JwtValidationBenchmark, MemoryUsage) {
  const size_t num_tokens = 1000;
  std::vector<std::string> tokens;
  std::vector<mcp_auth_token_payload_t> payloads;
  
  // Generate tokens
  for (size_t i = 0; i < num_tokens; ++i) {
    tokens.push_back(generateTestToken(i));
  }
  
  // Measure baseline memory
  size_t baseline_memory = PerformanceMetrics::getCurrentMemoryUsage();
  
  // Extract payloads and measure memory growth
  for (const auto& token : tokens) {
    mcp_auth_token_payload_t payload = nullptr;
    mcp_auth_error_t error = mcp_auth_extract_payload(token.c_str(), &payload);
    
    if (error == MCP_AUTH_SUCCESS && payload) {
      payloads.push_back(payload);
    }
  }
  
  size_t peak_memory = PerformanceMetrics::getCurrentMemoryUsage();
  
  // Cleanup payloads
  for (auto payload : payloads) {
    mcp_auth_payload_destroy(payload);
  }
  
  size_t final_memory = PerformanceMetrics::getCurrentMemoryUsage();
  
  std::cout << "\n=== Memory Usage Analysis ===" << std::endl;
  std::cout << "Number of tokens: " << num_tokens << std::endl;
  std::cout << "Payloads extracted: " << payloads.size() << std::endl;
  
  if (baseline_memory > 0) {
    std::cout << "Baseline memory: " << baseline_memory / 1024 << " KB" << std::endl;
    std::cout << "Peak memory: " << peak_memory / 1024 << " KB" << std::endl;
    std::cout << "Final memory: " << final_memory / 1024 << " KB" << std::endl;
    std::cout << "Memory growth: " << (peak_memory - baseline_memory) / 1024 << " KB" << std::endl;
    std::cout << "Average per token: " << (peak_memory - baseline_memory) / num_tokens 
              << " bytes" << std::endl;
    
    // Check for memory leaks
    EXPECT_LE(final_memory, baseline_memory * 1.1)
      << "Memory should return close to baseline after cleanup";
  } else {
    std::cout << "Memory measurement not available on this platform" << std::endl;
  }
}

/**
 * @brief Stress test with rapid token validation
 */
TEST_F(JwtValidationBenchmark, StressTest) {
  const size_t duration_seconds = 5;
  const size_t num_threads = 8;
  
  std::atomic<size_t> total_validations(0);
  std::atomic<bool> stop_flag(false);
  
  auto stress_worker = [this, &total_validations, &stop_flag]() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 99999);
    
    while (!stop_flag) {
      std::string token = generateTestToken(dis(gen));
      mcp_auth_validation_result_t result;
      
      mcp_auth_validate_token(client_, token.c_str(), options_, &result);
      total_validations++;
    }
  };
  
  std::cout << "\n=== Stress Test ===" << std::endl;
  std::cout << "Duration: " << duration_seconds << " seconds" << std::endl;
  std::cout << "Threads: " << num_threads << std::endl;
  
  // Start stress test
  auto start_time = std::chrono::steady_clock::now();
  
  std::vector<std::thread> threads;
  for (size_t i = 0; i < num_threads; ++i) {
    threads.emplace_back(stress_worker);
  }
  
  // Run for specified duration
  std::this_thread::sleep_for(std::chrono::seconds(duration_seconds));
  stop_flag = true;
  
  // Wait for threads to complete
  for (auto& thread : threads) {
    thread.join();
  }
  
  auto end_time = std::chrono::steady_clock::now();
  auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
    end_time - start_time
  );
  
  double ops_per_second = (total_validations * 1000.0) / duration.count();
  
  std::cout << "Total validations: " << total_validations << std::endl;
  std::cout << "Throughput: " << std::fixed << std::setprecision(2)
            << ops_per_second << " ops/sec" << std::endl;
  std::cout << "Average per thread: " << std::fixed << std::setprecision(2)
            << ops_per_second / num_threads << " ops/sec" << std::endl;
  
  // Stress test should complete without crashes
  EXPECT_GT(total_validations, duration_seconds * 1000)
    << "Should complete at least 1000 validations per second under stress";
}

} // namespace mcp::auth::test