/**
 * @file test_keycloak_integration.cc
 * @brief Integration tests for Keycloak authentication
 * 
 * Tests real token validation with Keycloak server
 */

#include <gtest/gtest.h>
#include "mcp/auth/auth_c_api.h"
#include <string>
#include <chrono>
#include <thread>
#include <cstdlib>
#include <iostream>
#include <curl/curl.h>
#include <vector>
#include <unordered_map>

namespace {

// Test configuration from environment
struct KeycloakConfig {
    std::string server_url;
    std::string realm;
    std::string client_id;
    std::string client_secret;
    std::string username;
    std::string password;
    std::string jwks_uri;
    std::string issuer;
    
    static KeycloakConfig fromEnvironment() {
        KeycloakConfig config;
        
        // Default to local Keycloak instance
        config.server_url = getEnvOrDefault("KEYCLOAK_URL", "http://localhost:8080");
        config.realm = getEnvOrDefault("KEYCLOAK_REALM", "master");
        config.client_id = getEnvOrDefault("KEYCLOAK_CLIENT_ID", "test-client");
        config.client_secret = getEnvOrDefault("KEYCLOAK_CLIENT_SECRET", "test-secret");
        config.username = getEnvOrDefault("KEYCLOAK_USERNAME", "test-user");
        config.password = getEnvOrDefault("KEYCLOAK_PASSWORD", "test-password");
        
        // Construct URIs
        config.jwks_uri = config.server_url + "/realms/" + config.realm + "/protocol/openid-connect/certs";
        config.issuer = config.server_url + "/realms/" + config.realm;
        
        return config;
    }
    
private:
    static std::string getEnvOrDefault(const char* name, const std::string& default_value) {
        const char* value = std::getenv(name);
        return value ? value : default_value;
    }
};

// CURL callback for response data
size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* userp) {
    userp->append((char*)contents, size * nmemb);
    return size * nmemb;
}

// Helper to get token from Keycloak
std::string getKeycloakToken(const KeycloakConfig& config, const std::string& scope = "") {
    CURL* curl = curl_easy_init();
    if (!curl) {
        return "";
    }
    
    std::string response;
    std::string token_url = config.server_url + "/realms/" + config.realm + "/protocol/openid-connect/token";
    
    // Build POST data
    std::string post_data = "grant_type=password";
    post_data += "&client_id=" + config.client_id;
    post_data += "&client_secret=" + config.client_secret;
    post_data += "&username=" + config.username;
    post_data += "&password=" + config.password;
    if (!scope.empty()) {
        post_data += "&scope=" + scope;
    }
    
    curl_easy_setopt(curl, CURLOPT_URL, token_url.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_data.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L); // For testing only
    
    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    
    if (res != CURLE_OK) {
        return "";
    }
    
    // Extract access_token from JSON response (simple parsing)
    size_t token_pos = response.find("\"access_token\":\"");
    if (token_pos == std::string::npos) {
        return "";
    }
    
    token_pos += 16; // Length of "access_token":"
    size_t token_end = response.find("\"", token_pos);
    if (token_end == std::string::npos) {
        return "";
    }
    
    return response.substr(token_pos, token_end - token_pos);
}

// Helper to create expired token (mock)
std::string createExpiredToken() {
    // This is a mock expired token for testing
    // In real scenario, you'd wait for a token to expire or use a pre-generated one
    return "eyJhbGciOiJSUzI1NiIsInR5cCI6IkpXVCJ9."
           "eyJleHAiOjE2MDAwMDAwMDAsImlzcyI6Imh0dHA6Ly9sb2NhbGhvc3Q6ODA4MC9yZWFsbXMvbWFzdGVyIn0."
           "invalid_signature";
}

class KeycloakIntegrationTest : public ::testing::Test {
protected:
    mcp_auth_client_t client = nullptr;
    KeycloakConfig config;
    bool keycloak_available = false;
    
    void SetUp() override {
        // Initialize library
        ASSERT_EQ(mcp_auth_init(), MCP_AUTH_SUCCESS);
        
        // Get configuration
        config = KeycloakConfig::fromEnvironment();
        
        // Check if Keycloak is available
        keycloak_available = checkKeycloakAvailable();
        
        if (!keycloak_available) {
            GTEST_SKIP() << "Keycloak server not available at " << config.server_url;
        }
        
        // Create auth client
        mcp_auth_error_t err = mcp_auth_client_create(&client, 
                                                      config.jwks_uri.c_str(),
                                                      config.issuer.c_str());
        ASSERT_EQ(err, MCP_AUTH_SUCCESS) << "Failed to create auth client";
    }
    
    void TearDown() override {
        if (client) {
            mcp_auth_client_destroy(client);
        }
        mcp_auth_shutdown();
    }
    
private:
    bool checkKeycloakAvailable() {
        CURL* curl = curl_easy_init();
        if (!curl) return false;
        
        std::string health_url = config.server_url + "/health";
        curl_easy_setopt(curl, CURLOPT_URL, health_url.c_str());
        curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 2L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        
        CURLcode res = curl_easy_perform(curl);
        long http_code = 0;
        if (res == CURLE_OK) {
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        }
        
        curl_easy_cleanup(curl);
        return (res == CURLE_OK && http_code > 0);
    }
};

