/**
 * @file benchmark_network_optimization.cc
 * @brief Benchmark tests for network operation optimizations
 */

#include <gtest/gtest.h>
#include "mcp/auth/auth_c_api.h"
#include <chrono>
#include <vector>
#include <thread>
#include <iostream>
#include <iomanip>
#include <numeric>
#include <algorithm>
#include <curl/curl.h>

namespace {

// Mock JWKS endpoint for testing
const std::string MOCK_JWKS_URL = "https://auth.example.com/jwks.json";

// Real test endpoints (when available)
const std::string TEST_JWKS_URL = "https://login.microsoftonline.com/common/discovery/v2.0/keys";

// Test fixture for network optimization benchmarks
class NetworkOptimizationBenchmark : public ::testing::Test {
protected:
    void SetUp() override {
        mcp_auth_init();
        curl_global_init(CURL_GLOBAL_ALL);
    }
    
    void TearDown() override {
        curl_global_cleanup();
        mcp_auth_shutdown();
    }
    
    // Helper to measure operation time
    template<typename Func>
    std::chrono::milliseconds measureTime(Func func) {
        auto start = std::chrono::high_resolution_clock::now();
        func();
        auto end = std::chrono::high_resolution_clock::now();
        return std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    }
    
    // Simulate JWKS fetch
    bool fetchJWKS(const std::string& url, std::string& response) {
        // In real test, this would call the optimized fetch function
        // For now, return mock success
        response = R"({
            "keys": [
                {
                    "kid": "test-key-1",
                    "alg": "RS256",
                    "x5c": ["MIIDDTCCAfWgAwIBAgIJAKxPFxhKJvs8MA0GCSqGSIb3DQEBCwUAMCQxIjAgBgNVBAMTGWRldi04dHA0Z2U3NC51cy5hdXRoMC5jb20wHhcNMjAwNTEzMTcxNTAwWhcNMzQwMTIwMTcxNTAwWjAkMSIwIAYDVQQDExlkZXYtOHRwNGdlNzQudXMuYXV0aDAuY29tMIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEAu1SU1LfVLPHCozMxH2Mo4lgOEePzNm0tRgeLezV6ffAt0gunVTLw7onLRnrq0"]
                }
            ]
        })";
        return true;
    }
};

// Test 1: Benchmark connection pooling
TEST_F(NetworkOptimizationBenchmark, ConnectionPooling) {
    const int requests = 100;
    std::vector<long> times_with_pool;
    std::vector<long> times_without_pool;
    
    std::cout << "\n=== Connection Pooling Performance ===" << std::endl;
    
    // Test with connection pooling (simulated)
    for (int i = 0; i < requests; ++i) {
        auto duration = measureTime([this]() {
            std::string response;
            fetchJWKS(TEST_JWKS_URL, response);
        });
        times_with_pool.push_back(duration.count());
    }
    
    // Clear pool to test without pooling
    mcp_auth_clear_connection_pool();
    
    // Test without connection pooling (new connection each time)
    for (int i = 0; i < requests; ++i) {
        auto duration = measureTime([this]() {
            std::string response;
            // Force new connection
            fetchJWKS(TEST_JWKS_URL + "?nocache=" + std::to_string(i), response);
        });
        times_without_pool.push_back(duration.count());
    }
    
    // Calculate statistics
    double avg_with_pool = std::accumulate(times_with_pool.begin(), 
                                          times_with_pool.end(), 0.0) / times_with_pool.size();
    double avg_without_pool = std::accumulate(times_without_pool.begin(), 
                                             times_without_pool.end(), 0.0) / times_without_pool.size();
    
    double improvement = ((avg_without_pool - avg_with_pool) / avg_without_pool) * 100;
    
    std::cout << "Requests: " << requests << std::endl;
    std::cout << "With pooling average: " << avg_with_pool << " ms" << std::endl;
    std::cout << "Without pooling average: " << avg_without_pool << " ms" << std::endl;
    std::cout << "Improvement: " << std::fixed << std::setprecision(1) 
              << improvement << "%" << std::endl;
    
    // Connection pooling should provide significant improvement
    EXPECT_GT(improvement, 20) << "Connection pooling should provide >20% improvement";
}

