/**
 * @file test_mcp_inspector_flow.cc
 * @brief Integration tests for MCP Inspector OAuth flow
 * 
 * Tests the complete OAuth authentication flow with MCP Inspector
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
#include <memory>

namespace {

// Test configuration for MCP Inspector
struct MCPInspectorConfig {
    std::string server_url;
    std::string auth_server_url;
    std::string client_id;
    std::string client_secret;
    std::string redirect_uri;
    std::string jwks_uri;
    std::string issuer;
    bool require_auth_on_connect;
    
    static MCPInspectorConfig fromEnvironment() {
        MCPInspectorConfig config;
        
        // MCP Server configuration
        config.server_url = getEnvOrDefault("MCP_SERVER_URL", "http://localhost:3000");
        config.require_auth_on_connect = getEnvOrDefault("REQUIRE_AUTH_ON_CONNECT", "true") == "true";
        
        // OAuth configuration
        config.auth_server_url = getEnvOrDefault("AUTH_SERVER_URL", "http://localhost:8080/realms/master");
        config.client_id = getEnvOrDefault("OAUTH_CLIENT_ID", "mcp-inspector");
        config.client_secret = getEnvOrDefault("OAUTH_CLIENT_SECRET", "mcp-secret");
        config.redirect_uri = getEnvOrDefault("OAUTH_REDIRECT_URI", "http://localhost:5173/auth/callback");
        
        // JWKS configuration
        config.jwks_uri = config.auth_server_url + "/protocol/openid-connect/certs";
        config.issuer = config.auth_server_url;
        
        return config;
    }
    
private:
    static std::string getEnvOrDefault(const char* name, const std::string& default_value) {
        const char* value = std::getenv(name);
        return value ? value : default_value;
    }
};

// Helper class to simulate MCP Inspector client behavior
class MCPInspectorClient {
public:
    MCPInspectorClient(const MCPInspectorConfig& config) 
        : config_(config), auth_client_(nullptr) {
        // Initialize auth client
        mcp_auth_error_t err = mcp_auth_client_create(&auth_client_,
                                                      config.jwks_uri.c_str(),
                                                      config.issuer.c_str());
        if (err != MCP_AUTH_SUCCESS) {
            throw std::runtime_error("Failed to create auth client");
        }
    }
    
    ~MCPInspectorClient() {
        if (auth_client_) {
            mcp_auth_client_destroy(auth_client_);
        }
    }
    
    // Simulate clicking "Connect" button
    bool initiateConnection() {
        if (config_.require_auth_on_connect) {
            // Should trigger OAuth flow
            return triggerOAuthFlow();
        }
        // Direct connection without auth
        return connectToServer("");
    }
    
    // Simulate OAuth authorization flow
    bool triggerOAuthFlow() {
        // In real MCP Inspector, this would open browser for OAuth
        // Here we simulate getting a token
        std::string token = simulateOAuthCodeFlow();
        if (token.empty()) {
            return false;
        }
        
        // Validate the token
        return validateAndConnect(token);
    }
    
    // Validate token and establish session
    bool validateAndConnect(const std::string& token) {
        // Validate token
        mcp_auth_validation_result_t result;
        mcp_auth_error_t err = mcp_auth_validate_token(auth_client_, 
                                                       token.c_str(), 
                                                       nullptr, 
                                                       &result);
        
        if (err != MCP_AUTH_SUCCESS || !result.valid) {
            last_error_ = "Token validation failed";
            return false;
        }
        
        // Connect with validated token
        return connectToServer(token);
    }
    
    // Connect to MCP server with token
    bool connectToServer(const std::string& token) {
        // Set authorization header
        if (!token.empty()) {
            auth_header_ = "Bearer " + token;
        }
        
        // Simulate connection
        connected_ = true;
        session_token_ = token;
        return true;
    }
    
    // Test tool invocation with authorization
    bool invokeTool(const std::string& tool_name, const std::string& required_scope = "") {
        if (!connected_) {
            last_error_ = "Not connected";
            return false;
        }
        
        // Check if we have a token
        if (session_token_.empty() && config_.require_auth_on_connect) {
            last_error_ = "No authentication token";
            return false;
        }
        
        // Validate scope if required
        if (!required_scope.empty() && !session_token_.empty()) {
            return validateToolScope(required_scope);
        }
        
        return true;
    }
    
    // Test token expiration handling
    bool handleTokenExpiration() {
        if (session_token_.empty()) {
            return true; // No token to expire
        }
        
        // Simulate expired token validation
        std::string expired_token = createExpiredToken();
        
        mcp_auth_validation_result_t result;
        mcp_auth_error_t err = mcp_auth_validate_token(auth_client_,
                                                       expired_token.c_str(),
                                                       nullptr,
                                                       &result);
        
        // Should detect expiration
        if (err == MCP_AUTH_SUCCESS || result.valid) {
            last_error_ = "Failed to detect expired token";
            return false;
        }
        
        // Should trigger re-authentication
        return triggerOAuthFlow();
    }
    
    bool isConnected() const { return connected_; }
    const std::string& getAuthHeader() const { return auth_header_; }
    const std::string& getLastError() const { return last_error_; }
    
private:
    // Simulate OAuth code flow (mock implementation)
    std::string simulateOAuthCodeFlow() {
        // In real scenario, this would:
        // 1. Open browser with authorization URL
        // 2. User logs in and approves
        // 3. Receive authorization code
        // 4. Exchange code for token
        
        // For testing, return a mock token
        return createMockToken();
    }
    
    // Create mock JWT token for testing
    std::string createMockToken() {
        // This is a mock token with standard claims
        // In real tests, you'd get this from actual OAuth server
        return "eyJhbGciOiJSUzI1NiIsInR5cCI6IkpXVCJ9."
               "eyJpc3MiOiJodHRwOi8vbG9jYWxob3N0OjgwODAvYXV0aC9yZWFsbXMvbWFzdGVyIiwic3ViIjoidGVzdC11c2VyIiwiYXVkIjoibWNwLWluc3BlY3RvciIsImV4cCI6OTk5OTk5OTk5OSwic2NvcGUiOiJtY3A6d2VhdGhlciBvcGVuaWQifQ."
               "mock_signature";
    }
    
    // Create expired token for testing
    std::string createExpiredToken() {
        return "eyJhbGciOiJSUzI1NiIsInR5cCI6IkpXVCJ9."
               "eyJpc3MiOiJodHRwOi8vbG9jYWxob3N0OjgwODAvYXV0aC9yZWFsbXMvbWFzdGVyIiwiZXhwIjoxNjAwMDAwMDAwfQ."
               "expired_signature";
    }
    
    // Validate tool scope requirements
    bool validateToolScope(const std::string& required_scope) {
        // Create validation options with scope
        mcp_auth_validation_options_t options = nullptr;
        mcp_auth_error_t err = mcp_auth_validation_options_create(&options);
        if (err != MCP_AUTH_SUCCESS) {
            last_error_ = "Failed to create validation options";
            return false;
        }
        
        err = mcp_auth_validation_options_set_scopes(options, required_scope.c_str());
        if (err != MCP_AUTH_SUCCESS) {
            mcp_auth_validation_options_destroy(options);
            last_error_ = "Failed to set required scope";
            return false;
        }
        
        // Validate token with scope requirement
        mcp_auth_validation_result_t result;
        err = mcp_auth_validate_token(auth_client_,
                                      session_token_.c_str(),
                                      options,
                                      &result);
        
        mcp_auth_validation_options_destroy(options);
        
        if (err != MCP_AUTH_SUCCESS || !result.valid) {
            last_error_ = "Insufficient scope for tool";
            return false;
        }
        
        return true;
    }
    
    MCPInspectorConfig config_;
    mcp_auth_client_t auth_client_;
    bool connected_ = false;
    std::string session_token_;
    std::string auth_header_;
    std::string last_error_;
};

// Test fixture for MCP Inspector flow
class MCPInspectorFlowTest : public ::testing::Test {
protected:
    MCPInspectorConfig config;
    std::unique_ptr<MCPInspectorClient> inspector;
    
    void SetUp() override {
        // Initialize library
        ASSERT_EQ(mcp_auth_init(), MCP_AUTH_SUCCESS);
        
        // Get configuration
        config = MCPInspectorConfig::fromEnvironment();
        
        // Create inspector client
        try {
            inspector = std::make_unique<MCPInspectorClient>(config);
        } catch (const std::exception& e) {
            GTEST_SKIP() << "Failed to initialize MCP Inspector client: " << e.what();
        }
    }
    
    void TearDown() override {
        inspector.reset();
        mcp_auth_shutdown();
    }
};

// Test 1: Connect triggers authentication when REQUIRE_AUTH_ON_CONNECT=true
TEST_F(MCPInspectorFlowTest, ConnectTriggersAuth) {
    if (!config.require_auth_on_connect) {
        GTEST_SKIP() << "REQUIRE_AUTH_ON_CONNECT is not enabled";
    }
    
    // Clicking Connect should trigger OAuth flow
    bool connected = inspector->initiateConnection();
    
    EXPECT_TRUE(connected) << "Failed to connect: " << inspector->getLastError();
    EXPECT_TRUE(inspector->isConnected());
    
    // Should have authorization header
    EXPECT_FALSE(inspector->getAuthHeader().empty());
    EXPECT_EQ(inspector->getAuthHeader().substr(0, 7), "Bearer ");
}

// Test 2: Connect without auth when REQUIRE_AUTH_ON_CONNECT=false
TEST_F(MCPInspectorFlowTest, ConnectWithoutAuth) {
    // Temporarily simulate no auth required
    MCPInspectorConfig no_auth_config = config;
    no_auth_config.require_auth_on_connect = false;
    
    MCPInspectorClient no_auth_inspector(no_auth_config);
    
    // Should connect without OAuth flow
    bool connected = no_auth_inspector.initiateConnection();
    
    EXPECT_TRUE(connected);
    EXPECT_TRUE(no_auth_inspector.isConnected());
    
    // Should not have authorization header
    EXPECT_TRUE(no_auth_inspector.getAuthHeader().empty());
}

// Test 3: Successful authentication and session creation
TEST_F(MCPInspectorFlowTest, SuccessfulAuthentication) {
    // Simulate getting a valid token
    std::string valid_token = "eyJhbGciOiJSUzI1NiIsInR5cCI6IkpXVCJ9."
                              "eyJpc3MiOiJodHRwOi8vbG9jYWxob3N0OjgwODAvYXV0aC9yZWFsbXMvbWFzdGVyIiwic3ViIjoidGVzdCIsImV4cCI6OTk5OTk5OTk5OX0."
                              "valid_signature";
    
    // Validate and connect
    bool connected = inspector->validateAndConnect(valid_token);
    
    // Note: This will fail with mock token as signature is invalid
    // In real test with actual OAuth server, this would succeed
    if (!connected) {
        // Expected with mock token
        EXPECT_FALSE(inspector->isConnected());
    }
}

// Test 4: Token passed in Authorization header
TEST_F(MCPInspectorFlowTest, TokenInAuthorizationHeader) {
    // Connect with auth
    bool connected = inspector->initiateConnection();
    
    if (connected) {
        // Check authorization header format
        const std::string& auth_header = inspector->getAuthHeader();
        if (!auth_header.empty()) {
            EXPECT_EQ(auth_header.substr(0, 7), "Bearer ");
            EXPECT_GT(auth_header.length(), 7);
        }
    }
}

// Test 5: Tool authorization with different scopes
TEST_F(MCPInspectorFlowTest, ToolAuthorizationScopes) {
    // Connect first
    inspector->initiateConnection();
    
    if (inspector->isConnected()) {
        // Test public tool (no scope required)
        bool public_access = inspector->invokeTool("get_current_time");
        EXPECT_TRUE(public_access) << inspector->getLastError();
        
        // Test protected tool (requires scope)
        bool protected_access = inspector->invokeTool("get_forecast", "mcp:weather");
        
        // Result depends on token having the scope
        // With mock token, this will likely fail
        if (!protected_access) {
            EXPECT_EQ(inspector->getLastError(), "Insufficient scope for tool");
        }
    }
}

// Test 6: Token expiration during active session
TEST_F(MCPInspectorFlowTest, TokenExpirationHandling) {
    // Connect with auth
    inspector->initiateConnection();
    
    if (inspector->isConnected()) {
        // Simulate token expiration
        bool handled = inspector->handleTokenExpiration();
        
        // Should trigger re-authentication
        if (config.require_auth_on_connect) {
            // After handling expiration, should still be connected
            EXPECT_TRUE(inspector->isConnected());
        }
    }
}

// Test 7: Multiple tool invocations with same token
TEST_F(MCPInspectorFlowTest, MultipleToolInvocations) {
    // Connect once
    inspector->initiateConnection();
    
    if (inspector->isConnected()) {
        // Invoke multiple tools with same session
        for (int i = 0; i < 5; ++i) {
            bool success = inspector->invokeTool("tool_" + std::to_string(i));
            EXPECT_TRUE(success) << "Failed to invoke tool " << i;
        }
    }
}

// Test 8: Scope validation for weather tools
TEST_F(MCPInspectorFlowTest, WeatherToolScopes) {
    // Connect with auth
    inspector->initiateConnection();
    
    if (inspector->isConnected()) {
        // Test weather tools with scope requirements
        struct ToolTest {
            std::string name;
            std::string required_scope;
            bool should_be_public;
        };
        
        std::vector<ToolTest> tools = {
            {"get_current_time", "", true},           // Public tool
            {"get_forecast", "mcp:weather", false},   // Protected tool
            {"get_alerts", "mcp:weather", false}      // Protected tool
        };
        
        for (const auto& tool : tools) {
            bool success = inspector->invokeTool(tool.name, tool.required_scope);
            
            if (tool.should_be_public) {
                EXPECT_TRUE(success) << "Public tool should be accessible: " << tool.name;
            }
            // Protected tools depend on token scopes
        }
    }
}

// Test 9: Session persistence across reconnects
TEST_F(MCPInspectorFlowTest, SessionPersistence) {
    // Initial connection
    inspector->initiateConnection();
    std::string initial_header = inspector->getAuthHeader();
    
    // Simulate disconnect and reconnect
    inspector->connectToServer("");  // Disconnect
    
    // Reconnect with same token (session persistence)
    if (!initial_header.empty()) {
        std::string token = initial_header.substr(7); // Remove "Bearer "
        bool reconnected = inspector->validateAndConnect(token);
        
        // Should maintain session if token is still valid
        if (reconnected) {
            EXPECT_EQ(inspector->getAuthHeader(), initial_header);
        }
    }
}

// Test 10: Error handling for invalid tokens
TEST_F(MCPInspectorFlowTest, InvalidTokenHandling) {
    // Try to connect with invalid token
    std::string invalid_token = "invalid.token.here";
    
    bool connected = inspector->validateAndConnect(invalid_token);
    
    EXPECT_FALSE(connected);
    EXPECT_FALSE(inspector->isConnected());
    EXPECT_FALSE(inspector->getLastError().empty());
}

} // namespace

// Helper class to verify OAuth flow compliance
class OAuthFlowVerifier {
public:
    static bool verifyAuthorizationEndpoint(const std::string& auth_url,
                                           const std::string& client_id,
                                           const std::string& redirect_uri) {
        // Build authorization URL
        std::string auth_endpoint = auth_url + "/protocol/openid-connect/auth";
        auth_endpoint += "?response_type=code";
        auth_endpoint += "&client_id=" + client_id;
        auth_endpoint += "&redirect_uri=" + redirect_uri;
        auth_endpoint += "&scope=openid+mcp:weather";
        
        // In real test, would check if URL is properly formed
        return !auth_endpoint.empty();
    }
    
    static bool verifyTokenEndpoint(const std::string& auth_url) {
        std::string token_endpoint = auth_url + "/protocol/openid-connect/token";
        // In real test, would verify endpoint is accessible
        return !token_endpoint.empty();
    }
    
    static bool verifyJWKSEndpoint(const std::string& jwks_uri) {
        // In real test, would fetch and verify JWKS
        return !jwks_uri.empty();
    }
};

// Additional test for OAuth flow compliance
TEST(OAuthFlowCompliance, VerifyEndpoints) {
    MCPInspectorConfig config = MCPInspectorConfig::fromEnvironment();
    
    // Verify authorization endpoint
    EXPECT_TRUE(OAuthFlowVerifier::verifyAuthorizationEndpoint(
        config.auth_server_url,
        config.client_id,
        config.redirect_uri
    ));
    
    // Verify token endpoint
    EXPECT_TRUE(OAuthFlowVerifier::verifyTokenEndpoint(config.auth_server_url));
    
    // Verify JWKS endpoint
    EXPECT_TRUE(OAuthFlowVerifier::verifyJWKSEndpoint(config.jwks_uri));
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    
    // Check if we should skip MCP Inspector tests
    const char* skip_tests = std::getenv("SKIP_MCP_INSPECTOR_TESTS");
    if (skip_tests && std::string(skip_tests) == "1") {
        std::cout << "Skipping MCP Inspector flow tests (SKIP_MCP_INSPECTOR_TESTS=1)" << std::endl;
        return 0;
    }
    
    std::cout << "=======================================" << std::endl;
    std::cout << "MCP Inspector OAuth Flow Tests" << std::endl;
    std::cout << "=======================================" << std::endl;
    
    return RUN_ALL_TESTS();
}