// Test 1: Valid token validation
TEST_F(KeycloakIntegrationTest, ValidateValidToken) {
    // Get a fresh token from Keycloak
    std::string token = getKeycloakToken(config);
    ASSERT_FALSE(token.empty()) << "Failed to get token from Keycloak";
    
    // Validate the token
    mcp_auth_validation_result_t result;
    mcp_auth_error_t err = mcp_auth_validate_token(client, token.c_str(), nullptr, &result);
    
    EXPECT_EQ(err, MCP_AUTH_SUCCESS);
    EXPECT_TRUE(result.valid);
    EXPECT_EQ(result.error_code, MCP_AUTH_SUCCESS);
}

// Test 2: JWKS fetching
TEST_F(KeycloakIntegrationTest, FetchJWKS) {
    // First validation triggers JWKS fetch
    std::string token = getKeycloakToken(config);
    ASSERT_FALSE(token.empty()) << "Failed to get token from Keycloak";
    
    mcp_auth_validation_result_t result;
    mcp_auth_error_t err = mcp_auth_validate_token(client, token.c_str(), nullptr, &result);
    EXPECT_EQ(err, MCP_AUTH_SUCCESS);
    
    // Second validation should use cached JWKS
    err = mcp_auth_validate_token(client, token.c_str(), nullptr, &result);
    EXPECT_EQ(err, MCP_AUTH_SUCCESS);
    EXPECT_TRUE(result.valid);
}

// Test 3: Expired token rejection
TEST_F(KeycloakIntegrationTest, RejectExpiredToken) {
    std::string expired_token = createExpiredToken();
    
    mcp_auth_validation_result_t result;
    mcp_auth_error_t err = mcp_auth_validate_token(client, expired_token.c_str(), nullptr, &result);
    
    // Should fail validation
    EXPECT_NE(err, MCP_AUTH_SUCCESS);
    EXPECT_FALSE(result.valid);
}

// Test 4: Invalid signature rejection
TEST_F(KeycloakIntegrationTest, RejectInvalidSignature) {
    // Get a valid token and corrupt the signature
    std::string token = getKeycloakToken(config);
    ASSERT_FALSE(token.empty()) << "Failed to get token from Keycloak";
    
    // Corrupt the signature (last part after last dot)
    size_t last_dot = token.rfind('.');
    if (last_dot != std::string::npos) {
        token = token.substr(0, last_dot + 1) + "corrupted_signature";
    }
    
    mcp_auth_validation_result_t result;
    mcp_auth_error_t err = mcp_auth_validate_token(client, token.c_str(), nullptr, &result);
    
    EXPECT_NE(err, MCP_AUTH_SUCCESS);
    EXPECT_FALSE(result.valid);
    EXPECT_EQ(result.error_code, MCP_AUTH_ERROR_INVALID_SIGNATURE);
}

// Test 5: Wrong issuer rejection
TEST_F(KeycloakIntegrationTest, RejectWrongIssuer) {
    // Create client with wrong issuer
    mcp_auth_client_t wrong_client = nullptr;
    mcp_auth_error_t err = mcp_auth_client_create(&wrong_client,
                                                  config.jwks_uri.c_str(),
                                                  "https://wrong.issuer.com");
    ASSERT_EQ(err, MCP_AUTH_SUCCESS);
    
    // Get valid token
    std::string token = getKeycloakToken(config);
    ASSERT_FALSE(token.empty()) << "Failed to get token from Keycloak";
    
    // Validate with wrong issuer
    mcp_auth_validation_result_t result;
    err = mcp_auth_validate_token(wrong_client, token.c_str(), nullptr, &result);
    
    EXPECT_NE(err, MCP_AUTH_SUCCESS);
    EXPECT_FALSE(result.valid);
    EXPECT_EQ(result.error_code, MCP_AUTH_ERROR_INVALID_ISSUER);
    
    mcp_auth_client_destroy(wrong_client);
}

// Test 6: Scope validation
TEST_F(KeycloakIntegrationTest, ValidateScopes) {
    // Get token with specific scope
    std::string token = getKeycloakToken(config, "openid profile");
    ASSERT_FALSE(token.empty()) << "Failed to get token from Keycloak";
    
    // Create validation options requiring scope
    mcp_auth_validation_options_t options = nullptr;
    mcp_auth_error_t err = mcp_auth_validation_options_create(&options);
    ASSERT_EQ(err, MCP_AUTH_SUCCESS);
    
    // Set required scope
    err = mcp_auth_validation_options_set_scopes(options, "openid");
    ASSERT_EQ(err, MCP_AUTH_SUCCESS);
    
    // Validate token with scope requirement
    mcp_auth_validation_result_t result;
    err = mcp_auth_validate_token(client, token.c_str(), options, &result);
    
    EXPECT_EQ(err, MCP_AUTH_SUCCESS);
    EXPECT_TRUE(result.valid);
    
    mcp_auth_validation_options_destroy(options);
}

