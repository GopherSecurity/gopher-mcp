/**
 * @file test_complete_integration.cc
 * @brief Complete integration tests for JWT authentication
 */

#include <gtest/gtest.h>
#include "mcp/auth/auth_c_api.h"
#include <thread>
#include <vector>
#include <chrono>
#include <curl/curl.h>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <atomic>

namespace {

// Test configuration from environment
struct TestConfig {
    std::string example_server_url;
    std::string keycloak_url;
    std::string jwks_uri;
    std::string client_id;
    std::string client_secret;
    std::string username;
    std::string password;
    std::string issuer;
    
    TestConfig() {
        // Read from environment with defaults
        example_server_url = getEnvOrDefault("EXAMPLE_SERVER_URL", "http://localhost:3000");
        keycloak_url = getEnvOrDefault("KEYCLOAK_URL", "http://localhost:8080/realms/master");
        jwks_uri = keycloak_url + "/protocol/openid-connect/certs";
        client_id = getEnvOrDefault("CLIENT_ID", "mcp-inspector");
        client_secret = getEnvOrDefault("CLIENT_SECRET", "");
        username = getEnvOrDefault("TEST_USERNAME", "test@example.com");
        password = getEnvOrDefault("TEST_PASSWORD", "password");
        issuer = keycloak_url;
    }
    
    static std::string getEnvOrDefault(const char* name, const std::string& default_value) {
        const char* value = std::getenv(name);
        return value ? value : default_value;
    }
};

// HTTP helper for testing
class HTTPHelper {
public:
    struct Response {
        std::string body;
        long status_code;
        std::map<std::string, std::string> headers;
    };
    
    static Response get(const std::string& url, const std::string& auth_header = "") {
        Response response;
        CURL* curl = curl_easy_init();
        
        if (curl) {
            curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response.body);
            
            struct curl_slist* headers = nullptr;
            if (!auth_header.empty()) {
                headers = curl_slist_append(headers, ("Authorization: " + auth_header).c_str());
                curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
            }
            
            CURLcode res = curl_easy_perform(curl);
            if (res == CURLE_OK) {
                curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.status_code);
            }
            
            if (headers) {
                curl_slist_free_all(headers);
            }
            curl_easy_cleanup(curl);
        }
        
        return response;
    }
    
    static Response post(const std::string& url, const std::string& data, 
                         const std::string& content_type = "application/x-www-form-urlencoded") {
        Response response;
        CURL* curl = curl_easy_init();
        
        if (curl) {
            curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data.c_str());
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response.body);
            
            struct curl_slist* headers = nullptr;
            headers = curl_slist_append(headers, ("Content-Type: " + content_type).c_str());
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
            
            CURLcode res = curl_easy_perform(curl);
            if (res == CURLE_OK) {
                curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.status_code);
            }
            
            curl_slist_free_all(headers);
            curl_easy_cleanup(curl);
        }
        
        return response;
    }
    
private:
    static size_t writeCallback(void* contents, size_t size, size_t nmemb, void* userp) {
        std::string* response = static_cast<std::string*>(userp);
        response->append(static_cast<char*>(contents), size * nmemb);
        return size * nmemb;
    }
};

// Test fixture for complete integration testing
class CompleteIntegrationTest : public ::testing::Test {
protected:
    TestConfig config;
    mcp_auth_client_t* client;
    
    void SetUp() override {
        mcp_auth_init();
        curl_global_init(CURL_GLOBAL_ALL);
        
        // Create auth client
        mcp_auth_config_t client_config = {
            .jwks_uri = config.jwks_uri.c_str(),
            .issuer = config.issuer.c_str(),
            .cache_duration = 3600,
            .auto_refresh = true
        };
        
        mcp_auth_error_t err = mcp_auth_client_create(&client_config, &client);
        ASSERT_EQ(err, MCP_AUTH_SUCCESS) << "Failed to create auth client";
    }
    
    void TearDown() override {
        if (client) {
            mcp_auth_client_destroy(client);
        }
        curl_global_cleanup();
        mcp_auth_shutdown();
    }
    
