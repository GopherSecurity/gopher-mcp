#ifndef MCP_AUTH_JWT_VALIDATOR_H
#define MCP_AUTH_JWT_VALIDATOR_H

#include <string>
#include <vector>
#include <memory>
#include <chrono>
#include <unordered_map>
#include "mcp/core/optional.h"
#include "mcp/auth/auth_types.h"

/**
 * @file jwt_validator.h
 * @brief JWT validation engine with JWKS integration
 */

namespace mcp {
namespace auth {

// Forward declarations
class JwksClient;
class ScopeValidator;
struct JsonWebKey;

/**
 * @brief JWT header information
 */
struct JwtHeader {
  std::string alg;  // Algorithm (RS256, ES256, etc.)
  std::string typ;  // Type (JWT)
  std::string kid;  // Key ID
  
  mcp::optional<std::string> jku;  // JWK Set URL
  mcp::optional<std::string> x5u;  // X.509 URL
};

/**
 * @brief JWT claims (payload)
 */
struct JwtClaims {
  // Standard claims
  mcp::optional<std::string> iss;     // Issuer
  mcp::optional<std::string> sub;     // Subject
  mcp::optional<std::string> aud;     // Audience (can be array)
  mcp::optional<int64_t> exp;         // Expiration time
  mcp::optional<int64_t> nbf;         // Not before
  mcp::optional<int64_t> iat;         // Issued at
  mcp::optional<std::string> jti;     // JWT ID
  
  // OAuth 2.1 specific
  mcp::optional<std::string> scope;   // Space-separated scopes
  mcp::optional<std::string> client_id;
  
  // Additional claims
  std::unordered_map<std::string, std::string> custom_claims;
  
  /**
   * @brief Check if token is expired
   * @return true if expired based on exp claim
   */
  bool is_expired() const;
  
  /**
   * @brief Check if token is active (not before time has passed)
   * @return true if nbf time has passed or nbf not set
   */
  bool is_active() const;
  
  /**
   * @brief Get scopes as vector
   * @return Vector of individual scope strings
   */
  std::vector<std::string> get_scopes() const;
};

/**
 * @brief JWT validation configuration
 */
struct JwtValidationConfig {
  bool verify_signature;              // Verify JWT signature
  bool verify_exp;                    // Verify expiration
  bool verify_nbf;                    // Verify not before
  bool verify_iat;                    // Verify issued at
  bool verify_aud;                    // Verify audience
  bool verify_iss;                    // Verify issuer
  
  std::vector<std::string> valid_issuers;    // List of valid issuers
  std::vector<std::string> valid_audiences;  // List of valid audiences
  std::chrono::seconds clock_skew;           // Allowed clock skew
  std::chrono::seconds max_age;              // Maximum token age
  
  bool require_exp;                   // Require exp claim
  bool require_nbf;                   // Require nbf claim
  bool require_iat;                   // Require iat claim
  
  JwtValidationConfig();
};

/**
 * @brief JWT validation result
 */
struct JwtValidationResult {
  bool valid;
  std::string error_message;
  AuthErrorCode error_code;
  
  mcp::optional<JwtHeader> header;
  mcp::optional<JwtClaims> claims;
  
  // Validation details
  bool signature_valid;
  bool expiry_valid;
  bool not_before_valid;
  bool audience_valid;
  bool issuer_valid;
  bool scope_valid;
};

/**
 * @brief JWT validator with JWKS and scope validation support
 */
class JwtValidator {
public:
  /**
   * @brief Construct JWT validator
   * @param config Validation configuration
   */
  explicit JwtValidator(const JwtValidationConfig& config);
  
  /**
   * @brief Destructor
   */
  ~JwtValidator();
  
  /**
   * @brief Set JWKS client for key retrieval
   * @param jwks_client JWKS client instance
   */
  void set_jwks_client(std::shared_ptr<JwksClient> jwks_client);
  
  /**
   * @brief Set scope validator
   * @param scope_validator Scope validator instance
   */
  void set_scope_validator(std::shared_ptr<ScopeValidator> scope_validator);
  
  /**
   * @brief Validate a JWT token
   * @param token JWT token string
   * @param required_scopes Optional required scopes
   * @return Validation result
   */
  JwtValidationResult validate(const std::string& token,
                               const std::vector<std::string>& required_scopes = {});
  
  /**
   * @brief Parse JWT without validation (for inspection)
   * @param token JWT token string
   * @return Parsed header and claims if parseable
   */
  JwtValidationResult parse(const std::string& token);
  
  /**
   * @brief Verify JWT signature
   * @param token JWT token string
   * @param key JSON Web Key to use for verification
   * @return true if signature is valid
   */
  bool verify_signature(const std::string& token, const JsonWebKey& key);
  
  /**
   * @brief Extract header from JWT
   * @param token JWT token string
   * @return Header if extractable
   */
  static mcp::optional<JwtHeader> extract_header(const std::string& token);
  
  /**
   * @brief Extract claims from JWT
   * @param token JWT token string
   * @return Claims if extractable
   */
  static mcp::optional<JwtClaims> extract_claims(const std::string& token);
  
  /**
   * @brief Split JWT into parts
   * @param token JWT token string
   * @return Vector with header, payload, signature parts
   */
  static std::vector<std::string> split_token(const std::string& token);
  
  /**
   * @brief Base64URL decode
   * @param input Base64URL encoded string
   * @return Decoded string
   */
  static std::string base64url_decode(const std::string& input);
  
  /**
   * @brief Base64URL encode
   * @param input String to encode
   * @return Base64URL encoded string
   */
  static std::string base64url_encode(const std::string& input);
  
  /**
   * @brief Get validation statistics
   */
  struct ValidationStats {
    size_t tokens_validated;
    size_t validation_successes;
    size_t validation_failures;
    size_t signature_failures;
    size_t expiry_failures;
    size_t scope_failures;
  };
  
  ValidationStats get_stats() const;
  
  /**
   * @brief Reset validation statistics
   */
  void reset_stats();
  
  /**
   * @brief Update configuration
   * @param config New validation configuration
   */
  void update_config(const JwtValidationConfig& config);

private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace auth
} // namespace mcp

#endif // MCP_AUTH_JWT_VALIDATOR_H