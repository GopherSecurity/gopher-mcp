#ifndef MCP_AUTH_METADATA_GENERATOR_H
#define MCP_AUTH_METADATA_GENERATOR_H

#include <string>
#include <vector>
#include <unordered_map>
#include "mcp/core/optional.h"

/**
 * @file metadata_generator.h
 * @brief OAuth metadata and WWW-Authenticate header generation (RFC 8414)
 */

namespace mcp {
namespace auth {

/**
 * @brief OAuth 2.0 Authorization Server Metadata (RFC 8414)
 */
struct OAuthMetadata {
  // Required
  std::string issuer;                                    // Issuer identifier
  std::string authorization_endpoint;                    // Authorization endpoint URL
  std::string token_endpoint;                           // Token endpoint URL
  std::string jwks_uri;                                 // JWKS endpoint URL
  std::vector<std::string> response_types_supported;    // Supported response types
  std::vector<std::string> subject_types_supported;     // Supported subject types
  std::vector<std::string> id_token_signing_alg_values_supported; // Signing algorithms
  
  // Recommended
  mcp::optional<std::string> userinfo_endpoint;         // UserInfo endpoint
  mcp::optional<std::string> registration_endpoint;     // Client registration endpoint
  mcp::optional<std::vector<std::string>> scopes_supported; // Supported scopes
  mcp::optional<std::vector<std::string>> claims_supported; // Supported claims
  
  // Optional
  mcp::optional<std::string> revocation_endpoint;       // Token revocation endpoint
  mcp::optional<std::string> introspection_endpoint;    // Token introspection endpoint
  mcp::optional<std::vector<std::string>> response_modes_supported;
  mcp::optional<std::vector<std::string>> grant_types_supported;
  mcp::optional<std::vector<std::string>> acr_values_supported;
  mcp::optional<std::vector<std::string>> token_endpoint_auth_methods_supported;
  mcp::optional<std::vector<std::string>> token_endpoint_auth_signing_alg_values_supported;
  mcp::optional<std::vector<std::string>> display_values_supported;
  mcp::optional<std::vector<std::string>> claim_types_supported;
  mcp::optional<std::string> service_documentation;
  mcp::optional<std::vector<std::string>> claims_locales_supported;
  mcp::optional<std::vector<std::string>> ui_locales_supported;
  mcp::optional<bool> claims_parameter_supported;
  mcp::optional<bool> request_parameter_supported;
  mcp::optional<bool> request_uri_parameter_supported;
  mcp::optional<bool> require_request_uri_registration;
  mcp::optional<std::string> op_policy_uri;
  mcp::optional<std::string> op_tos_uri;
  
  // OAuth 2.1 specific
  mcp::optional<std::vector<std::string>> code_challenge_methods_supported;
  mcp::optional<bool> require_pkce;
  
  /**
   * @brief Convert to JSON string
   * @return JSON representation of metadata
   */
  std::string to_json() const;
  
  /**
   * @brief Parse from JSON string
   * @param json JSON string to parse
   * @return Parsed metadata or nullopt on error
   */
  static mcp::optional<OAuthMetadata> from_json(const std::string& json);
};

/**
 * @brief WWW-Authenticate header parameters
 */
struct WwwAuthenticateParams {
  std::string realm;                           // Authentication realm
  mcp::optional<std::string> scope;           // Required scope
  mcp::optional<std::string> error;           // Error code
  mcp::optional<std::string> error_description; // Human-readable error
  mcp::optional<std::string> error_uri;       // Error documentation URI
  
  // Additional parameters
  std::unordered_map<std::string, std::string> additional_params;
};

/**
 * @brief OAuth error response
 */
struct OAuthError {
  std::string error;                           // Error code (required)
  mcp::optional<std::string> error_description; // Human-readable description
  mcp::optional<std::string> error_uri;        // URI for error documentation
  mcp::optional<int> status_code;              // HTTP status code
  
  /**
   * @brief Convert to JSON string
   * @return JSON representation of error
   */
  std::string to_json() const;
  
  /**
   * @brief Create invalid_request error
   */
  static OAuthError invalid_request(const std::string& description = "");
  
  /**
   * @brief Create invalid_token error
   */
  static OAuthError invalid_token(const std::string& description = "");
  
  /**
   * @brief Create insufficient_scope error
   */
  static OAuthError insufficient_scope(const std::string& required_scope = "");
  
  /**
   * @brief Create unauthorized_client error
   */
  static OAuthError unauthorized_client(const std::string& description = "");
  
  /**
   * @brief Create access_denied error
   */
  static OAuthError access_denied(const std::string& description = "");
};

/**
 * @brief Metadata generator for OAuth 2.1 compliance
 */
class MetadataGenerator {
public:
  /**
   * @brief Default constructor
   */
  MetadataGenerator();
  
  /**
   * @brief Destructor
   */
  ~MetadataGenerator();
  
  /**
   * @brief Generate WWW-Authenticate header value
   * @param scheme Authentication scheme (e.g., "Bearer")
   * @param params WWW-Authenticate parameters
   * @return Formatted header value
   */
  static std::string generate_www_authenticate(const std::string& scheme,
                                               const WwwAuthenticateParams& params);
  
  /**
   * @brief Parse WWW-Authenticate header value
   * @param header Header value to parse
   * @return Parsed parameters or nullopt on error
   */
  static mcp::optional<WwwAuthenticateParams> parse_www_authenticate(const std::string& header);
  
  /**
   * @brief Generate OAuth metadata JSON
   * @param metadata OAuth metadata structure
   * @return JSON string
   */
  static std::string generate_metadata_json(const OAuthMetadata& metadata);
  
  /**
   * @brief Generate error response JSON
   * @param error OAuth error structure
   * @return JSON string
   */
  static std::string generate_error_json(const OAuthError& error);
  
  /**
   * @brief Create well-known metadata endpoint path
   * @param issuer Issuer URL
   * @return Well-known metadata path
   */
  static std::string get_well_known_path(const std::string& issuer);
  
  /**
   * @brief Validate metadata for RFC 8414 compliance
   * @param metadata Metadata to validate
   * @return Error message if invalid, empty string if valid
   */
  static std::string validate_metadata(const OAuthMetadata& metadata);
  
  /**
   * @brief Build metadata from configuration
   */
  class Builder {
  public:
    Builder();
    ~Builder();
    
    Builder& set_issuer(const std::string& issuer);
    Builder& set_authorization_endpoint(const std::string& endpoint);
    Builder& set_token_endpoint(const std::string& endpoint);
    Builder& set_jwks_uri(const std::string& uri);
    Builder& add_response_type(const std::string& type);
    Builder& add_subject_type(const std::string& type);
    Builder& add_signing_algorithm(const std::string& alg);
    Builder& add_scope(const std::string& scope);
    Builder& add_claim(const std::string& claim);
    Builder& set_userinfo_endpoint(const std::string& endpoint);
    Builder& set_registration_endpoint(const std::string& endpoint);
    Builder& set_revocation_endpoint(const std::string& endpoint);
    Builder& set_introspection_endpoint(const std::string& endpoint);
    Builder& add_grant_type(const std::string& type);
    Builder& add_code_challenge_method(const std::string& method);
    Builder& require_pkce(bool require = true);
    
    /**
     * @brief Build the metadata
     * @return Built metadata or nullopt if invalid
     */
    mcp::optional<OAuthMetadata> build();
    
  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
  };

private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace auth
} // namespace mcp

#endif // MCP_AUTH_METADATA_GENERATOR_H