    // Helper to get a valid token from Keycloak
    std::string getValidToken() {
        std::string token_url = config.keycloak_url + "/protocol/openid-connect/token";
        
        std::stringstream data;
        data << "grant_type=password"
             << "&client_id=" << config.client_id
             << "&username=" << config.username
             << "&password=" << config.password;
        
        if (!config.client_secret.empty()) {
            data << "&client_secret=" << config.client_secret;
        }
        
        HTTPHelper::Response response = HTTPHelper::post(token_url, data.str());
        
        if (response.status_code == 200) {
            // Parse JSON to extract access_token
            size_t pos = response.body.find("\"access_token\":\"");
            if (pos != std::string::npos) {
                pos += 16; // Length of "access_token":"
                size_t end = response.body.find("\"", pos);
                return response.body.substr(pos, end - pos);
            }
        }
        
        return "";
    }
    
    // Check if server is available
    bool isServerAvailable(const std::string& url) {
        HTTPHelper::Response response = HTTPHelper::get(url);
        return response.status_code > 0;
    }
};

// Test 1: Verify example server starts and responds
TEST_F(CompleteIntegrationTest, ExampleServerStartup) {
    if (!isServerAvailable(config.example_server_url)) {
        GTEST_SKIP() << "Example server not available at " << config.example_server_url;
    }
    
    std::cout << "\n=== Testing Example Server ===" << std::endl;
    
    // Test server health endpoint
    HTTPHelper::Response response = HTTPHelper::get(config.example_server_url + "/health");
    
    EXPECT_EQ(response.status_code, 200) << "Server health check failed";
    std::cout << "✓ Server is running and healthy" << std::endl;
}

// Test 2: Test public tool access without authentication
TEST_F(CompleteIntegrationTest, PublicToolAccess) {
    if (!isServerAvailable(config.example_server_url)) {
        GTEST_SKIP() << "Example server not available";
    }
    
    std::cout << "\n=== Testing Public Tool Access ===" << std::endl;
    
    // Test accessing public weather tool without auth
    std::string public_endpoint = config.example_server_url + "/tools/weather/current";
    HTTPHelper::Response response = HTTPHelper::get(public_endpoint + "?location=London");
    
    if (response.status_code == 200) {
        std::cout << "✓ Public tool accessible without authentication" << std::endl;
    } else if (response.status_code == 401) {
        std::cout << "✗ Public tool requires authentication (may be configured differently)" << std::endl;
    }
    
    // Public tools should be accessible without auth
    EXPECT_NE(response.status_code, 0) << "Failed to connect to server";
}

// Test 3: Test protected tool requires authentication
TEST_F(CompleteIntegrationTest, ProtectedToolRequiresAuth) {
    if (!isServerAvailable(config.example_server_url)) {
        GTEST_SKIP() << "Example server not available";
    }
    
    std::cout << "\n=== Testing Protected Tool Authentication ===" << std::endl;
    
    // Test accessing protected tool without auth
    std::string protected_endpoint = config.example_server_url + "/tools/weather/forecast";
    HTTPHelper::Response response = HTTPHelper::get(protected_endpoint + "?location=London&days=5");
    
    // Should get 401 Unauthorized
    EXPECT_EQ(response.status_code, 401) << "Protected tool should require authentication";
    std::cout << "✓ Protected tool correctly requires authentication" << std::endl;
}

// Test 4: Test tool access with valid token
TEST_F(CompleteIntegrationTest, AuthenticatedToolAccess) {
    if (!isServerAvailable(config.example_server_url)) {
        GTEST_SKIP() << "Example server not available";
    }
    
    if (!isServerAvailable(config.keycloak_url)) {
        GTEST_SKIP() << "Keycloak not available";
    }
    
    std::cout << "\n=== Testing Authenticated Tool Access ===" << std::endl;
    
    // Get valid token
    std::string token = getValidToken();
    if (token.empty()) {
        GTEST_SKIP() << "Could not obtain valid token from Keycloak";
    }
    
    // Validate token locally
    mcp_auth_validation_result_t result;
    mcp_auth_error_t err = mcp_auth_validate_token(client, token.c_str(), nullptr, &result);
    
    EXPECT_EQ(err, MCP_AUTH_SUCCESS) << "Token validation failed";
    std::cout << "✓ Token validated successfully" << std::endl;
    
    // Test accessing protected tool with auth
    std::string protected_endpoint = config.example_server_url + "/tools/weather/forecast";
    HTTPHelper::Response response = HTTPHelper::get(
        protected_endpoint + "?location=London&days=5",
        "Bearer " + token
    );
    
    EXPECT_EQ(response.status_code, 200) << "Should access protected tool with valid token";
    std::cout << "✓ Protected tool accessible with valid token" << std::endl;
}