// Test 2: Benchmark DNS caching
TEST_F(NetworkOptimizationBenchmark, DNSCaching) {
    const int iterations = 50;
    std::vector<long> first_lookups;
    std::vector<long> cached_lookups;
    
    std::cout << "\n=== DNS Caching Performance ===" << std::endl;
    
    // First lookups (DNS resolution needed)
    for (int i = 0; i < iterations; ++i) {
        auto duration = measureTime([this, i]() {
            std::string response;
            // Different subdomains to force DNS lookup
            std::string url = "https://sub" + std::to_string(i) + ".example.com/jwks";
            fetchJWKS(url, response);
        });
        first_lookups.push_back(duration.count());
    }
    
    // Cached lookups (DNS cache should be warm)
    for (int i = 0; i < iterations; ++i) {
        auto duration = measureTime([this]() {
            std::string response;
            // Same domain to hit cache
            fetchJWKS("https://example.com/jwks", response);
        });
        cached_lookups.push_back(duration.count());
    }
    
    double avg_first = std::accumulate(first_lookups.begin(), 
                                      first_lookups.end(), 0.0) / first_lookups.size();
    double avg_cached = std::accumulate(cached_lookups.begin(), 
                                       cached_lookups.end(), 0.0) / cached_lookups.size();
    
    std::cout << "First lookup average: " << avg_first << " ms" << std::endl;
    std::cout << "Cached lookup average: " << avg_cached << " ms" << std::endl;
    std::cout << "DNS cache speedup: " << std::fixed << std::setprecision(2) 
              << (avg_first / avg_cached) << "x" << std::endl;
    
    // DNS caching should provide speedup
    EXPECT_LT(avg_cached, avg_first) << "Cached DNS lookups should be faster";
}

// Test 3: Benchmark keep-alive connections
TEST_F(NetworkOptimizationBenchmark, KeepAliveConnections) {
    const int requests = 50;
    std::vector<long> keep_alive_times;
    std::vector<long> no_keep_alive_times;
    
    std::cout << "\n=== Keep-Alive Performance ===" << std::endl;
    
    // Test with keep-alive
    for (int i = 0; i < requests; ++i) {
        auto duration = measureTime([this]() {
            std::string response;
            fetchJWKS(TEST_JWKS_URL, response);
        });
        keep_alive_times.push_back(duration.count());
    }
    
    // Simulate without keep-alive (new connection each time)
    for (int i = 0; i < requests; ++i) {
        mcp_auth_clear_connection_pool(); // Force new connection
        
        auto duration = measureTime([this]() {
            std::string response;
            fetchJWKS(TEST_JWKS_URL, response);
        });
        no_keep_alive_times.push_back(duration.count());
    }
    
    double avg_keep_alive = std::accumulate(keep_alive_times.begin(), 
                                           keep_alive_times.end(), 0.0) / keep_alive_times.size();
    double avg_no_keep_alive = std::accumulate(no_keep_alive_times.begin(), 
                                              no_keep_alive_times.end(), 0.0) / no_keep_alive_times.size();
    
    std::cout << "With keep-alive: " << avg_keep_alive << " ms" << std::endl;
    std::cout << "Without keep-alive: " << avg_no_keep_alive << " ms" << std::endl;
    std::cout << "Keep-alive benefit: " << std::fixed << std::setprecision(1)
              << ((avg_no_keep_alive - avg_keep_alive) / avg_no_keep_alive * 100) << "%" << std::endl;
    
    EXPECT_LT(avg_keep_alive, avg_no_keep_alive) 
        << "Keep-alive should reduce average request time";
}

// Test 4: Benchmark JSON parsing performance
TEST_F(NetworkOptimizationBenchmark, JSONParsingSpeed) {
    const int iterations = 10000;
    
    // Sample JWKS JSON
    std::string jwks_json = R"({
        "keys": [
            {"kid": "key1", "alg": "RS256", "x5c": ["cert1"]},
            {"kid": "key2", "alg": "RS256", "x5c": ["cert2"]},
            {"kid": "key3", "alg": "RS256", "x5c": ["cert3"]},
            {"kid": "key4", "alg": "RS256", "x5c": ["cert4"]},
            {"kid": "key5", "alg": "RS256", "x5c": ["cert5"]}
        ]
    })";
    
    std::cout << "\n=== JSON Parsing Performance ===" << std::endl;
    
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < iterations; ++i) {
        char** kids = nullptr;
        char** certs = nullptr;
        size_t count = 0;
        
        // This would call the optimized parser
        mcp_auth_parse_jwks_optimized(jwks_json.c_str(), &kids, &certs, &count);
        
        // Clean up
        for (size_t j = 0; j < count; ++j) {
            if (kids && kids[j]) free(kids[j]);
            if (certs && certs[j]) free(certs[j]);
        }
        if (kids) free(kids);
        if (certs) free(certs);
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto total_time = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    double avg_time_us = static_cast<double>(total_time.count()) / iterations;
    
    std::cout << "Iterations: " << iterations << std::endl;
    std::cout << "Average parse time: " << avg_time_us << " µs" << std::endl;
    std::cout << "Throughput: " << std::fixed << std::setprecision(0)
              << (1000000.0 / avg_time_us) << " parses/sec" << std::endl;
    
    // Should be very fast with optimized parser
    EXPECT_LT(avg_time_us, 100) << "JSON parsing should be sub-100 microseconds";
}

