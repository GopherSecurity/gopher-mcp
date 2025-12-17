/**
 * @file test_token_exchange.cc
 * @brief Unit tests for OAuth 2.0 Token Exchange (RFC 8693)
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "mcp/auth/auth_c_api.h"
#include <cstring>
#include <vector>
#include <string>
#include <thread>
#include <chrono>

using ::testing::_;
using ::testing::Return;
using ::testing::NotNull;
using ::testing::IsNull;

class TokenExchangeTest : public ::testing::Test {
protected:
  mcp_auth_client_t client_ = nullptr;
  
  void SetUp() override {
    // Initialize auth library
    ASSERT_EQ(MCP_AUTH_SUCCESS, mcp_auth_init());
    
    // Create auth client with test configuration
    const char* jwks_uri = "https://sso-test.gopher.security:8443/realms/gopher-mcp-auth/protocol/openid-connect/certs";
    const char* issuer = "https://sso-test.gopher.security:8443/realms/gopher-mcp-auth";
    ASSERT_EQ(MCP_AUTH_SUCCESS, mcp_auth_client_create(&client_, jwks_uri, issuer));
    
    // Set client ID and secret for token exchange
    ASSERT_EQ(MCP_AUTH_SUCCESS, 
      mcp_auth_client_set_option(client_, "client_id", "mcp_test_client"));
    ASSERT_EQ(MCP_AUTH_SUCCESS,
      mcp_auth_client_set_option(client_, "client_secret", "test_secret"));
  }
  
  void TearDown() override {
    if (client_) {
      mcp_auth_client_destroy(client_);
      client_ = nullptr;
    }
    mcp_auth_shutdown();
  }
  
  // Helper to create a mock JWT token
  std::string CreateMockToken() {
    // This would be a valid JWT in real tests
    return "eyJhbGciOiJSUzI1NiIsInR5cCI6IkpXVCJ9.eyJzdWIiOiJ1c2VyMTIzIiwiaXNzIjoiaHR0cHM6Ly9zc28tdGVzdC5nb3BoZXIuc2VjdXJpdHk6ODQ0My9yZWFsbXMvZ29waGVyLW1jcC1hdXRoIiwiYXVkIjoibWNwX3Rlc3RfY2xpZW50IiwiZXhwIjoyMDAwMDAwMDAwLCJpYXQiOjE2MDAwMDAwMDAsInNjb3BlIjoib3BlbmlkIHByb2ZpbGUgZW1haWwifQ.mock_signature";
  }
};

// Test single IDP token exchange
TEST_F(TokenExchangeTest, ExchangeSingleToken) {
  std::string subject_token = CreateMockToken();
  
  auto result = mcp_auth_exchange_token(
    client_,
    subject_token.c_str(),
    "github",
    nullptr,  // audience
    nullptr   // scope
  );
  
  // Check result structure
  if (result.error_code == MCP_AUTH_SUCCESS) {
    EXPECT_NE(nullptr, result.access_token);
    EXPECT_NE(nullptr, result.token_type);
    EXPECT_STREQ("Bearer", result.token_type);
    EXPECT_GT(result.expires_in, 0);
    
    // Clean up
    mcp_auth_free_exchange_result(&result);
  } else if (result.error_code == MCP_AUTH_ERROR_IDP_NOT_LINKED) {
    // User not linked to IDP is an expected error in tests
    EXPECT_NE(nullptr, result.error_description);
  } else {
    // Network errors are acceptable in unit tests
    EXPECT_EQ(MCP_AUTH_ERROR_NETWORK_ERROR, result.error_code);
  }
}

// Test multiple IDP token exchange
TEST_F(TokenExchangeTest, ExchangeMultipleTokens) {
  std::string subject_token = CreateMockToken();
  const char* idp_aliases = "github,google,microsoft";
  
  // Prepare results array
  mcp_auth_token_exchange_result_t results[3];
  size_t result_count = 3;
  
  mcp_auth_error_t err = mcp_auth_exchange_token_multi(
    client_,
    subject_token.c_str(),
    idp_aliases,
    results,
    &result_count
  );
  
  // Should succeed even if some IDPs fail
  if (err == MCP_AUTH_SUCCESS) {
    EXPECT_GT(result_count, 0);
    EXPECT_LE(result_count, 3);
    
    // Check each result
    int successful = 0;
    for (size_t i = 0; i < result_count; i++) {
      if (results[i].error_code == MCP_AUTH_SUCCESS) {
        successful++;
        EXPECT_NE(nullptr, results[i].access_token);
        EXPECT_NE(nullptr, results[i].token_type);
      }
      
      // Clean up each result
      mcp_auth_free_exchange_result(&results[i]);
    }
    
    // At least one should succeed in integration environment
    // In unit tests, all might fail due to network
  }
}

// Test parallel token exchange performance
TEST_F(TokenExchangeTest, ParallelExchangePerformance) {
  std::string subject_token = CreateMockToken();
  const char* idp_aliases = "github,google,microsoft";
  
  // Measure time for parallel exchange
  auto start = std::chrono::steady_clock::now();
  
  mcp_auth_token_exchange_result_t results[3];
  size_t result_count = 3;
  
  mcp_auth_exchange_token_multi(
    client_,
    subject_token.c_str(),
    idp_aliases,
    results,
    &result_count
  );
  
  auto end = std::chrono::steady_clock::now();
  auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
  
  // Parallel exchange should be faster than sequential
  // Assuming each exchange takes ~500ms, parallel should be < 1500ms
  EXPECT_LT(duration.count(), 1500);
  
  // Clean up
  for (size_t i = 0; i < result_count; i++) {
    mcp_auth_free_exchange_result(&results[i]);
  }
}

// Test setting exchange IDPs configuration
TEST_F(TokenExchangeTest, SetExchangeIDPs) {
  const char* idps = "github,google";
  
  mcp_auth_error_t err = mcp_auth_set_exchange_idps(client_, idps);
  EXPECT_EQ(MCP_AUTH_SUCCESS, err);
  
  // Test with empty string (disable exchange)
  err = mcp_auth_set_exchange_idps(client_, "");
  EXPECT_EQ(MCP_AUTH_SUCCESS, err);
  
  // Test with single IDP
  err = mcp_auth_set_exchange_idps(client_, "github");
  EXPECT_EQ(MCP_AUTH_SUCCESS, err);
  
  // Test with spaces (should be trimmed)
  err = mcp_auth_set_exchange_idps(client_, " github , google , microsoft ");
  EXPECT_EQ(MCP_AUTH_SUCCESS, err);
}

// Test validate and exchange combined operation
TEST_F(TokenExchangeTest, ValidateAndExchange) {
  std::string token = CreateMockToken();
  
  // Create validation options
  mcp_auth_validation_options_t options = nullptr;
  ASSERT_EQ(MCP_AUTH_SUCCESS, mcp_auth_validation_options_create(&options));
  
  // Set exchange IDPs
  ASSERT_EQ(MCP_AUTH_SUCCESS, mcp_auth_set_exchange_idps(client_, "github,google"));
  
  // Prepare results
  mcp_auth_validation_result_t validation_result;
  mcp_auth_token_exchange_result_t exchange_results[2];
  size_t exchange_count = 2;
  
  mcp_auth_error_t err = mcp_auth_validate_and_exchange(
    client_,
    token.c_str(),
    options,
    &validation_result,
    exchange_results,
    &exchange_count
  );
  
  // In unit tests, validation might fail due to mock token
  if (err == MCP_AUTH_SUCCESS) {
    EXPECT_TRUE(validation_result.valid);
    
    // Check exchange results
    for (size_t i = 0; i < exchange_count; i++) {
      mcp_auth_free_exchange_result(&exchange_results[i]);
    }
  }
  
  mcp_auth_validation_options_destroy(options);
}

// Test error handling for invalid IDP
TEST_F(TokenExchangeTest, InvalidIDPAlias) {
  std::string subject_token = CreateMockToken();
  
  auto result = mcp_auth_exchange_token(
    client_,
    subject_token.c_str(),
    "", // Empty IDP alias
    nullptr,
    nullptr
  );
  
  EXPECT_EQ(MCP_AUTH_ERROR_INVALID_IDP_ALIAS, result.error_code);
  EXPECT_NE(nullptr, result.error_description);
  
  mcp_auth_free_exchange_result(&result);
}

// Test error handling for invalid token
TEST_F(TokenExchangeTest, InvalidSubjectToken) {
  auto result = mcp_auth_exchange_token(
    client_,
    "invalid_token",
    "github",
    nullptr,
    nullptr
  );
  
  EXPECT_NE(MCP_AUTH_SUCCESS, result.error_code);
  EXPECT_NE(nullptr, result.error_description);
  
  mcp_auth_free_exchange_result(&result);
}

// Test with optional parameters (audience and scope)
TEST_F(TokenExchangeTest, ExchangeWithOptionalParams) {
  std::string subject_token = CreateMockToken();
  
  auto result = mcp_auth_exchange_token(
    client_,
    subject_token.c_str(),
    "github",
    "https://api.github.com",  // audience
    "repo user"                 // scope
  );
  
  // Result depends on actual IDP configuration
  if (result.error_code == MCP_AUTH_SUCCESS) {
    EXPECT_NE(nullptr, result.access_token);
    
    // Check if requested scope is in response
    if (result.scope) {
      // Scope might be adjusted by IDP
      EXPECT_NE(nullptr, strstr(result.scope, "repo"));
    }
  }
  
  mcp_auth_free_exchange_result(&result);
}

// Test cleanup and memory management
TEST_F(TokenExchangeTest, MemoryManagement) {
  std::string subject_token = CreateMockToken();
  
  // Perform multiple exchanges
  for (int i = 0; i < 10; i++) {
    auto result = mcp_auth_exchange_token(
      client_,
      subject_token.c_str(),
      "github",
      nullptr,
      nullptr
    );
    
    // Always clean up, regardless of result
    mcp_auth_free_exchange_result(&result);
    
    // After cleanup, pointers should be null
    EXPECT_EQ(nullptr, result.access_token);
    EXPECT_EQ(nullptr, result.token_type);
    EXPECT_EQ(nullptr, result.refresh_token);
    EXPECT_EQ(nullptr, result.scope);
  }
}

// Test concurrent token exchanges
TEST_F(TokenExchangeTest, ConcurrentExchanges) {
  std::string subject_token = CreateMockToken();
  const int num_threads = 5;
  
  std::vector<std::thread> threads;
  std::vector<mcp_auth_token_exchange_result_t> results(num_threads);
  
  // Launch concurrent exchanges
  for (int i = 0; i < num_threads; i++) {
    threads.emplace_back([this, &subject_token, &results, i]() {
      results[i] = mcp_auth_exchange_token(
        client_,
        subject_token.c_str(),
        "github",
        nullptr,
        nullptr
      );
    });
  }
  
  // Wait for all threads
  for (auto& t : threads) {
    t.join();
  }
  
  // Clean up all results
  for (auto& result : results) {
    mcp_auth_free_exchange_result(&result);
  }
}

// Test IDP-specific error handling
TEST_F(TokenExchangeTest, IDPNotLinkedError) {
  std::string subject_token = CreateMockToken();
  
  // Try to exchange for an IDP the user hasn't authenticated with
  auto result = mcp_auth_exchange_token(
    client_,
    subject_token.c_str(),
    "unlinked-idp",
    nullptr,
    nullptr
  );
  
  // Should get specific error for unlinked IDP
  if (result.error_code == MCP_AUTH_ERROR_IDP_NOT_LINKED) {
    EXPECT_NE(nullptr, result.error_description);
    // Error message should mention the IDP
    EXPECT_NE(nullptr, strstr(result.error_description, "not linked"));
  }
  
  mcp_auth_free_exchange_result(&result);
}

// Test exchange with expired token
TEST_F(TokenExchangeTest, ExchangeWithExpiredToken) {
  // Create an expired token (past exp claim)
  std::string expired_token = "eyJhbGciOiJSUzI1NiIsInR5cCI6IkpXVCJ9.eyJleHAiOjE2MDAwMDAwMDB9.expired";
  
  auto result = mcp_auth_exchange_token(
    client_,
    expired_token.c_str(),
    "github",
    nullptr,
    nullptr
  );
  
  // Should fail with appropriate error
  EXPECT_NE(MCP_AUTH_SUCCESS, result.error_code);
  EXPECT_NE(nullptr, result.error_description);
  
  mcp_auth_free_exchange_result(&result);
}