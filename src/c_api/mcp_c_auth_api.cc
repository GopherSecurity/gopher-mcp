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
// Simple JSON Parser Utilities
// ========================================================================

// Extract a string value from JSON by key
static bool extract_json_string(const std::string& json, const std::string& key, std::string& value) {
    std::string search_key = "\"" + key + "\"";
    size_t key_pos = json.find(search_key);
    if (key_pos == std::string::npos) {
        return false;
    }
    
    size_t colon = json.find(':', key_pos);
    if (colon == std::string::npos) {
        return false;
    }
    
    // Skip whitespace after colon
    size_t val_start = colon + 1;
    while (val_start < json.length() && std::isspace(json[val_start])) {
        val_start++;
    }
    
    if (val_start >= json.length()) {
        return false;
    }
    
    // Check if value is a string
    if (json[val_start] == '"') {
        size_t quote_end = json.find('"', val_start + 1);
        if (quote_end != std::string::npos) {
            value = json.substr(val_start + 1, quote_end - val_start - 1);
            return true;
        }
    }
    
    return false;
}

// Extract a number value from JSON by key
static bool extract_json_number(const std::string& json, const std::string& key, int64_t& value) {
    std::string search_key = "\"" + key + "\"";
    size_t key_pos = json.find(search_key);
    if (key_pos == std::string::npos) {
        return false;
    }
    
    size_t colon = json.find(':', key_pos);
    if (colon == std::string::npos) {
        return false;
    }
    
    // Skip whitespace after colon
    size_t val_start = colon + 1;
    while (val_start < json.length() && std::isspace(json[val_start])) {
        val_start++;
    }
    
    if (val_start >= json.length()) {
        return false;
    }
    
    // Find end of number (comma, space, or })
    size_t val_end = val_start;
    while (val_end < json.length() && 
           (std::isdigit(json[val_end]) || json[val_end] == '-' || json[val_end] == '.')) {
        val_end++;
    }
    
    if (val_end > val_start) {
        std::string num_str = json.substr(val_start, val_end - val_start);
        try {
            value = std::stoll(num_str);
            return true;
        } catch (...) {
            return false;
        }
    }
    
    return false;
}

static bool parse_jwt_header(const std::string& header_json, std::string& alg, std::string& kid) {
    // Use helper functions to extract header fields
    extract_json_string(header_json, "alg", alg);
    extract_json_string(header_json, "kid", kid);  // kid is optional
    
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

// Forward declaration for JWT payload parsing
static bool parse_jwt_payload(const std::string& payload_json, mcp_auth_token_payload* payload);

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
        
        // Parse the payload JSON
        return parse_jwt_payload(payload_json, this);
    }
};

// ========================================================================
// JWT Payload Parsing Implementation
// ========================================================================

static bool parse_jwt_payload(const std::string& payload_json, mcp_auth_token_payload* payload) {
    // Extract standard JWT claims
    extract_json_string(payload_json, "sub", payload->subject);
    extract_json_string(payload_json, "iss", payload->issuer);
    
    // Handle audience - can be string or array
    if (!extract_json_string(payload_json, "aud", payload->audience)) {
        // Try to extract first element if it's an array
        size_t aud_pos = payload_json.find("\"aud\"");
        if (aud_pos != std::string::npos) {
            size_t colon = payload_json.find(':', aud_pos);
            if (colon != std::string::npos) {
                size_t bracket = payload_json.find('[', colon);
                if (bracket != std::string::npos && bracket - colon < 5) {
                    // It's an array, try to get first element
                    size_t quote1 = payload_json.find('"', bracket);
                    if (quote1 != std::string::npos) {
                        size_t quote2 = payload_json.find('"', quote1 + 1);
                        if (quote2 != std::string::npos) {
                            payload->audience = payload_json.substr(quote1 + 1, quote2 - quote1 - 1);
                        }
                    }
                }
            }
        }
    }
    
    // Extract expiration and issued at times
    extract_json_number(payload_json, "exp", payload->expiration);
    int64_t iat = 0;
    if (extract_json_number(payload_json, "iat", iat)) {
        // Store iat in claims for reference
        payload->claims["iat"] = std::to_string(iat);
    }
    
    // Extract not before time if present
    int64_t nbf = 0;
    if (extract_json_number(payload_json, "nbf", nbf)) {
        payload->claims["nbf"] = std::to_string(nbf);
    }
    
    // Extract scope claim (OAuth 2.0 standard)
    extract_json_string(payload_json, "scope", payload->scopes);
    
    // Also try scopes (some implementations use this)
    if (payload->scopes.empty()) {
        extract_json_string(payload_json, "scopes", payload->scopes);
    }
    
    // Extract additional custom claims that might be useful
    std::string email, name, org_id, server_id;
    if (extract_json_string(payload_json, "email", email)) {
        payload->claims["email"] = email;
    }
    if (extract_json_string(payload_json, "name", name)) {
        payload->claims["name"] = name;
    }
    if (extract_json_string(payload_json, "organization_id", org_id)) {
        payload->claims["organization_id"] = org_id;
    }
    if (extract_json_string(payload_json, "server_id", server_id)) {
        payload->claims["server_id"] = server_id;
    }
    
    // Validate required fields
    if (payload->subject.empty()) {
        set_error(MCP_AUTH_ERROR_INVALID_TOKEN, "JWT payload missing 'sub' claim");
        return false;
    }
    
    if (payload->issuer.empty()) {
        set_error(MCP_AUTH_ERROR_INVALID_TOKEN, "JWT payload missing 'iss' claim");
        return false;
    }
    
    if (payload->expiration == 0) {
        set_error(MCP_AUTH_ERROR_INVALID_TOKEN, "JWT payload missing 'exp' claim");
        return false;
    }
    
    return true;
}

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
    
    // Step 3: Decode and parse payload
    std::string payload_json = base64url_decode(payload_b64);
    if (payload_json.empty()) {
        set_error(MCP_AUTH_ERROR_INVALID_TOKEN, "Failed to decode JWT payload");
        result->error_code = MCP_AUTH_ERROR_INVALID_TOKEN;
        result->error_message = strdup("Failed to decode JWT payload");
        return MCP_AUTH_ERROR_INVALID_TOKEN;
    }
    
    // Parse payload claims
    mcp_auth_token_payload payload_data;
    if (!parse_jwt_payload(payload_json, &payload_data)) {
        result->error_code = g_last_error_code;
        result->error_message = strdup(g_last_error.c_str());
        return g_last_error_code;
    }
    
    // TODO: Step 4: Fetch JWKS and verify signature
    // TODO: Step 5: Validate claims (exp, iss, aud, scopes)
    
    // For now, accept token if we successfully parsed header and payload
    // This allows testing the parsing implementation
    fprintf(stderr, "JWT Token parsed successfully:\n");
    fprintf(stderr, "  Algorithm: %s\n", alg.c_str());
    fprintf(stderr, "  Key ID: %s\n", kid.c_str());
    fprintf(stderr, "  Subject: %s\n", payload_data.subject.c_str());
    fprintf(stderr, "  Issuer: %s\n", payload_data.issuer.c_str());
    fprintf(stderr, "  Audience: %s\n", payload_data.audience.c_str());
    fprintf(stderr, "  Scopes: %s\n", payload_data.scopes.c_str());
    fprintf(stderr, "  Expiration: %lld\n", payload_data.expiration);
    
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