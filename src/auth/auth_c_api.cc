#include "mcp/auth/auth_c_api.h"
#include <string>
#include <memory>
#include <mutex>
#include <cstring>

namespace {

// Thread-local error storage
thread_local std::string g_last_error;
std::mutex g_init_mutex;
bool g_initialized = false;

// Set error message
void set_error(const std::string& error) {
  g_last_error = error;
}

// Clear error message
void clear_error() {
  g_last_error.clear();
}

// Duplicate string for C API
char* duplicate_string(const std::string& str) {
  char* result = static_cast<char*>(malloc(str.length() + 1));
  if (result) {
    std::strcpy(result, str.c_str());
  }
  return result;
}

} // anonymous namespace

extern "C" {

/* ========================================================================
 * Library Initialization
 * ======================================================================== */

mcp_auth_error_t mcp_auth_init(void) {
  std::lock_guard<std::mutex> lock(g_init_mutex);
  if (g_initialized) {
    return MCP_AUTH_SUCCESS;
  }
  
  // Initialize authentication subsystem
  g_initialized = true;
  clear_error();
  return MCP_AUTH_SUCCESS;
}

mcp_auth_error_t mcp_auth_shutdown(void) {
  std::lock_guard<std::mutex> lock(g_init_mutex);
  if (!g_initialized) {
    return MCP_AUTH_ERROR_NOT_INITIALIZED;
  }
  
  // Cleanup authentication subsystem
  g_initialized = false;
  clear_error();
  return MCP_AUTH_SUCCESS;
}

const char* mcp_auth_version(void) {
  return "1.0.0";
}

/* ========================================================================
 * Client Lifecycle
 * ======================================================================== */

struct mcp_auth_client {
  std::string jwks_uri;
  std::string issuer;
  // In real implementation, would contain JwksClient, JwtValidator, etc.
};

mcp_auth_error_t mcp_auth_client_create(
    mcp_auth_client_t* client,
    const char* jwks_uri,
    const char* issuer) {
  
  if (!g_initialized) {
    set_error("Library not initialized");
    return MCP_AUTH_ERROR_NOT_INITIALIZED;
  }
  
  if (!client || !jwks_uri || !issuer) {
    set_error("Invalid parameters");
    return MCP_AUTH_ERROR_INVALID_PARAMETER;
  }
  
  try {
    *client = new mcp_auth_client{jwks_uri, issuer};
    clear_error();
    return MCP_AUTH_SUCCESS;
  } catch (const std::exception& e) {
    set_error(e.what());
    return MCP_AUTH_ERROR_OUT_OF_MEMORY;
  }
}

mcp_auth_error_t mcp_auth_client_destroy(mcp_auth_client_t client) {
  if (!client) {
    set_error("Invalid client handle");
    return MCP_AUTH_ERROR_INVALID_PARAMETER;
  }
  
  delete client;
  clear_error();
  return MCP_AUTH_SUCCESS;
}

mcp_auth_error_t mcp_auth_client_set_option(
    mcp_auth_client_t client,
    const char* option,
    const char* value) {
  
  if (!client || !option || !value) {
    set_error("Invalid parameters");
    return MCP_AUTH_ERROR_INVALID_PARAMETER;
  }
  
  // In real implementation, would configure client options
  clear_error();
  return MCP_AUTH_SUCCESS;
}

/* ========================================================================
 * Validation Options
 * ======================================================================== */

struct mcp_auth_validation_options {
  std::string scopes;
  std::string audience;
  int64_t clock_skew = 60;
};

mcp_auth_error_t mcp_auth_validation_options_create(
    mcp_auth_validation_options_t* options) {
  
  if (!options) {
    set_error("Invalid parameters");
    return MCP_AUTH_ERROR_INVALID_PARAMETER;
  }
  
  try {
    *options = new mcp_auth_validation_options{};
    clear_error();
    return MCP_AUTH_SUCCESS;
  } catch (const std::exception& e) {
    set_error(e.what());
    return MCP_AUTH_ERROR_OUT_OF_MEMORY;
  }
}

mcp_auth_error_t mcp_auth_validation_options_destroy(
    mcp_auth_validation_options_t options) {
  
  if (!options) {
    set_error("Invalid options handle");
    return MCP_AUTH_ERROR_INVALID_PARAMETER;
  }
  
  delete options;
  clear_error();
  return MCP_AUTH_SUCCESS;
}

mcp_auth_error_t mcp_auth_validation_options_set_scopes(
    mcp_auth_validation_options_t options,
    const char* scopes) {
  
  if (!options || !scopes) {
    set_error("Invalid parameters");
    return MCP_AUTH_ERROR_INVALID_PARAMETER;
  }
  
  options->scopes = scopes;
  clear_error();
  return MCP_AUTH_SUCCESS;
}

mcp_auth_error_t mcp_auth_validation_options_set_audience(
    mcp_auth_validation_options_t options,
    const char* audience) {
  
  if (!options || !audience) {
    set_error("Invalid parameters");
    return MCP_AUTH_ERROR_INVALID_PARAMETER;
  }
  
  options->audience = audience;
  clear_error();
  return MCP_AUTH_SUCCESS;
}

mcp_auth_error_t mcp_auth_validation_options_set_clock_skew(
    mcp_auth_validation_options_t options,
    int64_t seconds) {
  
  if (!options) {
    set_error("Invalid parameters");
    return MCP_AUTH_ERROR_INVALID_PARAMETER;
  }
  
  options->clock_skew = seconds;
  clear_error();
  return MCP_AUTH_SUCCESS;
}

/* ========================================================================
 * Token Validation
 * ======================================================================== */

mcp_auth_error_t mcp_auth_validate_token(
    mcp_auth_client_t client,
    const char* token,
    mcp_auth_validation_options_t options,
    mcp_auth_validation_result_t* result) {
  
  if (!client || !token || !result) {
    set_error("Invalid parameters");
    return MCP_AUTH_ERROR_INVALID_PARAMETER;
  }
  
  // In real implementation, would perform actual JWT validation
  // For now, return a stub success result
  result->valid = true;
  result->error_code = MCP_AUTH_SUCCESS;
  result->error_message = nullptr;
  
  clear_error();
  return MCP_AUTH_SUCCESS;
}

/* ========================================================================
 * Token Payload Access
 * ======================================================================== */

struct mcp_auth_token_payload {
  std::string subject;
  std::string issuer;
  std::string audience;
  std::string scopes;
  int64_t expiration = 0;
};

mcp_auth_error_t mcp_auth_extract_payload(
    const char* token,
    mcp_auth_token_payload_t* payload) {
  
  if (!token || !payload) {
    set_error("Invalid parameters");
    return MCP_AUTH_ERROR_INVALID_PARAMETER;
  }
  
  try {
    // In real implementation, would parse JWT and extract payload
    *payload = new mcp_auth_token_payload{};
    clear_error();
    return MCP_AUTH_SUCCESS;
  } catch (const std::exception& e) {
    set_error(e.what());
    return MCP_AUTH_ERROR_OUT_OF_MEMORY;
  }
}

mcp_auth_error_t mcp_auth_payload_get_subject(
    mcp_auth_token_payload_t payload,
    char** value) {
  
  if (!payload || !value) {
    set_error("Invalid parameters");
    return MCP_AUTH_ERROR_INVALID_PARAMETER;
  }
  
  *value = duplicate_string(payload->subject);
  if (!*value && !payload->subject.empty()) {
    set_error("Out of memory");
    return MCP_AUTH_ERROR_OUT_OF_MEMORY;
  }
  
  clear_error();
  return MCP_AUTH_SUCCESS;
}

mcp_auth_error_t mcp_auth_payload_get_issuer(
    mcp_auth_token_payload_t payload,
    char** value) {
  
  if (!payload || !value) {
    set_error("Invalid parameters");
    return MCP_AUTH_ERROR_INVALID_PARAMETER;
  }
  
  *value = duplicate_string(payload->issuer);
  if (!*value && !payload->issuer.empty()) {
    set_error("Out of memory");
    return MCP_AUTH_ERROR_OUT_OF_MEMORY;
  }
  
  clear_error();
  return MCP_AUTH_SUCCESS;
}

mcp_auth_error_t mcp_auth_payload_get_audience(
    mcp_auth_token_payload_t payload,
    char** value) {
  
  if (!payload || !value) {
    set_error("Invalid parameters");
    return MCP_AUTH_ERROR_INVALID_PARAMETER;
  }
  
  *value = duplicate_string(payload->audience);
  if (!*value && !payload->audience.empty()) {
    set_error("Out of memory");
    return MCP_AUTH_ERROR_OUT_OF_MEMORY;
  }
  
  clear_error();
  return MCP_AUTH_SUCCESS;
}

mcp_auth_error_t mcp_auth_payload_get_scopes(
    mcp_auth_token_payload_t payload,
    char** value) {
  
  if (!payload || !value) {
    set_error("Invalid parameters");
    return MCP_AUTH_ERROR_INVALID_PARAMETER;
  }
  
  *value = duplicate_string(payload->scopes);
  if (!*value && !payload->scopes.empty()) {
    set_error("Out of memory");
    return MCP_AUTH_ERROR_OUT_OF_MEMORY;
  }
  
  clear_error();
  return MCP_AUTH_SUCCESS;
}

mcp_auth_error_t mcp_auth_payload_get_expiration(
    mcp_auth_token_payload_t payload,
    int64_t* value) {
  
  if (!payload || !value) {
    set_error("Invalid parameters");
    return MCP_AUTH_ERROR_INVALID_PARAMETER;
  }
  
  *value = payload->expiration;
  clear_error();
  return MCP_AUTH_SUCCESS;
}

mcp_auth_error_t mcp_auth_payload_get_claim(
    mcp_auth_token_payload_t payload,
    const char* claim_name,
    char** value) {
  
  if (!payload || !claim_name || !value) {
    set_error("Invalid parameters");
    return MCP_AUTH_ERROR_INVALID_PARAMETER;
  }
  
  // In real implementation, would look up custom claim
  *value = duplicate_string("");
  clear_error();
  return MCP_AUTH_SUCCESS;
}

mcp_auth_error_t mcp_auth_payload_destroy(mcp_auth_token_payload_t payload) {
  if (!payload) {
    set_error("Invalid payload handle");
    return MCP_AUTH_ERROR_INVALID_PARAMETER;
  }
  
  delete payload;
  clear_error();
  return MCP_AUTH_SUCCESS;
}

/* ========================================================================
 * OAuth Metadata
 * ======================================================================== */

mcp_auth_error_t mcp_auth_generate_www_authenticate(
    const char* realm,
    const char* error,
    const char* error_description,
    char** header) {
  
  if (!realm || !header) {
    set_error("Invalid parameters");
    return MCP_AUTH_ERROR_INVALID_PARAMETER;
  }
  
  // Build WWW-Authenticate header
  std::string result = "Bearer realm=\"" + std::string(realm) + "\"";
  if (error) {
    result += ", error=\"" + std::string(error) + "\"";
  }
  if (error_description) {
    result += ", error_description=\"" + std::string(error_description) + "\"";
  }
  
  *header = duplicate_string(result);
  if (!*header) {
    set_error("Out of memory");
    return MCP_AUTH_ERROR_OUT_OF_MEMORY;
  }
  
  clear_error();
  return MCP_AUTH_SUCCESS;
}

/* ========================================================================
 * Memory Management
 * ======================================================================== */

void mcp_auth_free_string(char* str) {
  free(str);
}

const char* mcp_auth_get_last_error(void) {
  return g_last_error.c_str();
}

void mcp_auth_clear_error(void) {
  clear_error();
}

/* ========================================================================
 * Utility Functions
 * ======================================================================== */

bool mcp_auth_validate_scopes(
    const char* required_scopes,
    const char* available_scopes) {
  
  if (!required_scopes || !available_scopes) {
    return false;
  }
  
  // In real implementation, would perform scope validation
  // For now, return true as stub
  return true;
}

const char* mcp_auth_error_to_string(mcp_auth_error_t error_code) {
  switch (error_code) {
    case MCP_AUTH_SUCCESS:
      return "Success";
    case MCP_AUTH_ERROR_INVALID_TOKEN:
      return "Invalid token";
    case MCP_AUTH_ERROR_EXPIRED_TOKEN:
      return "Token expired";
    case MCP_AUTH_ERROR_INVALID_SIGNATURE:
      return "Invalid signature";
    case MCP_AUTH_ERROR_INVALID_ISSUER:
      return "Invalid issuer";
    case MCP_AUTH_ERROR_INVALID_AUDIENCE:
      return "Invalid audience";
    case MCP_AUTH_ERROR_INSUFFICIENT_SCOPE:
      return "Insufficient scope";
    case MCP_AUTH_ERROR_JWKS_FETCH_FAILED:
      return "JWKS fetch failed";
    case MCP_AUTH_ERROR_INVALID_KEY:
      return "Invalid key";
    case MCP_AUTH_ERROR_NETWORK_ERROR:
      return "Network error";
    case MCP_AUTH_ERROR_INVALID_CONFIG:
      return "Invalid configuration";
    case MCP_AUTH_ERROR_OUT_OF_MEMORY:
      return "Out of memory";
    case MCP_AUTH_ERROR_INVALID_PARAMETER:
      return "Invalid parameter";
    case MCP_AUTH_ERROR_NOT_INITIALIZED:
      return "Library not initialized";
    case MCP_AUTH_ERROR_INTERNAL_ERROR:
      return "Internal error";
    default:
      return "Unknown error";
  }
}

} // extern "C"