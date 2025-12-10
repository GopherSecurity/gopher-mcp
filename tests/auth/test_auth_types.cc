#include "mcp/auth/auth_types.h"
#include <gtest/gtest.h>
#include <chrono>

namespace mcp {
namespace auth {
namespace {

// Test fixture for auth types
class AuthTypesTest : public ::testing::Test {
protected:
  void SetUp() override {
    // Common setup if needed
  }
  
  void TearDown() override {
    // Cleanup if needed
  }
};

// Test that handle types have expected size for FFI
TEST_F(AuthTypesTest, HandleTypeSizes) {
  // Handles should be 64-bit for FFI compatibility
  EXPECT_EQ(sizeof(mcp_auth_handle_t), 8);
  EXPECT_EQ(sizeof(mcp_token_payload_handle_t), 8);
}

// Test error code enum values
TEST_F(AuthTypesTest, ErrorCodeValues) {
  // Success should be 0
  EXPECT_EQ(static_cast<int32_t>(AuthErrorCode::SUCCESS), 0);
  
  // Error codes should be negative
  EXPECT_LT(static_cast<int32_t>(AuthErrorCode::INVALID_TOKEN), 0);
  EXPECT_LT(static_cast<int32_t>(AuthErrorCode::EXPIRED_TOKEN), 0);
  EXPECT_LT(static_cast<int32_t>(AuthErrorCode::INVALID_SIGNATURE), 0);
  EXPECT_LT(static_cast<int32_t>(AuthErrorCode::INVALID_ISSUER), 0);
  EXPECT_LT(static_cast<int32_t>(AuthErrorCode::INVALID_AUDIENCE), 0);
  EXPECT_LT(static_cast<int32_t>(AuthErrorCode::INSUFFICIENT_SCOPES), 0);
  EXPECT_LT(static_cast<int32_t>(AuthErrorCode::MALFORMED_TOKEN), 0);
  EXPECT_LT(static_cast<int32_t>(AuthErrorCode::NETWORK_ERROR), 0);
  EXPECT_LT(static_cast<int32_t>(AuthErrorCode::CONFIGURATION_ERROR), 0);
  EXPECT_LT(static_cast<int32_t>(AuthErrorCode::INTERNAL_ERROR), 0);
  
  // Error codes should be unique
  std::vector<int32_t> error_codes = {
    static_cast<int32_t>(AuthErrorCode::INVALID_TOKEN),
    static_cast<int32_t>(AuthErrorCode::EXPIRED_TOKEN),
    static_cast<int32_t>(AuthErrorCode::INVALID_SIGNATURE),
    static_cast<int32_t>(AuthErrorCode::INVALID_ISSUER),
    static_cast<int32_t>(AuthErrorCode::INVALID_AUDIENCE),
    static_cast<int32_t>(AuthErrorCode::INSUFFICIENT_SCOPES),
    static_cast<int32_t>(AuthErrorCode::MALFORMED_TOKEN),
    static_cast<int32_t>(AuthErrorCode::NETWORK_ERROR),
    static_cast<int32_t>(AuthErrorCode::CONFIGURATION_ERROR),
    static_cast<int32_t>(AuthErrorCode::INTERNAL_ERROR),
    static_cast<int32_t>(AuthErrorCode::MEMORY_ERROR),
    static_cast<int32_t>(AuthErrorCode::INVALID_PARAMETER),
    static_cast<int32_t>(AuthErrorCode::NOT_INITIALIZED),
    static_cast<int32_t>(AuthErrorCode::JWKS_ERROR),
    static_cast<int32_t>(AuthErrorCode::CACHE_ERROR)
  };
  
  std::set<int32_t> unique_codes(error_codes.begin(), error_codes.end());
  EXPECT_EQ(unique_codes.size(), error_codes.size());
}

// Test TokenValidationOptions default values
TEST_F(AuthTypesTest, TokenValidationOptionsDefaults) {
  TokenValidationOptions options;
  
  // Check default values
  EXPECT_TRUE(options.issuer.empty());
  EXPECT_TRUE(options.audience.empty());
  EXPECT_TRUE(options.required_scopes.empty());
  EXPECT_TRUE(options.verify_signature);
  EXPECT_TRUE(options.check_expiration);
  EXPECT_TRUE(options.check_not_before);
  EXPECT_EQ(options.clock_skew.count(), 60);
}

// Test TokenPayload structure
TEST_F(AuthTypesTest, TokenPayloadStructure) {
  TokenPayload payload;
  
  // Check that all string fields are default-initialized
  EXPECT_TRUE(payload.sub.empty());
  EXPECT_TRUE(payload.iss.empty());
  EXPECT_TRUE(payload.aud.empty());
  EXPECT_TRUE(payload.jti.empty());
  EXPECT_TRUE(payload.client_id.empty());
  EXPECT_TRUE(payload.email.empty());
  EXPECT_TRUE(payload.name.empty());
  EXPECT_TRUE(payload.organization_id.empty());
  EXPECT_TRUE(payload.server_id.empty());
  EXPECT_TRUE(payload.token_type.empty());
  EXPECT_TRUE(payload.algorithm.empty());
  
  // Check that vector is empty
  EXPECT_TRUE(payload.scopes.empty());
  
  // Add a scope and verify
  payload.scopes.push_back("read");
  payload.scopes.push_back("write");
  EXPECT_EQ(payload.scopes.size(), 2);
  EXPECT_EQ(payload.scopes[0], "read");
  EXPECT_EQ(payload.scopes[1], "write");
}

// Test OAuthMetadata structure
TEST_F(AuthTypesTest, OAuthMetadataStructure) {
  OAuthMetadata metadata;
  
  // Check default values
  EXPECT_TRUE(metadata.issuer.empty());
  EXPECT_TRUE(metadata.authorization_endpoint.empty());
  EXPECT_TRUE(metadata.token_endpoint.empty());
  EXPECT_TRUE(metadata.jwks_uri.empty());
  EXPECT_TRUE(metadata.registration_endpoint.empty());
  EXPECT_TRUE(metadata.scopes_supported.empty());
  EXPECT_TRUE(metadata.response_types_supported.empty());
  EXPECT_TRUE(metadata.grant_types_supported.empty());
  EXPECT_TRUE(metadata.token_endpoint_auth_methods_supported.empty());
  EXPECT_TRUE(metadata.code_challenge_methods_supported.empty());
  EXPECT_TRUE(metadata.require_pkce); // OAuth 2.1 requires PKCE
}

// Test AuthConfig structure and defaults
TEST_F(AuthTypesTest, AuthConfigDefaults) {
  AuthConfig config;
  
  // Check string fields
  EXPECT_TRUE(config.auth_server_url.empty());
  EXPECT_TRUE(config.realm.empty());
  EXPECT_TRUE(config.client_id.empty());
  EXPECT_TRUE(config.client_secret.empty());
  EXPECT_TRUE(config.jwks_uri.empty());
  
  // Check default values
  EXPECT_TRUE(config.use_discovery);
  EXPECT_EQ(config.cache_duration.count(), 3600);
  EXPECT_EQ(config.max_cache_size, 1000);
  EXPECT_EQ(config.http_timeout.count(), 30);
  EXPECT_TRUE(config.validate_ssl_certificates);
}

// Test ValidationResult structure
TEST_F(AuthTypesTest, ValidationResultStructure) {
  ValidationResult result;
  
  // Set some values
  result.valid = true;
  result.error_code = AuthErrorCode::SUCCESS;
  result.error_message = "Token is valid";
  result.payload.sub = "user123";
  
  EXPECT_TRUE(result.valid);
  EXPECT_EQ(result.error_code, AuthErrorCode::SUCCESS);
  EXPECT_EQ(result.error_message, "Token is valid");
  EXPECT_EQ(result.payload.sub, "user123");
}

// Test ScopeValidationMode enum
TEST_F(AuthTypesTest, ScopeValidationModeValues) {
  // Just verify the enum values exist and are distinct
  auto require_all = ScopeValidationMode::REQUIRE_ALL;
  auto require_any = ScopeValidationMode::REQUIRE_ANY;
  auto exact_match = ScopeValidationMode::EXACT_MATCH;
  
  EXPECT_NE(require_all, require_any);
  EXPECT_NE(require_all, exact_match);
  EXPECT_NE(require_any, exact_match);
}

// Test WWWAuthenticateParams structure
TEST_F(AuthTypesTest, WWWAuthenticateParamsStructure) {
  WWWAuthenticateParams params;
  
  // Check default initialization
  EXPECT_TRUE(params.realm.empty());
  EXPECT_TRUE(params.scope.empty());
  EXPECT_TRUE(params.error.empty());
  EXPECT_TRUE(params.error_description.empty());
  EXPECT_TRUE(params.error_uri.empty());
  
  // Set values and verify
  params.realm = "gopher-auth";
  params.error = "invalid_token";
  params.error_description = "The access token expired";
  
  EXPECT_EQ(params.realm, "gopher-auth");
  EXPECT_EQ(params.error, "invalid_token");
  EXPECT_EQ(params.error_description, "The access token expired");
}

// Test time point handling in TokenPayload
TEST_F(AuthTypesTest, TokenPayloadTimePoints) {
  TokenPayload payload;
  
  // Set time points
  auto now = std::chrono::system_clock::now();
  auto exp_time = now + std::chrono::hours(1);
  auto nbf_time = now - std::chrono::minutes(5);
  
  payload.iat = now;
  payload.exp = exp_time;
  payload.nbf = nbf_time;
  
  // Verify time relationships
  EXPECT_LT(payload.nbf, payload.iat);
  EXPECT_LT(payload.iat, payload.exp);
  
  // Verify duration calculations work
  auto token_lifetime = payload.exp - payload.iat;
  auto expected_lifetime = std::chrono::hours(1);
  EXPECT_EQ(token_lifetime, expected_lifetime);
}

// Test structure sizes for ABI stability
TEST_F(AuthTypesTest, StructureSizeConsistency) {
  // Record structure sizes to detect ABI-breaking changes
  // These values may change but should be tracked
  size_t token_validation_options_size = sizeof(TokenValidationOptions);
  size_t token_payload_size = sizeof(TokenPayload);
  size_t oauth_metadata_size = sizeof(OAuthMetadata);
  size_t auth_config_size = sizeof(AuthConfig);
  size_t validation_result_size = sizeof(ValidationResult);
  size_t www_authenticate_params_size = sizeof(WWWAuthenticateParams);
  
  // Just verify they have reasonable sizes (not zero, not huge)
  EXPECT_GT(token_validation_options_size, 0);
  EXPECT_GT(token_payload_size, 0);
  EXPECT_GT(oauth_metadata_size, 0);
  EXPECT_GT(auth_config_size, 0);
  EXPECT_GT(validation_result_size, 0);
  EXPECT_GT(www_authenticate_params_size, 0);
  
  // Sanity check - structures shouldn't be unreasonably large
  EXPECT_LT(token_validation_options_size, 1024);
  EXPECT_LT(token_payload_size, 2048);
  EXPECT_LT(oauth_metadata_size, 2048);
  EXPECT_LT(auth_config_size, 1024);
  EXPECT_LT(validation_result_size, 2048);
  EXPECT_LT(www_authenticate_params_size, 1024);
}

} // namespace
} // namespace auth
} // namespace mcp