#ifndef MCP_AUTH_AUTH_C_API_H
#define MCP_AUTH_AUTH_C_API_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/**
 * @file auth_c_api.h
 * @brief C API interface for authentication module (FFI compatible)
 * 
 * This header provides C-compatible function signatures for FFI bindings,
 * following existing mcp-cpp-sdk C API patterns for memory safety and
 * clear ownership semantics.
 */

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================
 * Type Definitions
 * ======================================================================== */

/**
 * @brief Opaque handle for authentication client
 */
typedef struct mcp_auth_client* mcp_auth_client_t;

/**
 * @brief Opaque handle for token payload
 */
typedef struct mcp_auth_token_payload* mcp_auth_token_payload_t;

/**
 * @brief Opaque handle for validation options
 */
typedef struct mcp_auth_validation_options* mcp_auth_validation_options_t;

/**
 * @brief Opaque handle for OAuth metadata
 */
typedef struct mcp_auth_metadata* mcp_auth_metadata_t;

/**
 * @brief Authentication error codes
 */
typedef enum {
  MCP_AUTH_SUCCESS = 0,
  MCP_AUTH_ERROR_INVALID_TOKEN = -1000,
  MCP_AUTH_ERROR_EXPIRED_TOKEN = -1001,
  MCP_AUTH_ERROR_INVALID_SIGNATURE = -1002,
  MCP_AUTH_ERROR_INVALID_ISSUER = -1003,
  MCP_AUTH_ERROR_INVALID_AUDIENCE = -1004,
  MCP_AUTH_ERROR_INSUFFICIENT_SCOPE = -1005,
  MCP_AUTH_ERROR_JWKS_FETCH_FAILED = -1006,
  MCP_AUTH_ERROR_INVALID_KEY = -1007,
  MCP_AUTH_ERROR_NETWORK_ERROR = -1008,
  MCP_AUTH_ERROR_INVALID_CONFIG = -1009,
  MCP_AUTH_ERROR_OUT_OF_MEMORY = -1010,
  MCP_AUTH_ERROR_INVALID_PARAMETER = -1011,
  MCP_AUTH_ERROR_NOT_INITIALIZED = -1012,
  MCP_AUTH_ERROR_INTERNAL_ERROR = -1013,
  MCP_AUTH_ERROR_TOKEN_EXCHANGE_FAILED = -1014,
  MCP_AUTH_ERROR_IDP_NOT_LINKED = -1015,
  MCP_AUTH_ERROR_INVALID_IDP_ALIAS = -1016
} mcp_auth_error_t;

/**
 * @brief Validation result structure
 */
typedef struct {
  bool valid;                    // Whether validation succeeded
  mcp_auth_error_t error_code;  // Error code if validation failed
  const char* error_message;     // Error message (caller must not free)
} mcp_auth_validation_result_t;

/* ========================================================================
 * Library Initialization
 * ======================================================================== */

/**
 * @brief Initialize the authentication library
 * @return Error code (MCP_AUTH_SUCCESS on success)
 */
mcp_auth_error_t mcp_auth_init(void);

/**
 * @brief Shutdown the authentication library
 * @return Error code (MCP_AUTH_SUCCESS on success)
 */
mcp_auth_error_t mcp_auth_shutdown(void);

/**
 * @brief Get library version string
 * @return Version string (caller must not free)
 */
const char* mcp_auth_version(void);

/* ========================================================================
 * Client Lifecycle
 * ======================================================================== */

/**
 * @brief Create a new authentication client
 * @param client Output handle for created client
 * @param jwks_uri JWKS endpoint URI
 * @param issuer Expected token issuer
 * @return Error code
 */
mcp_auth_error_t mcp_auth_client_create(
    mcp_auth_client_t* client,
    const char* jwks_uri,
    const char* issuer);

/**
 * @brief Destroy an authentication client
 * @param client Client handle to destroy
 * @return Error code
 */
mcp_auth_error_t mcp_auth_client_destroy(mcp_auth_client_t client);

/**
 * @brief Set client configuration option
 * @param client Client handle
 * @param option Option name
 * @param value Option value
 * @return Error code
 */
mcp_auth_error_t mcp_auth_client_set_option(
    mcp_auth_client_t client,
    const char* option,
    const char* value);

/* ========================================================================
 * Validation Options
 * ======================================================================== */

/**
 * @brief Create validation options
 * @param options Output handle for created options
 * @return Error code
 */
mcp_auth_error_t mcp_auth_validation_options_create(
    mcp_auth_validation_options_t* options);

