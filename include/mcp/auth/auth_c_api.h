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
  MCP_AUTH_ERROR_INTERNAL_ERROR = -1013
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