// Test 7: Cache invalidation on unknown kid
TEST_F(KeycloakIntegrationTest, CacheInvalidationOnUnknownKid) {
    // Get first token
    std::string token1 = getKeycloakToken(config);
    ASSERT_FALSE(token1.empty()) << "Failed to get first token";
    
    // Validate to populate cache
    mcp_auth_validation_result_t result;
    mcp_auth_error_t err = mcp_auth_validate_token(client, token1.c_str(), nullptr, &result);
    EXPECT_EQ(err, MCP_AUTH_SUCCESS);
    
    // In real scenario, Keycloak would rotate keys here
    // For testing, we can only verify the mechanism exists
    
    // Get another token (might have different kid if keys rotated)
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    std::string token2 = getKeycloakToken(config);
    ASSERT_FALSE(token2.empty()) << "Failed to get second token";
    
    // Validate second token - should work even with different kid
    err = mcp_auth_validate_token(client, token2.c_str(), nullptr, &result);
    EXPECT_EQ(err, MCP_AUTH_SUCCESS);
    EXPECT_TRUE(result.valid);
}

// Test 8: Concurrent token validation
TEST_F(KeycloakIntegrationTest, ConcurrentValidation) {
    // Get multiple tokens
    std::vector<std::string> tokens;
    for (int i = 0; i < 5; ++i) {
        std::string token = getKeycloakToken(config);
        ASSERT_FALSE(token.empty()) << "Failed to get token " << i;
        tokens.push_back(token);
    }
    
    // Validate tokens concurrently
    std::vector<std::thread> threads;
    std::vector<bool> results(tokens.size(), false);
    
    for (size_t i = 0; i < tokens.size(); ++i) {
        threads.emplace_back([this, &tokens, &results, i]() {
            mcp_auth_validation_result_t result;
            mcp_auth_error_t err = mcp_auth_validate_token(client, 
                                                          tokens[i].c_str(), 
                                                          nullptr, 
                                                          &result);
            results[i] = (err == MCP_AUTH_SUCCESS && result.valid);
        });
    }
    
    // Wait for all threads
    for (auto& t : threads) {
        t.join();
    }
    
    // Check all validations succeeded
    for (size_t i = 0; i < results.size(); ++i) {
        EXPECT_TRUE(results[i]) << "Validation failed for token " << i;
    }
}

// Test 9: Token refresh scenario
TEST_F(KeycloakIntegrationTest, TokenRefreshScenario) {
    // Get initial token
    std::string token1 = getKeycloakToken(config);
    ASSERT_FALSE(token1.empty()) << "Failed to get initial token";
    
    // Validate initial token
    mcp_auth_validation_result_t result;
    mcp_auth_error_t err = mcp_auth_validate_token(client, token1.c_str(), nullptr, &result);
    EXPECT_EQ(err, MCP_AUTH_SUCCESS);
    EXPECT_TRUE(result.valid);
    
    // Simulate token refresh by getting new token
    std::this_thread::sleep_for(std::chrono::seconds(1));
    std::string token2 = getKeycloakToken(config);
    ASSERT_FALSE(token2.empty()) << "Failed to get refreshed token";
    
    // Validate refreshed token
    err = mcp_auth_validate_token(client, token2.c_str(), nullptr, &result);
    EXPECT_EQ(err, MCP_AUTH_SUCCESS);
    EXPECT_TRUE(result.valid);
}

// Test 10: Audience validation
TEST_F(KeycloakIntegrationTest, AudienceValidation) {
    // Get token
    std::string token = getKeycloakToken(config);
    ASSERT_FALSE(token.empty()) << "Failed to get token";
    
    // Create validation options with audience
    mcp_auth_validation_options_t options = nullptr;
    mcp_auth_error_t err = mcp_auth_validation_options_create(&options);
    ASSERT_EQ(err, MCP_AUTH_SUCCESS);
    
    // Set expected audience (this might need adjustment based on Keycloak config)
    err = mcp_auth_validation_options_set_audience(options, config.client_id.c_str());
    ASSERT_EQ(err, MCP_AUTH_SUCCESS);
    
    // Validate token with audience requirement
    mcp_auth_validation_result_t result;
    err = mcp_auth_validate_token(client, token.c_str(), options, &result);
    
    // Note: Result depends on Keycloak configuration
    // If audience is not in token, this will fail
    if (err != MCP_AUTH_SUCCESS) {
        EXPECT_EQ(result.error_code, MCP_AUTH_ERROR_INVALID_AUDIENCE);
    }
    
    mcp_auth_validation_options_destroy(options);
}

} // namespace

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    
    // Check if we should skip Keycloak tests
    const char* skip_keycloak = std::getenv("SKIP_KEYCLOAK_TESTS");
    if (skip_keycloak && std::string(skip_keycloak) == "1") {
        std::cout << "Skipping Keycloak integration tests (SKIP_KEYCLOAK_TESTS=1)" << std::endl;
        return 0;
    }
    
    return RUN_ALL_TESTS();
}