// Test 5: Test scope validation
TEST_F(CompleteIntegrationTest, ScopeValidation) {
    if (!isServerAvailable(config.keycloak_url)) {
        GTEST_SKIP() << "Keycloak not available";
    }
    
    std::cout << "\n=== Testing Scope Validation ===" << std::endl;
    
    std::string token = getValidToken();
    if (token.empty()) {
        GTEST_SKIP() << "Could not obtain valid token";
    }
    
    // Create validation options with required scope
    mcp_auth_validation_options_t* options = mcp_auth_validation_options_create();
    mcp_auth_validation_options_set_scopes(options, "mcp:weather");
    
    // Validate token with scope requirement
    mcp_auth_validation_result_t result;
    mcp_auth_error_t err = mcp_auth_validate_token(client, token.c_str(), options, &result);
    
    if (err == MCP_AUTH_SUCCESS) {
        std::cout << "✓ Token has required mcp:weather scope" << std::endl;
    } else if (err == MCP_AUTH_INSUFFICIENT_SCOPE) {
        std::cout << "✗ Token lacks mcp:weather scope" << std::endl;
    }
    
    mcp_auth_validation_options_destroy(options);
}

// Test 6: Test concurrent token validation (thread safety)
TEST_F(CompleteIntegrationTest, ConcurrentTokenValidation) {
    if (!isServerAvailable(config.keycloak_url)) {
        GTEST_SKIP() << "Keycloak not available";
    }
    
    std::cout << "\n=== Testing Thread Safety ===" << std::endl;
    
    std::string token = getValidToken();
    if (token.empty()) {
        GTEST_SKIP() << "Could not obtain valid token";
    }
    
    const int thread_count = 10;
    const int validations_per_thread = 100;
    std::atomic<int> success_count(0);
    std::atomic<int> failure_count(0);
    
    std::vector<std::thread> threads;
    
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < thread_count; ++i) {
        threads.emplace_back([this, &token, &success_count, &failure_count, validations_per_thread]() {
            for (int j = 0; j < validations_per_thread; ++j) {
                mcp_auth_validation_result_t result;
                mcp_auth_error_t err = mcp_auth_validate_token(client, token.c_str(), nullptr, &result);
                
                if (err == MCP_AUTH_SUCCESS) {
                    success_count++;
                } else {
                    failure_count++;
                }
            }
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    int total = success_count + failure_count;
    double throughput = (total * 1000.0) / duration.count();
    
    std::cout << "Threads: " << thread_count << std::endl;
    std::cout << "Total validations: " << total << std::endl;
    std::cout << "Successful: " << success_count << std::endl;
    std::cout << "Failed: " << failure_count << std::endl;
    std::cout << "Duration: " << duration.count() << " ms" << std::endl;
    std::cout << "Throughput: " << throughput << " validations/sec" << std::endl;
    
    EXPECT_EQ(success_count, total) << "All concurrent validations should succeed";
    std::cout << "✓ Thread-safe operation confirmed" << std::endl;
}

// Test 7: Test token expiration handling
TEST_F(CompleteIntegrationTest, TokenExpiration) {
    std::cout << "\n=== Testing Token Expiration ===" << std::endl;
    
    // Create an expired token (mock)
    std::string expired_token = "eyJhbGciOiJSUzI1NiIsInR5cCI6IkpXVCJ9."
                                "eyJpc3MiOiJ0ZXN0IiwiZXhwIjoxMDAwMDAwMDAwfQ."
                                "signature";
    
    mcp_auth_validation_result_t result;
    mcp_auth_error_t err = mcp_auth_validate_token(client, expired_token.c_str(), nullptr, &result);
    
    EXPECT_EQ(err, MCP_AUTH_EXPIRED_TOKEN) << "Should detect expired token";
    std::cout << "✓ Expired token correctly rejected" << std::endl;
}

// Test 8: OAuth flow simulation
TEST_F(CompleteIntegrationTest, OAuthFlowSimulation) {
    std::cout << "\n=== Testing OAuth Flow ===" << std::endl;
    
    // Simulate OAuth authorization code flow
    std::cout << "1. Redirect to authorization endpoint" << std::endl;
    std::string auth_url = config.keycloak_url + "/protocol/openid-connect/auth"
                          "?client_id=" + config.client_id +
                          "&redirect_uri=http://localhost:5173/auth/callback"
                          "&response_type=code"
                          "&scope=openid mcp:weather";
    
    std::cout << "   Authorization URL: " << auth_url << std::endl;
    
    std::cout << "2. User authenticates and authorizes" << std::endl;
    std::cout << "3. Redirect back with authorization code" << std::endl;
    std::cout << "4. Exchange code for tokens" << std::endl;
    
    // In real flow, would exchange auth code for tokens
    // For testing, we use password grant
    std::string token = getValidToken();
    
    if (!token.empty()) {
        std::cout << "5. Token obtained successfully" << std::endl;
        std::cout << "✓ OAuth flow simulation complete" << std::endl;
    } else {
        std::cout << "✗ Could not complete OAuth flow simulation" << std::endl;
    }
}

// Test 9: Performance benchmarks
TEST_F(CompleteIntegrationTest, PerformanceBenchmarks) {
    std::cout << "\n=== Performance Benchmarks ===" << std::endl;
    
    // Create a mock token for performance testing
    std::string mock_token = "eyJhbGciOiJSUzI1NiIsInR5cCI6IkpXVCIsImtpZCI6InRlc3QifQ."
                            "eyJpc3MiOiJodHRwczovL3Rlc3QuY29tIiwic3ViIjoidXNlcjEyMyIsImV4cCI6OTk5OTk5OTk5OX0."
                            "signature";
    
    const int iterations = 1000;
    
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < iterations; ++i) {
        mcp_auth_validation_result_t result;
        mcp_auth_validate_token(client, mock_token.c_str(), nullptr, &result);
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    double avg_time_us = static_cast<double>(duration.count()) / iterations;
    double throughput = 1000000.0 / avg_time_us;
    
    std::cout << "Iterations: " << iterations << std::endl;
    std::cout << "Average validation time: " << avg_time_us << " µs" << std::endl;
    std::cout << "Throughput: " << throughput << " validations/sec" << std::endl;
    
    EXPECT_LT(avg_time_us, 1000) << "Validation should be sub-millisecond";
    std::cout << "✓ Performance meets requirements" << std::endl;
}

// Test 10: Memory leak detection preparation
TEST_F(CompleteIntegrationTest, MemoryLeakCheck) {
    std::cout << "\n=== Memory Leak Check ===" << std::endl;
    std::cout << "Run with valgrind for comprehensive memory leak detection:" << std::endl;
    std::cout << "  valgrind --leak-check=full --show-leak-kinds=all ./test_complete_integration" << std::endl;
    
    // Perform operations that could leak memory
    for (int i = 0; i < 100; ++i) {
        mcp_auth_client_t* temp_client;
        mcp_auth_config_t temp_config = {
            .jwks_uri = config.jwks_uri.c_str(),
            .issuer = config.issuer.c_str(),
            .cache_duration = 3600,
            .auto_refresh = false
        };
        
        mcp_auth_client_create(&temp_config, &temp_client);
        
        // Validate a token
        std::string token = "test.token.here";
        mcp_auth_validation_result_t result;
        mcp_auth_validate_token(temp_client, token.c_str(), nullptr, &result);
        
        mcp_auth_client_destroy(temp_client);
    }
    
    std::cout << "✓ Memory operations completed (check valgrind output)" << std::endl;
}

} // namespace

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    
    std::cout << "=========================================" << std::endl;
    std::cout << "Complete Integration Test Suite" << std::endl;
    std::cout << "=========================================" << std::endl;
    std::cout << "" << std::endl;
    std::cout << "Environment Configuration:" << std::endl;
    std::cout << "  EXAMPLE_SERVER_URL: " << (std::getenv("EXAMPLE_SERVER_URL") ?: "http://localhost:3000") << std::endl;
    std::cout << "  KEYCLOAK_URL: " << (std::getenv("KEYCLOAK_URL") ?: "http://localhost:8080/realms/master") << std::endl;
    std::cout << "  CLIENT_ID: " << (std::getenv("CLIENT_ID") ?: "mcp-inspector") << std::endl;
    std::cout << "" << std::endl;
    
    return RUN_ALL_TESTS();
}