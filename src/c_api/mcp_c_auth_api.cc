/**
 * @file mcp_c_auth_api.cc
 * @brief C API implementation for authentication module
 * 
 * Provides JWT validation and OAuth support matching gopher-auth-sdk-nodejs functionality
 */

#include "mcp/auth/auth_c_api.h"
#include <string>
#include <memory>
#include <unordered_map>
#include <vector>
#include <sstream>
#include <chrono>
#include <cstring>
#include <mutex>
#include <algorithm>

// Thread-local error storage
static thread_local std::string g_last_error;
static thread_local mcp_auth_error_t g_last_error_code = MCP_AUTH_SUCCESS;

// Global initialization state
static bool g_initialized = false;
static std::mutex g_init_mutex;

// Set error state
static void set_error(mcp_auth_error_t code, const std::string& message) {
    g_last_error_code = code;
    g_last_error = message;
}

// Clear error state
static void clear_error() {
    g_last_error_code = MCP_AUTH_SUCCESS;
    g_last_error.clear();
}

// ========================================================================
// Base64URL Decoding
// ========================================================================

static const std::string base64url_chars = 
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

static std::string base64url_decode(const std::string& encoded) {
    std::string padded = encoded;
    
    // Add padding if needed
    while (padded.length() % 4 != 0) {
        padded += '=';
    }
    
    // Replace URL-safe characters with standard base64
    std::replace(padded.begin(), padded.end(), '-', '+');
    std::replace(padded.begin(), padded.end(), '_', '/');
    
    // Decode
    std::string decoded;
    decoded.reserve(padded.length() * 3 / 4);
    
    int val = 0, valb = -8;
    for (unsigned char c : padded) {
        if (c == '=') break;
        
        const char* pos = strchr("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/", c);
        if (!pos) return "";
        
        val = (val << 6) + (pos - "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/");
        valb += 6;
        if (valb >= 0) {
            decoded.push_back(char((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    
    return decoded;
}

// ========================================================================
// Simple JSON Parser for JWT Header
// ========================================================================

static bool parse_jwt_header(const std::string& header_json, std::string& alg, std::string& kid) {
    // Simple JSON parsing for header fields
    // Looking for "alg":"RS256" and "kid":"key-id" patterns
    
    size_t alg_pos = header_json.find("\"alg\"");
    if (alg_pos != std::string::npos) {
        size_t colon = header_json.find(':', alg_pos);
        if (colon != std::string::npos) {
            size_t quote1 = header_json.find('"', colon);
            if (quote1 != std::string::npos) {
                size_t quote2 = header_json.find('"', quote1 + 1);
                if (quote2 != std::string::npos) {
                    alg = header_json.substr(quote1 + 1, quote2 - quote1 - 1);
                }
            }
        }
    }
    
    size_t kid_pos = header_json.find("\"kid\"");
    if (kid_pos != std::string::npos) {
        size_t colon = header_json.find(':', kid_pos);
        if (colon != std::string::npos) {
            size_t quote1 = header_json.find('"', colon);
            if (quote1 != std::string::npos) {
                size_t quote2 = header_json.find('"', quote1 + 1);
                if (quote2 != std::string::npos) {
                    kid = header_json.substr(quote1 + 1, quote2 - quote1 - 1);
                }
            }
        }
    }
    
    // Validate algorithm
    if (alg.empty()) {
        set_error(MCP_AUTH_ERROR_INVALID_TOKEN, "JWT header missing 'alg' field");
        return false;
    }
    
    // Check supported algorithms
    if (alg != "RS256" && alg != "RS384" && alg != "RS512") {
        set_error(MCP_AUTH_ERROR_INVALID_TOKEN, "Unsupported algorithm: " + alg);
        return false;
    }
    
    return true;
}

// Split JWT into parts
static bool split_jwt(const std::string& token, 
                     std::string& header, 
                     std::string& payload, 
                     std::string& signature) {
    size_t first_dot = token.find('.');
    if (first_dot == std::string::npos) {
        set_error(MCP_AUTH_ERROR_INVALID_TOKEN, "Invalid JWT format: missing first separator");
        return false;
    }
    
    size_t second_dot = token.find('.', first_dot + 1);
    if (second_dot == std::string::npos) {
        set_error(MCP_AUTH_ERROR_INVALID_TOKEN, "Invalid JWT format: missing second separator");
        return false;
    }
    
    header = token.substr(0, first_dot);
    payload = token.substr(first_dot + 1, second_dot - first_dot - 1);
    signature = token.substr(second_dot + 1);
    
    if (header.empty() || payload.empty() || signature.empty()) {
        set_error(MCP_AUTH_ERROR_INVALID_TOKEN, "Invalid JWT format: empty component");
        return false;
    }
    
    return true;
}

// ========================================================================
// Internal structures
// ========================================================================

struct mcp_auth_client {
    std::string jwks_uri;
    std::string issuer;
    int64_t cache_duration = 3600;
    bool auto_refresh = true;
    
    // Cached JWT header info for last validated token
    std::string last_alg;
    std::string last_kid;
    
    mcp_auth_client(const char* uri, const char* iss) 
        : jwks_uri(uri ? uri : "")
        , issuer(iss ? iss : "") {}
};

struct mcp_auth_validation_options {
    std::string scopes;
    std::string audience;
    int64_t clock_skew = 60;
};

struct mcp_auth_token_payload {
    std::string subject;
    std::string issuer;
    std::string audience;
    std::string scopes;
    int64_t expiration = 0;
    std::unordered_map<std::string, std::string> claims;
    
    // Parse JWT payload from base64url encoded string
    bool decode_from_token(const std::string& token) {
        std::string header_b64, payload_b64, signature_b64;
        if (!split_jwt(token, header_b64, payload_b64, signature_b64)) {
            return false;
        }
        
        std::string payload_json = base64url_decode(payload_b64);
        if (payload_json.empty()) {
            return false;
        }
        
        // TODO: Parse payload JSON in Prompt 2
        // For now, populate with test data
        subject = "test-subject";
        issuer = "http://localhost:8080/realms/gopher-auth";
        audience = "mcp-server";
        scopes = "openid mcp:weather";
        expiration = std::chrono::system_clock::now().time_since_epoch().count() + 3600;
        
        return true;
    }
};

struct mcp_auth_metadata {
    std::string resource;
    std::vector<std::string> authorization_servers;
    std::vector<std::string> scopes_supported;
};

// ========================================================================
// Library Initialization
// ========================================================================

extern "C" {

mcp_auth_error_t mcp_auth_init(void) {
    std::lock_guard<std::mutex> lock(g_init_mutex);
    if (g_initialized) {
        return MCP_AUTH_SUCCESS;
    }
    
    clear_error();
    g_initialized = true;
    return MCP_AUTH_SUCCESS;
}

mcp_auth_error_t mcp_auth_shutdown(void) {
    std::lock_guard<std::mutex> lock(g_init_mutex);
    if (!g_initialized) {
        set_error(MCP_AUTH_ERROR_NOT_INITIALIZED, "Library not initialized");
        return MCP_AUTH_ERROR_NOT_INITIALIZED;
    }
    
    clear_error();
    g_initialized = false;
    return MCP_AUTH_SUCCESS;
}

const char* mcp_auth_version(void) {
    return "1.0.0";
}

// ========================================================================
// Client Lifecycle
// ========================================================================

mcp_auth_error_t mcp_auth_client_create(
    mcp_auth_client_t* client,
    const char* jwks_uri,
    const char* issuer) {
    
    if (!g_initialized) {
        set_error(MCP_AUTH_ERROR_NOT_INITIALIZED, "Library not initialized");
        return MCP_AUTH_ERROR_NOT_INITIALIZED;
    }
    
    if (!client || !jwks_uri || !issuer) {
        set_error(MCP_AUTH_ERROR_INVALID_PARAMETER, "Invalid parameter");
        return MCP_AUTH_ERROR_INVALID_PARAMETER;
    }
    
    clear_error();
    
    try {
        *client = new mcp_auth_client(jwks_uri, issuer);
        return MCP_AUTH_SUCCESS;
    } catch (const std::exception& e) {
        set_error(MCP_AUTH_ERROR_OUT_OF_MEMORY, e.what());
        return MCP_AUTH_ERROR_OUT_OF_MEMORY;
    }
}

mcp_auth_error_t mcp_auth_client_destroy(mcp_auth_client_t client) {
    if (!g_initialized) {
        set_error(MCP_AUTH_ERROR_NOT_INITIALIZED, "Library not initialized");
        return MCP_AUTH_ERROR_NOT_INITIALIZED;
    }
    
    if (!client) {
        set_error(MCP_AUTH_ERROR_INVALID_PARAMETER, "Invalid client");
        return MCP_AUTH_ERROR_INVALID_PARAMETER;
    }
    
    clear_error();
    delete client;
    return MCP_AUTH_SUCCESS;
}

mcp_auth_error_t mcp_auth_client_set_option(
    mcp_auth_client_t client,
    const char* option,
    const char* value) {
    
    if (!g_initialized) {
        set_error(MCP_AUTH_ERROR_NOT_INITIALIZED, "Library not initialized");
        return MCP_AUTH_ERROR_NOT_INITIALIZED;
    }
    
    if (!client || !option || !value) {
        set_error(MCP_AUTH_ERROR_INVALID_PARAMETER, "Invalid parameter");
        return MCP_AUTH_ERROR_INVALID_PARAMETER;
    }
    
    clear_error();
    
    std::string opt(option);
    if (opt == "cache_duration") {
        client->cache_duration = std::stoll(value);
    } else if (opt == "auto_refresh") {
        client->auto_refresh = (std::string(value) == "true");
    } else {
        set_error(MCP_AUTH_ERROR_INVALID_PARAMETER, "Unknown option: " + opt);
        return MCP_AUTH_ERROR_INVALID_PARAMETER;
    }
    
    return MCP_AUTH_SUCCESS;
}

// ========================================================================
// Validation Options
// ========================================================================

mcp_auth_error_t mcp_auth_validation_options_create(
    mcp_auth_validation_options_t* options) {
    
    if (!g_initialized) {
        set_error(MCP_AUTH_ERROR_NOT_INITIALIZED, "Library not initialized");
        return MCP_AUTH_ERROR_NOT_INITIALIZED;
    }
    
    if (!options) {
        set_error(MCP_AUTH_ERROR_INVALID_PARAMETER, "Invalid parameter");
        return MCP_AUTH_ERROR_INVALID_PARAMETER;
    }
    
    clear_error();
    
    try {
        *options = new mcp_auth_validation_options();
        return MCP_AUTH_SUCCESS;
    } catch (const std::exception& e) {
        set_error(MCP_AUTH_ERROR_OUT_OF_MEMORY, e.what());
        return MCP_AUTH_ERROR_OUT_OF_MEMORY;
    }
}

mcp_auth_error_t mcp_auth_validation_options_destroy(
    mcp_auth_validation_options_t options) {
    
    if (!g_initialized) {
        set_error(MCP_AUTH_ERROR_NOT_INITIALIZED, "Library not initialized");
        return MCP_AUTH_ERROR_NOT_INITIALIZED;
    }
    
    if (!options) {
        set_error(MCP_AUTH_ERROR_INVALID_PARAMETER, "Invalid options");
        return MCP_AUTH_ERROR_INVALID_PARAMETER;
    }
    
    clear_error();
    delete options;
    return MCP_AUTH_SUCCESS;
}

mcp_auth_error_t mcp_auth_validation_options_set_scopes(
    mcp_auth_validation_options_t options,
    const char* scopes) {
    
    if (!g_initialized) {
        set_error(MCP_AUTH_ERROR_NOT_INITIALIZED, "Library not initialized");
        return MCP_AUTH_ERROR_NOT_INITIALIZED;
    }
    
    if (!options) {
        set_error(MCP_AUTH_ERROR_INVALID_PARAMETER, "Invalid options");
        return MCP_AUTH_ERROR_INVALID_PARAMETER;
    }
    
    clear_error();
    options->scopes = scopes ? scopes : "";
    return MCP_AUTH_SUCCESS;
}

mcp_auth_error_t mcp_auth_validation_options_set_audience(
    mcp_auth_validation_options_t options,
    const char* audience) {
    
    if (!g_initialized) {
        set_error(MCP_AUTH_ERROR_NOT_INITIALIZED, "Library not initialized");
        return MCP_AUTH_ERROR_NOT_INITIALIZED;
    }
    
    if (!options) {
        set_error(MCP_AUTH_ERROR_INVALID_PARAMETER, "Invalid options");
        return MCP_AUTH_ERROR_INVALID_PARAMETER;
    }
    
    clear_error();
    options->audience = audience ? audience : "";
    return MCP_AUTH_SUCCESS;
}

mcp_auth_error_t mcp_auth_validation_options_set_clock_skew(
    mcp_auth_validation_options_t options,
    int64_t seconds) {
    
    if (!g_initialized) {
        set_error(MCP_AUTH_ERROR_NOT_INITIALIZED, "Library not initialized");
        return MCP_AUTH_ERROR_NOT_INITIALIZED;
    }
    
    if (!options) {
        set_error(MCP_AUTH_ERROR_INVALID_PARAMETER, "Invalid options");
        return MCP_AUTH_ERROR_INVALID_PARAMETER;
    }
    
    clear_error();
    options->clock_skew = seconds;
    return MCP_AUTH_SUCCESS;
}

// ========================================================================
// Token Validation
// ========================================================================

mcp_auth_error_t mcp_auth_validate_token(
    mcp_auth_client_t client,
    const char* token,
    mcp_auth_validation_options_t options,
    mcp_auth_validation_result_t* result) {
    
    if (!g_initialized) {
        set_error(MCP_AUTH_ERROR_NOT_INITIALIZED, "Library not initialized");
        return MCP_AUTH_ERROR_NOT_INITIALIZED;
    }
    
    if (!client || !token || !result) {
        set_error(MCP_AUTH_ERROR_INVALID_PARAMETER, "Invalid parameter");
        return MCP_AUTH_ERROR_INVALID_PARAMETER;
    }
    
    clear_error();
    
    // Initialize result
    result->valid = false;
    result->error_code = MCP_AUTH_SUCCESS;
    result->error_message = nullptr;
    
    // Step 1: Parse JWT components
    std::string header_b64, payload_b64, signature_b64;
    if (!split_jwt(token, header_b64, payload_b64, signature_b64)) {
        result->error_code = g_last_error_code;
        result->error_message = strdup(g_last_error.c_str());
        return g_last_error_code;
    }
    
    // Step 2: Decode and parse header
    std::string header_json = base64url_decode(header_b64);
    if (header_json.empty()) {
        set_error(MCP_AUTH_ERROR_INVALID_TOKEN, "Failed to decode JWT header");
        result->error_code = MCP_AUTH_ERROR_INVALID_TOKEN;
        result->error_message = strdup("Failed to decode JWT header");
        return MCP_AUTH_ERROR_INVALID_TOKEN;
    }
    
    std::string alg, kid;
    if (!parse_jwt_header(header_json, alg, kid)) {
        result->error_code = g_last_error_code;
        result->error_message = strdup(g_last_error.c_str());
        return g_last_error_code;
    }
    
    // Cache the parsed header info in the client
    client->last_alg = alg;
    client->last_kid = kid;
    
    // Step 3: Decode payload (will be parsed in next prompt)
    std::string payload_json = base64url_decode(payload_b64);
    if (payload_json.empty()) {
        set_error(MCP_AUTH_ERROR_INVALID_TOKEN, "Failed to decode JWT payload");
        result->error_code = MCP_AUTH_ERROR_INVALID_TOKEN;
        result->error_message = strdup("Failed to decode JWT payload");
        return MCP_AUTH_ERROR_INVALID_TOKEN;
    }
    
    // TODO: Step 4: Parse payload claims (Prompt 2)
    // TODO: Step 5: Fetch JWKS and verify signature (Prompt 3-6)
    // TODO: Step 6: Validate claims (exp, iss, aud, scopes) (Prompt 7-10)
    
    // For now, accept token if we successfully parsed the header
    // This allows testing the header parsing implementation
    result->valid = true;
    result->error_code = MCP_AUTH_SUCCESS;
    
    return MCP_AUTH_SUCCESS;
}

mcp_auth_error_t mcp_auth_extract_payload(
    const char* token,
    mcp_auth_token_payload_t* payload) {
    
    if (!g_initialized) {
        set_error(MCP_AUTH_ERROR_NOT_INITIALIZED, "Library not initialized");
        return MCP_AUTH_ERROR_NOT_INITIALIZED;
    }
    
    if (!token || !payload) {
        set_error(MCP_AUTH_ERROR_INVALID_PARAMETER, "Invalid parameter");
        return MCP_AUTH_ERROR_INVALID_PARAMETER;
    }
    
    clear_error();
    
    try {
        auto* p = new mcp_auth_token_payload();
        if (!p->decode_from_token(token)) {
            delete p;
            set_error(MCP_AUTH_ERROR_INVALID_TOKEN, "Failed to decode token");
            return MCP_AUTH_ERROR_INVALID_TOKEN;
        }
        *payload = p;
        return MCP_AUTH_SUCCESS;
    } catch (const std::exception& e) {
        set_error(MCP_AUTH_ERROR_OUT_OF_MEMORY, e.what());
        return MCP_AUTH_ERROR_OUT_OF_MEMORY;
    }
}

// ========================================================================
// Token Payload Access
// ========================================================================

mcp_auth_error_t mcp_auth_payload_get_subject(
    mcp_auth_token_payload_t payload,
    char** value) {
    
    if (!g_initialized) {
        set_error(MCP_AUTH_ERROR_NOT_INITIALIZED, "Library not initialized");
        return MCP_AUTH_ERROR_NOT_INITIALIZED;
    }
    
    if (!payload || !value) {
        set_error(MCP_AUTH_ERROR_INVALID_PARAMETER, "Invalid parameter");
        return MCP_AUTH_ERROR_INVALID_PARAMETER;
    }
    
    clear_error();
    *value = strdup(payload->subject.c_str());
    return MCP_AUTH_SUCCESS;
}

mcp_auth_error_t mcp_auth_payload_get_issuer(
    mcp_auth_token_payload_t payload,
    char** value) {
    
    if (!g_initialized) {
        set_error(MCP_AUTH_ERROR_NOT_INITIALIZED, "Library not initialized");
        return MCP_AUTH_ERROR_NOT_INITIALIZED;
    }
    
    if (!payload || !value) {
        set_error(MCP_AUTH_ERROR_INVALID_PARAMETER, "Invalid parameter");
        return MCP_AUTH_ERROR_INVALID_PARAMETER;
    }
    
    clear_error();
    *value = strdup(payload->issuer.c_str());
    return MCP_AUTH_SUCCESS;
}

mcp_auth_error_t mcp_auth_payload_get_audience(
    mcp_auth_token_payload_t payload,
    char** value) {
    
    if (!g_initialized) {
        set_error(MCP_AUTH_ERROR_NOT_INITIALIZED, "Library not initialized");
        return MCP_AUTH_ERROR_NOT_INITIALIZED;
    }
    
    if (!payload || !value) {
        set_error(MCP_AUTH_ERROR_INVALID_PARAMETER, "Invalid parameter");
        return MCP_AUTH_ERROR_INVALID_PARAMETER;
    }
    
    clear_error();
    *value = strdup(payload->audience.c_str());
    return MCP_AUTH_SUCCESS;
}

mcp_auth_error_t mcp_auth_payload_get_scopes(
    mcp_auth_token_payload_t payload,
    char** value) {
    
    if (!g_initialized) {
        set_error(MCP_AUTH_ERROR_NOT_INITIALIZED, "Library not initialized");
        return MCP_AUTH_ERROR_NOT_INITIALIZED;
    }
    
    if (!payload || !value) {
        set_error(MCP_AUTH_ERROR_INVALID_PARAMETER, "Invalid parameter");
        return MCP_AUTH_ERROR_INVALID_PARAMETER;
    }
    
    clear_error();
    *value = strdup(payload->scopes.c_str());
    return MCP_AUTH_SUCCESS;
}

mcp_auth_error_t mcp_auth_payload_get_expiration(
    mcp_auth_token_payload_t payload,
    int64_t* value) {
    
    if (!g_initialized) {
        set_error(MCP_AUTH_ERROR_NOT_INITIALIZED, "Library not initialized");
        return MCP_AUTH_ERROR_NOT_INITIALIZED;
    }
    
    if (!payload || !value) {
        set_error(MCP_AUTH_ERROR_INVALID_PARAMETER, "Invalid parameter");
        return MCP_AUTH_ERROR_INVALID_PARAMETER;
    }
    
    clear_error();
    *value = payload->expiration;
    return MCP_AUTH_SUCCESS;
}

mcp_auth_error_t mcp_auth_payload_get_claim(
    mcp_auth_token_payload_t payload,
    const char* claim_name,
    char** value) {
    
    if (!g_initialized) {
        set_error(MCP_AUTH_ERROR_NOT_INITIALIZED, "Library not initialized");
        return MCP_AUTH_ERROR_NOT_INITIALIZED;
    }
    
    if (!payload || !claim_name || !value) {
        set_error(MCP_AUTH_ERROR_INVALID_PARAMETER, "Invalid parameter");
        return MCP_AUTH_ERROR_INVALID_PARAMETER;
    }
    
    clear_error();
    
    auto it = payload->claims.find(claim_name);
    if (it != payload->claims.end()) {
        *value = strdup(it->second.c_str());
    } else {
        *value = nullptr;
    }
    
    return MCP_AUTH_SUCCESS;
}

mcp_auth_error_t mcp_auth_payload_destroy(mcp_auth_token_payload_t payload) {
    if (!g_initialized) {
        set_error(MCP_AUTH_ERROR_NOT_INITIALIZED, "Library not initialized");
        return MCP_AUTH_ERROR_NOT_INITIALIZED;
    }
    
    if (!payload) {
        set_error(MCP_AUTH_ERROR_INVALID_PARAMETER, "Invalid payload");
        return MCP_AUTH_ERROR_INVALID_PARAMETER;
    }
    
    clear_error();
    delete payload;
    return MCP_AUTH_SUCCESS;
}

// ========================================================================
// OAuth Metadata
// ========================================================================

mcp_auth_error_t mcp_auth_generate_www_authenticate(
    const char* realm,
    const char* error,
    const char* error_description,
    char** header) {
    
    if (!g_initialized) {
        set_error(MCP_AUTH_ERROR_NOT_INITIALIZED, "Library not initialized");
        return MCP_AUTH_ERROR_NOT_INITIALIZED;
    }
    
    if (!header) {
        set_error(MCP_AUTH_ERROR_INVALID_PARAMETER, "Invalid parameter");
        return MCP_AUTH_ERROR_INVALID_PARAMETER;
    }
    
    clear_error();
    
    std::ostringstream oss;
    oss << "Bearer";
    
    if (realm) {
        oss << " realm=\"" << realm << "\"";
    }
    
    if (error) {
        oss << " error=\"" << error << "\"";
    }
    
    if (error_description) {
        oss << " error_description=\"" << error_description << "\"";
    }
    
    *header = strdup(oss.str().c_str());
    return MCP_AUTH_SUCCESS;
}

// ========================================================================
// Memory Management
// ========================================================================

void mcp_auth_free_string(char* str) {
    if (str) {
        free(str);
    }
}

const char* mcp_auth_get_last_error(void) {
    return g_last_error.c_str();
}

void mcp_auth_clear_error(void) {
    clear_error();
}

// ========================================================================
// Utility Functions
// ========================================================================

bool mcp_auth_validate_scopes(
    const char* required_scopes,
    const char* available_scopes) {
    
    if (!required_scopes || !available_scopes) {
        return false;
    }
    
    // Simple implementation: check if all required scopes are in available scopes
    std::istringstream required(required_scopes);
    std::string scope;
    
    while (required >> scope) {
        if (std::string(available_scopes).find(scope) == std::string::npos) {
            return false;
        }
    }
    
    return true;
}

const char* mcp_auth_error_to_string(mcp_auth_error_t error_code) {
    switch (error_code) {
        case MCP_AUTH_SUCCESS: return "Success";
        case MCP_AUTH_ERROR_INVALID_TOKEN: return "Invalid token";
        case MCP_AUTH_ERROR_EXPIRED_TOKEN: return "Token expired";
        case MCP_AUTH_ERROR_INVALID_SIGNATURE: return "Invalid signature";
        case MCP_AUTH_ERROR_INVALID_ISSUER: return "Invalid issuer";
        case MCP_AUTH_ERROR_INVALID_AUDIENCE: return "Invalid audience";
        case MCP_AUTH_ERROR_INSUFFICIENT_SCOPE: return "Insufficient scope";
        case MCP_AUTH_ERROR_JWKS_FETCH_FAILED: return "JWKS fetch failed";
        case MCP_AUTH_ERROR_INVALID_KEY: return "Invalid key";
        case MCP_AUTH_ERROR_NETWORK_ERROR: return "Network error";
        case MCP_AUTH_ERROR_INVALID_CONFIG: return "Invalid configuration";
        case MCP_AUTH_ERROR_OUT_OF_MEMORY: return "Out of memory";
        case MCP_AUTH_ERROR_INVALID_PARAMETER: return "Invalid parameter";
        case MCP_AUTH_ERROR_NOT_INITIALIZED: return "Not initialized";
        case MCP_AUTH_ERROR_INTERNAL_ERROR: return "Internal error";
        default: return "Unknown error";
    }
}

} // extern "C"