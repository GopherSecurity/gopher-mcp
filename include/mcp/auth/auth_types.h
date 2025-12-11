#ifndef MCP_AUTH_AUTH_TYPES_H
#define MCP_AUTH_AUTH_TYPES_H

#include <cstdint>
#include <string>
#include <vector>
#include <chrono>

/**
 * @file auth_types.h
 * @brief Core type definitions for the authentication module
 */

namespace mcp {
namespace auth {

// Forward declarations
class AuthClient;
struct TokenPayload;

/**
 * @brief Opaque handle type for auth client instances (FFI-compatible)
 */
using mcp_auth_handle_t = uint64_t;

/**
 * @brief Opaque handle type for token payload instances (FFI-compatible)  
 */
using mcp_token_payload_handle_t = uint64_t;

/**
 * @brief Authentication error codes
 */
enum class AuthErrorCode : int32_t {
  SUCCESS = 0,
  INVALID_TOKEN = -1000,
  EXPIRED_TOKEN = -1001,
  INVALID_SIGNATURE = -1002,
  INVALID_ISSUER = -1003,
  INVALID_AUDIENCE = -1004,
  INSUFFICIENT_SCOPES = -1005,
  MALFORMED_TOKEN = -1006,
  NETWORK_ERROR = -1007,
  CONFIGURATION_ERROR = -1008,
  INTERNAL_ERROR = -1009,
  MEMORY_ERROR = -1010,
  INVALID_PARAMETER = -1011,
  NOT_INITIALIZED = -1012,
  JWKS_ERROR = -1013,
  CACHE_ERROR = -1014
};

/**
 * @brief Token validation options
 */
struct TokenValidationOptions {
  std::string issuer;                      // Expected token issuer
  std::string audience;                    // Expected audience
  std::vector<std::string> required_scopes; // Required OAuth scopes
  bool verify_signature = true;            // Whether to verify JWT signature
  bool check_expiration = true;            // Whether to check token expiration
  bool check_not_before = true;            // Whether to check nbf claim
  std::chrono::seconds clock_skew{60};     // Allowed clock skew for time validation
};

/**
 * @brief JWT token payload structure
 */
struct TokenPayload {
  // Standard JWT claims
  std::string sub;                         // Subject (user ID)
  std::string iss;                         // Issuer
  std::string aud;                         // Audience
  std::chrono::system_clock::time_point exp; // Expiration time
  std::chrono::system_clock::time_point iat; // Issued at time
  std::chrono::system_clock::time_point nbf; // Not before time
  std::string jti;                         // JWT ID
  
  // OAuth 2.1 specific claims
  std::vector<std::string> scopes;         // OAuth scopes
  std::string client_id;                   // OAuth client ID
  
  // Custom claims for MCP
  std::string email;                       // User email
  std::string name;                        // User display name
  std::string organization_id;             // Organization identifier
  std::string server_id;                   // MCP server identifier
  
  // Additional metadata
  std::string token_type;                  // Token type (e.g., "Bearer")
  std::string algorithm;                   // Signature algorithm (e.g., "RS256")
};

/**
 * @brief OAuth metadata structure (RFC 8414)
 */
struct OAuthMetadata {
  std::string issuer;                      // Authorization server issuer
  std::string authorization_endpoint;       // Authorization endpoint URL
  std::string token_endpoint;              // Token endpoint URL
  std::string jwks_uri;                    // JWKS endpoint URL
  std::string registration_endpoint;        // Client registration endpoint
  std::vector<std::string> scopes_supported; // Supported OAuth scopes
  std::vector<std::string> response_types_supported; // Supported response types
  std::vector<std::string> grant_types_supported;    // Supported grant types
  std::vector<std::string> token_endpoint_auth_methods_supported; // Auth methods
  std::vector<std::string> code_challenge_methods_supported; // PKCE methods
  bool require_pkce = true;                // Whether PKCE is required (OAuth 2.1)
};

/**
 * @brief Authentication client configuration
 */
struct AuthConfig {
  std::string auth_server_url;             // Base URL of auth server
  std::string realm;                       // Auth realm/tenant
  std::string client_id;                   // OAuth client ID
  std::string client_secret;               // OAuth client secret (optional)
  std::string jwks_uri;                    // JWKS endpoint (optional, can be discovered)
  bool use_discovery = true;               // Use OAuth discovery endpoint
  std::chrono::seconds cache_duration{3600}; // Cache duration for JWKS
  size_t max_cache_size = 1000;            // Maximum cache entries
  std::chrono::seconds http_timeout{30};    // HTTP request timeout
  bool validate_ssl_certificates = true;    // SSL/TLS validation
};

/**
 * @brief Result structure for token validation
 */
struct ValidationResult {
  bool valid;                              // Whether validation succeeded
  AuthErrorCode error_code;                // Error code if validation failed
  std::string error_message;               // Human-readable error message
  TokenPayload payload;                    // Token payload if valid
};

/**
 * @brief Scope validation mode
 */
enum class ScopeValidationMode {
  REQUIRE_ALL,    // All required scopes must be present
  REQUIRE_ANY,    // At least one required scope must be present
  EXACT_MATCH     // Token scopes must exactly match required scopes
};

/**
 * @brief WWW-Authenticate header parameters
 */
struct WWWAuthenticateParams {
  std::string realm;                       // Auth realm
  std::string scope;                       // Required scopes
  std::string error;                       // Error code (invalid_token, etc.)
  std::string error_description;           // Human-readable error description
  std::string error_uri;                   // URI for error documentation
};

} // namespace auth
} // namespace mcp

#endif // MCP_AUTH_AUTH_TYPES_H