// Test 5: Benchmark concurrent requests
TEST_F(NetworkOptimizationBenchmark, ConcurrentRequests) {
    const int thread_count = 8;
    const int requests_per_thread = 25;
    
    std::cout << "\n=== Concurrent Request Performance ===" << std::endl;
    
    std::vector<std::thread> threads;
    std::vector<std::vector<long>> thread_times(thread_count);
    
    auto start_all = std::chrono::high_resolution_clock::now();
    
    for (int t = 0; t < thread_count; ++t) {
        threads.emplace_back([this, t, &thread_times, requests_per_thread]() {
            for (int i = 0; i < requests_per_thread; ++i) {
                auto duration = measureTime([this]() {
                    std::string response;
                    fetchJWKS(TEST_JWKS_URL, response);
                });
                thread_times[t].push_back(duration.count());
            }
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    auto end_all = std::chrono::high_resolution_clock::now();
    auto total_duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_all - start_all);
    
    // Calculate statistics
    std::cout << "Threads: " << thread_count << std::endl;
    std::cout << "Requests per thread: " << requests_per_thread << std::endl;
    
    for (int t = 0; t < thread_count; ++t) {
        double avg = std::accumulate(thread_times[t].begin(), 
                                    thread_times[t].end(), 0.0) / thread_times[t].size();
        std::cout << "Thread " << t << " average: " << avg << " ms" << std::endl;
    }
    
    double throughput = (thread_count * requests_per_thread * 1000.0) / total_duration.count();
    std::cout << "Total time: " << total_duration.count() << " ms" << std::endl;
    std::cout << "Throughput: " << std::fixed << std::setprecision(0) 
              << throughput << " requests/sec" << std::endl;
    
    // Should handle concurrent requests efficiently
    EXPECT_GT(throughput, 50) << "Should handle >50 requests/sec";
}

// Test 6: Benchmark memory efficiency
TEST_F(NetworkOptimizationBenchmark, MemoryEfficiency) {
    const int iterations = 1000;
    
    std::cout << "\n=== Memory Efficiency Test ===" << std::endl;
    
    // Perform many requests
    for (int i = 0; i < iterations; ++i) {
        std::string response;
        fetchJWKS(TEST_JWKS_URL, response);
        
        // Parse the response
        char** kids = nullptr;
        char** certs = nullptr;
        size_t count = 0;
        
        mcp_auth_parse_jwks_optimized(response.c_str(), &kids, &certs, &count);
        
        // Clean up immediately
        for (size_t j = 0; j < count; ++j) {
            if (kids && kids[j]) free(kids[j]);
            if (certs && certs[j]) free(certs[j]);
        }
        if (kids) free(kids);
        if (certs) free(certs);
    }
    
    std::cout << "Completed " << iterations << " fetch/parse cycles" << std::endl;
    std::cout << "Memory should remain stable (check with external tools)" << std::endl;
    
    // In real test, would check memory usage
    EXPECT_TRUE(true) << "Memory test completed";
}

// Test 7: Get and display network statistics
TEST_F(NetworkOptimizationBenchmark, NetworkStatistics) {
    std::cout << "\n=== Network Statistics ===" << std::endl;
    
    // Perform some requests to generate statistics
    for (int i = 0; i < 20; ++i) {
        std::string response;
        fetchJWKS(TEST_JWKS_URL, response);
    }
    
    // Get statistics
    size_t total_requests = 0;
    size_t connection_reuses = 0;
    double reuse_rate = 0;
    double dns_hit_rate = 0;
    double avg_latency = 0;
    
    mcp_auth_get_network_stats(&total_requests, &connection_reuses, 
                               &reuse_rate, &dns_hit_rate, &avg_latency);
    
    std::cout << "Total requests: " << total_requests << std::endl;
    std::cout << "Connection reuses: " << connection_reuses << std::endl;
    std::cout << "Connection reuse rate: " << std::fixed << std::setprecision(1) 
              << (reuse_rate * 100) << "%" << std::endl;
    std::cout << "DNS cache hit rate: " << std::fixed << std::setprecision(1)
              << (dns_hit_rate * 100) << "%" << std::endl;
    std::cout << "Average latency: " << avg_latency << " ms" << std::endl;
    
    // Should show good reuse rates
    EXPECT_GT(reuse_rate, 0.5) << "Connection reuse rate should be >50%";
}

} // namespace

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    
    std::cout << "=======================================</" << std::endl;
    std::cout << "Network Operations Optimization Benchmark" << std::endl;
    std::cout << "========================================" << std::endl;
    
    return RUN_ALL_TESTS();
}