/**
 * @brief Destroy validation options
 * @param options Options handle to destroy
 * @return Error code
 */
mcp_auth_error_t mcp_auth_validation_options_destroy(
    mcp_auth_validation_options_t options);

/**
 * @brief Set required scopes
 * @param options Options handle
 * @param scopes Space-separated scope string
 * @return Error code
 */
mcp_auth_error_t mcp_auth_validation_options_set_scopes(
    mcp_auth_validation_options_t options,
    const char* scopes);

/**
 * @brief Set audience validation
 * @param options Options handle
 * @param audience Expected audience
 * @return Error code
 */
mcp_auth_error_t mcp_auth_validation_options_set_audience(
    mcp_auth_validation_options_t options,
    const char* audience);

/**
 * @brief Set clock skew tolerance
 * @param options Options handle
 * @param seconds Clock skew in seconds
 * @return Error code
 */
mcp_auth_error_t mcp_auth_validation_options_set_clock_skew(
    mcp_auth_validation_options_t options,
    int64_t seconds);

/* ========================================================================
 * Token Validation
 * ======================================================================== */

/**
 * @brief Validate a JWT token
 * @param client Client handle
 * @param token JWT token string
 * @param options Validation options (can be NULL for defaults)
 * @param result Output validation result
 * @return Error code
 */
mcp_auth_error_t mcp_auth_validate_token(
    mcp_auth_client_t client,
    const char* token,
    mcp_auth_validation_options_t options,
    mcp_auth_validation_result_t* result);

/**
 * @brief Validate a JWT token (returns result by value for FFI compatibility)
 * @param client Client handle
 * @param token JWT token string
 * @param options Validation options (can be NULL for defaults)
 * @return Validation result struct
 */
mcp_auth_validation_result_t mcp_auth_validate_token_ret(
    mcp_auth_client_t client,
    const char* token,
    mcp_auth_validation_options_t options);

/**
 * @brief Extract token payload without validation
 * @param token JWT token string
 * @param payload Output handle for payload
 * @return Error code
 */
mcp_auth_error_t mcp_auth_extract_payload(
    const char* token,
    mcp_auth_token_payload_t* payload);

/* ========================================================================
 * Token Payload Access
 * ======================================================================== */

/**
 * @brief Get subject from token payload
 * @param payload Payload handle
 * @param value Output string (caller must free with mcp_auth_free_string)
 * @return Error code
 */
mcp_auth_error_t mcp_auth_payload_get_subject(
    mcp_auth_token_payload_t payload,
    char** value);

/**
 * @brief Get issuer from token payload
 * @param payload Payload handle
 * @param value Output string (caller must free with mcp_auth_free_string)
 * @return Error code
 */
mcp_auth_error_t mcp_auth_payload_get_issuer(
    mcp_auth_token_payload_t payload,
    char** value);

/**
 * @brief Get audience from token payload
 * @param payload Payload handle
 * @param value Output string (caller must free with mcp_auth_free_string)
 * @return Error code
 */
mcp_auth_error_t mcp_auth_payload_get_audience(
    mcp_auth_token_payload_t payload,
    char** value);

/**
 * @brief Get scopes from token payload
 * @param payload Payload handle
 * @param value Output string (caller must free with mcp_auth_free_string)
 * @return Error code
 */
mcp_auth_error_t mcp_auth_payload_get_scopes(
    mcp_auth_token_payload_t payload,
    char** value);

/**
 * @brief Get expiration time from token payload
 * @param payload Payload handle
 * @param value Output expiration timestamp
 * @return Error code
 */
mcp_auth_error_t mcp_auth_payload_get_expiration(
    mcp_auth_token_payload_t payload,
    int64_t* value);

/**
 * @brief Get custom claim from token payload
 * @param payload Payload handle
 * @param claim_name Claim name
 * @param value Output string (caller must free with mcp_auth_free_string)
 * @return Error code
 */
mcp_auth_error_t mcp_auth_payload_get_claim(
    mcp_auth_token_payload_t payload,
    const char* claim_name,
    char** value);

/**
 * @brief Destroy token payload handle
 * @param payload Payload handle to destroy
 * @return Error code
 */
mcp_auth_error_t mcp_auth_payload_destroy(mcp_auth_token_payload_t payload);

/* ========================================================================
 * OAuth Metadata
 * ======================================================================== */

/**
 * @brief Generate OAuth metadata JSON
 * @param metadata Metadata handle
 * @param json Output JSON string (caller must free with mcp_auth_free_string)
 * @return Error code
 */
