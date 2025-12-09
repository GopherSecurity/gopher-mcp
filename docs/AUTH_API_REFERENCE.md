# JWT Authentication API Reference

## Table of Contents
- [Initialization](#initialization)
- [Client Management](#client-management)
- [Token Validation](#token-validation)
- [Payload Extraction](#payload-extraction)
- [Options Management](#options-management)
- [Error Handling](#error-handling)
- [Performance APIs](#performance-apis)

## Initialization

### `mcp_auth_init()`

Initialize the authentication library.

```c
void mcp_auth_init(void);
```

**Description**: Initializes internal structures and libraries (OpenSSL, CURL). Must be called before any other auth functions.

**Thread Safety**: Thread-safe, but should only be called once.

### `mcp_auth_shutdown()`

Shutdown the authentication library.

```c
void mcp_auth_shutdown(void);
```

**Description**: Cleans up all resources and cached data. Should be called when auth functionality is no longer needed.

## Client Management

### `mcp_auth_client_create()`

Create a new authentication client.

```c
mcp_auth_error_t mcp_auth_client_create(
    const mcp_auth_config_t* config,
    mcp_auth_client_t** client
);
```

**Parameters**:
- `config`: Configuration structure containing:
  - `jwks_uri`: URI to fetch JWKS (required)
  - `issuer`: Expected token issuer (required)
  - `cache_duration`: JWKS cache duration in seconds (default: 3600)
  - `auto_refresh`: Enable automatic JWKS refresh (default: true)
- `client`: Output pointer to created client

**Returns**: 
- `MCP_AUTH_SUCCESS` on success
- `MCP_AUTH_INVALID_CONFIG` if configuration is invalid
- `MCP_AUTH_OUT_OF_MEMORY` if allocation fails

**Example**:
```c
mcp_auth_config_t config = {
    .jwks_uri = "https://auth.example.com/.well-known/jwks.json",
    .issuer = "https://auth.example.com",
    .cache_duration = 3600,
    .auto_refresh = true
};

mcp_auth_client_t* client;
mcp_auth_error_t err = mcp_auth_client_create(&config, &client);
```

### `mcp_auth_client_destroy()`

Destroy an authentication client.

```c
void mcp_auth_client_destroy(mcp_auth_client_t* client);
```

**Parameters**:
- `client`: Client to destroy

**Description**: Frees all resources associated with the client, including cached JWKS.

## Token Validation

### `mcp_auth_validate_token()`

Validate a JWT token.

```c
mcp_auth_error_t mcp_auth_validate_token(
    mcp_auth_client_t* client,
    const char* token,
    const mcp_auth_validation_options_t* options,
    mcp_auth_validation_result_t* result
);
```

**Parameters**:
- `client`: Authentication client
- `token`: JWT token string to validate
- `options`: Optional validation options (can be NULL)
- `result`: Output validation result

**Returns**:
- `MCP_AUTH_SUCCESS`: Token is valid
- `MCP_AUTH_INVALID_TOKEN`: Token format is invalid
- `MCP_AUTH_EXPIRED_TOKEN`: Token has expired
- `MCP_AUTH_INVALID_SIGNATURE`: Signature verification failed
- `MCP_AUTH_INVALID_ISSUER`: Issuer doesn't match
- `MCP_AUTH_INVALID_AUDIENCE`: Audience doesn't match
- `MCP_AUTH_INSUFFICIENT_SCOPE`: Required scopes missing
- `MCP_AUTH_INVALID_KEY`: No matching key found

**Result Structure**:
```c
typedef struct {
    bool valid;               // Overall validation result
    char* subject;           // Token subject (sub claim)
    char* issuer;            // Token issuer (iss claim)
    char* audience;          // Token audience (aud claim)
    char* scope;             // Token scope claim
    int64_t expiration;      // Expiration timestamp
    int64_t issued_at;       // Issued at timestamp
    char* error_message;     // Error details if validation failed
} mcp_auth_validation_result_t;
```

### `mcp_auth_validate_scopes()`

Validate token scopes against requirements.

```c
mcp_auth_error_t mcp_auth_validate_scopes(
    const char* token_scopes,
    const char* required_scopes
);
```

**Parameters**:
- `token_scopes`: Space-separated scopes from token
- `required_scopes`: Space-separated required scopes

**Returns**:
- `MCP_AUTH_SUCCESS`: All required scopes present
- `MCP_AUTH_INSUFFICIENT_SCOPE`: Missing required scopes

**Scope Matching**:
- Exact match: `"read"` matches `"read"`
- Hierarchical: `"mcp:weather"` matches `"mcp:weather:read"`
- Multiple: All required scopes must be present

## Payload Extraction

### `mcp_auth_extract_payload()`

Extract and parse JWT payload without validation.

```c
mcp_auth_error_t mcp_auth_extract_payload(
    const char* token,
    mcp_auth_jwt_payload_t** payload
);
```

**Parameters**:
- `token`: JWT token string
- `payload`: Output parsed payload

**Returns**:
- `MCP_AUTH_SUCCESS`: Payload extracted successfully
- `MCP_AUTH_INVALID_TOKEN`: Token format invalid

**Payload Structure**:
```c
typedef struct {
    char* iss;      // Issuer
    char* sub;      // Subject
    char* aud;      // Audience (JSON string if array)
    int64_t exp;    // Expiration time
    int64_t iat;    // Issued at time
    int64_t nbf;    // Not before time
    char* scope;    // Scope claim
    char* jti;      // JWT ID
    char* raw_json; // Raw JSON payload
} mcp_auth_jwt_payload_t;
```

### `mcp_auth_payload_destroy()`

Free extracted payload.

```c
void mcp_auth_payload_destroy(mcp_auth_jwt_payload_t* payload);
```

## Options Management

### `mcp_auth_validation_options_create()`

Create validation options.

```c
mcp_auth_validation_options_t* mcp_auth_validation_options_create(void);
```

**Returns**: New validation options structure with defaults.

### `mcp_auth_validation_options_set_audience()`

Set expected audience.

```c
void mcp_auth_validation_options_set_audience(
    mcp_auth_validation_options_t* options,
    const char* audience
);
```

### `mcp_auth_validation_options_set_scopes()`

Set required scopes.

```c
void mcp_auth_validation_options_set_scopes(
    mcp_auth_validation_options_t* options,
    const char* scopes
);
```

### `mcp_auth_validation_options_set_clock_skew()`

Set clock skew tolerance.

```c
void mcp_auth_validation_options_set_clock_skew(
    mcp_auth_validation_options_t* options,
    int seconds
);
```

**Parameters**:
- `seconds`: Clock skew in seconds (default: 60)

### `mcp_auth_validation_options_destroy()`

Destroy validation options.

```c
void mcp_auth_validation_options_destroy(
    mcp_auth_validation_options_t* options
);
```

## Error Handling

### Error Codes

```c
typedef enum {
    MCP_AUTH_SUCCESS = 0,
    MCP_AUTH_INVALID_TOKEN = 1,
    MCP_AUTH_EXPIRED_TOKEN = 2,
    MCP_AUTH_INVALID_SIGNATURE = 3,
    MCP_AUTH_INVALID_KEY = 4,
    MCP_AUTH_INVALID_ISSUER = 5,
    MCP_AUTH_INVALID_AUDIENCE = 6,
    MCP_AUTH_INSUFFICIENT_SCOPE = 7,
    MCP_AUTH_NETWORK_ERROR = 8,
    MCP_AUTH_INVALID_CONFIG = 9,
    MCP_AUTH_OUT_OF_MEMORY = 10,
    MCP_AUTH_UNKNOWN_ERROR = 99
} mcp_auth_error_t;
```

### `mcp_auth_error_to_string()`

Convert error code to string.

```c
const char* mcp_auth_error_to_string(mcp_auth_error_t error);
```

### `mcp_auth_get_last_error()`

Get last error message.

```c
const char* mcp_auth_get_last_error(void);
```

**Returns**: Detailed error message for last operation.

**Thread Safety**: Thread-local storage used for error messages.

## Performance APIs

### `mcp_auth_verify_signature_optimized()`

Optimized signature verification with caching.

```c
bool mcp_auth_verify_signature_optimized(
    const char* signing_input,
    const char* signature,
    const char* public_key_pem,
    const char* algorithm
);
```

**Features**:
- Certificate caching
- Context pooling
- Sub-millisecond performance

### `mcp_auth_get_crypto_performance()`

Get cryptographic operation metrics.

```c
bool mcp_auth_get_crypto_performance(
    double* avg_microseconds,
    double* min_microseconds,
    double* max_microseconds,
    bool* is_sub_millisecond
);
```

### `mcp_auth_get_network_stats()`

Get network operation statistics.

```c
bool mcp_auth_get_network_stats(
    size_t* total_requests,
    size_t* connection_reuses,
    double* reuse_rate,
    double* dns_hit_rate,
    double* avg_latency_ms
);
```

### `mcp_auth_clear_connection_pool()`

Clear connection pool.

```c
void mcp_auth_clear_connection_pool(void);
```

**Description**: Closes all pooled connections. Useful for testing or when changing endpoints.

### `mcp_auth_clear_crypto_cache()`

Clear cryptographic caches.

```c
void mcp_auth_clear_crypto_cache(void);
```

**Description**: Clears certificate and context caches.

## Thread Safety

All functions are thread-safe with the following considerations:

1. `mcp_auth_init()` should be called once before any other operations
2. Each thread can use the same `mcp_auth_client_t` instance
3. Error messages are stored in thread-local storage
4. Caches use appropriate synchronization

## Memory Management

Rules for memory management:

1. All `create` functions require corresponding `destroy` calls
2. String fields in result structures are owned by the library
3. Use `mcp_auth_free_string()` for strings returned by the library
4. Result structures are valid until the next validation call

## Example: Complete Flow

```c
#include "mcp/auth/auth_c_api.h"
#include <stdio.h>

int main() {
    // Initialize
    mcp_auth_init();
    
    // Configure client
    mcp_auth_config_t config = {
        .jwks_uri = "https://auth.example.com/.well-known/jwks.json",
        .issuer = "https://auth.example.com",
        .cache_duration = 3600,
        .auto_refresh = true
    };
    
    // Create client
    mcp_auth_client_t* client;
    if (mcp_auth_client_create(&config, &client) != MCP_AUTH_SUCCESS) {
        fprintf(stderr, "Failed to create client: %s\n", 
                mcp_auth_get_last_error());
        return 1;
    }
    
    // Create validation options
    mcp_auth_validation_options_t* options = 
        mcp_auth_validation_options_create();
    mcp_auth_validation_options_set_audience(options, "my-api");
    mcp_auth_validation_options_set_scopes(options, "read write");
    
    // Validate token
    const char* token = "eyJhbGciOiJSUzI1NiI...";
    mcp_auth_validation_result_t result;
    
    mcp_auth_error_t err = mcp_auth_validate_token(
        client, token, options, &result);
    
    if (err == MCP_AUTH_SUCCESS) {
        printf("Token valid!\n");
        printf("Subject: %s\n", result.subject);
        printf("Scope: %s\n", result.scope);
    } else {
        printf("Validation failed: %s\n", 
               mcp_auth_error_to_string(err));
        if (result.error_message) {
            printf("Details: %s\n", result.error_message);
        }
    }
    
    // Cleanup
    mcp_auth_validation_options_destroy(options);
    mcp_auth_client_destroy(client);
    mcp_auth_shutdown();
    
    return 0;
}
```