mcp_auth_error_t mcp_auth_metadata_to_json(
    mcp_auth_metadata_t metadata,
    char** json);

/**
 * @brief Generate WWW-Authenticate header
 * @param realm Authentication realm
 * @param error Error code (can be NULL)
 * @param error_description Error description (can be NULL)
 * @param header Output header string (caller must free with mcp_auth_free_string)
 * @return Error code
 */
mcp_auth_error_t mcp_auth_generate_www_authenticate(
    const char* realm,
    const char* error,
    const char* error_description,
    char** header);

/* ========================================================================
 * Memory Management
 * ======================================================================== */

/**
 * @brief Free a string allocated by the library
 * @param str String to free
 */
void mcp_auth_free_string(char* str);

/**
 * @brief Get last error message
 * @return Error message string (caller must not free)
 */
const char* mcp_auth_get_last_error(void);

/**
 * @brief Clear last error
 */
void mcp_auth_clear_error(void);

/* ========================================================================
 * Token Exchange (RFC 8693)
 * ======================================================================== */

/**
 * @brief Token exchange result structure
 */
typedef struct {
  char* access_token;      // Exchanged access token (caller must free)
  char* token_type;        // Token type (e.g., "Bearer")
  int64_t expires_in;      // Expiration in seconds (-1 if not provided)
  char* refresh_token;     // Refresh token if provided (can be NULL)
  char* scope;             // Granted scopes if provided (can be NULL)
  mcp_auth_error_t error_code; // Error code if exchange failed
  const char* error_description; // Error description (do not free)
} mcp_auth_token_exchange_result_t;

/**
 * @brief Exchange a token for external IDP tokens
 * @param client Client handle
 * @param subject_token The token to exchange (typically Keycloak access token)
 * @param idp_aliases Comma-separated list of IDP aliases to exchange for
 * @param result Array of results (must be pre-allocated by caller)
 * @param result_count Number of results (in: array size, out: actual count)
 * @return Error code (MCP_AUTH_SUCCESS if at least one exchange succeeded)
 */
mcp_auth_error_t mcp_auth_exchange_token_multi(
    mcp_auth_client_t client,
    const char* subject_token,
    const char* idp_aliases,
    mcp_auth_token_exchange_result_t* results,
    size_t* result_count);

/**
 * @brief Exchange a token for a single external IDP token
 * @param client Client handle
 * @param subject_token The token to exchange
 * @param idp_alias The IDP alias to exchange for
 * @param audience Optional audience parameter
 * @param scope Optional scope parameter
 * @return Token exchange result (check error_code field)
 */
mcp_auth_token_exchange_result_t mcp_auth_exchange_token(
    mcp_auth_client_t client,
    const char* subject_token,
    const char* idp_alias,
    const char* audience,
    const char* scope);

/**
 * @brief Free token exchange result
 * @param result Pointer to result structure to free
 */
void mcp_auth_free_exchange_result(mcp_auth_token_exchange_result_t* result);

/**
 * @brief Set IDP configuration for automatic token exchange
 * @param client Client handle
 * @param exchange_idps Comma-separated list of IDP aliases
 * @return Error code
 */
mcp_auth_error_t mcp_auth_set_exchange_idps(
    mcp_auth_client_t client,
    const char* exchange_idps);

/**
 * @brief Validate and exchange token with configured IDPs
 * @param client Client handle  
 * @param token Token to validate and exchange
 * @param options Validation options
 * @param validation_result Validation result output
 * @param exchange_results Array for exchange results (can be NULL)
 * @param exchange_count Size of exchange results array
 * @return Error code (MCP_AUTH_SUCCESS if validation passed)
 */
mcp_auth_error_t mcp_auth_validate_and_exchange(
    mcp_auth_client_t client,
    const char* token,
    mcp_auth_validation_options_t options,
    mcp_auth_validation_result_t* validation_result,
    mcp_auth_token_exchange_result_t* exchange_results,
    size_t* exchange_count);

/* ========================================================================
 * Utility Functions
 * ======================================================================== */

/**
 * @brief Validate scope requirements
 * @param required_scopes Space-separated required scopes
 * @param available_scopes Space-separated available scopes
 * @return true if requirements are met
 */
bool mcp_auth_validate_scopes(
    const char* required_scopes,
    const char* available_scopes);

/**
 * @brief Get error description for error code
 * @param error_code Error code
 * @return Error description string (caller must not free)
 */
const char* mcp_auth_error_to_string(mcp_auth_error_t error_code);

#ifdef __cplusplus
}
#endif

#endif // MCP_AUTH_AUTH_C_API_H