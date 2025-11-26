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
#include <openssl/evp.h>
#include <openssl/rsa.h>
#include <openssl/pem.h>
#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/bn.h>
#include <curl/curl.h>

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

// Forward declarations
static bool parse_jwt_payload(const std::string& payload_json, mcp_auth_token_payload* payload);
static bool extract_json_string(const std::string& json, const std::string& key, std::string& value);
static bool extract_json_number(const std::string& json, const std::string& key, int64_t& value);

// JWKS key structure
struct jwks_key {
    std::string kid;
    std::string kty;  // Key type (RSA)
    std::string use;  // Key use (sig)
    std::string alg;  // Algorithm (RS256, RS384, RS512)
    std::string n;    // RSA modulus
    std::string e;    // RSA exponent
    std::string pem;  // Converted PEM format public key
};

// ========================================================================
// HTTP Client for JWKS Fetching  
// ========================================================================

// Callback for libcurl to write response data
static size_t jwks_curl_write_callback(void* ptr, size_t size, size_t nmemb, std::string* data) {
    data->append(static_cast<char*>(ptr), size * nmemb);
    return size * nmemb;
}

// Fetch JWKS from the specified URI
static bool fetch_jwks_json(const std::string& uri, std::string& response) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        set_error(MCP_AUTH_ERROR_NETWORK_ERROR, "Failed to initialize CURL");
        return false;
    }
    
    // Set up CURL options
    curl_easy_setopt(curl, CURLOPT_URL, uri.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, jwks_curl_write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);  // 10 second timeout
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);  // Follow redirects
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 3L);  // Max 3 redirects
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);  // Verify SSL certificate
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);  // Verify hostname
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "MCP-Auth-Client/1.0");
    
    // Add headers
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Accept: application/json");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    
    // Perform the request
    CURLcode res = curl_easy_perform(curl);
    
    // Check for errors
    bool success = false;
    if (res != CURLE_OK) {
        set_error(MCP_AUTH_ERROR_NETWORK_ERROR, 
                 std::string("JWKS fetch failed: ") + curl_easy_strerror(res));
    } else {
        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        if (http_code == 200) {
            success = true;
        } else {
            set_error(MCP_AUTH_ERROR_JWKS_FETCH_FAILED, 
                     "JWKS fetch returned HTTP " + std::to_string(http_code));
        }
    }
    
    // Clean up
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    
    return success;
}

// Convert RSA components (n, e) to PEM format public key
static std::string jwk_to_pem(const std::string& n_b64, const std::string& e_b64) {
    // Decode modulus and exponent from base64url
    std::string n_raw = base64url_decode(n_b64);
    std::string e_raw = base64url_decode(e_b64);
    
    if (n_raw.empty() || e_raw.empty()) {
        return "";
    }
    
    // Create BIGNUM for modulus and exponent
    BIGNUM* bn_n = BN_bin2bn(reinterpret_cast<const unsigned char*>(n_raw.c_str()), 
                             n_raw.length(), nullptr);
    BIGNUM* bn_e = BN_bin2bn(reinterpret_cast<const unsigned char*>(e_raw.c_str()), 
                             e_raw.length(), nullptr);
    
    if (!bn_n || !bn_e) {
        if (bn_n) BN_free(bn_n);
        if (bn_e) BN_free(bn_e);
        return "";
    }
    
    // Create RSA key
    RSA* rsa = RSA_new();
    if (!rsa) {
        BN_free(bn_n);
        BN_free(bn_e);
        return "";
    }
    
    // Set public key components (RSA takes ownership of BIGNUMs)
    if (RSA_set0_key(rsa, bn_n, bn_e, nullptr) != 1) {
        RSA_free(rsa);
        BN_free(bn_n);
        BN_free(bn_e);
        return "";
    }
    
    // Create EVP_PKEY
    EVP_PKEY* pkey = EVP_PKEY_new();
    if (!pkey) {
        RSA_free(rsa);
        return "";
    }
    
    if (EVP_PKEY_assign_RSA(pkey, rsa) != 1) {
        EVP_PKEY_free(pkey);
        RSA_free(rsa);
        return "";
    }
    
    // Convert to PEM format
    BIO* bio = BIO_new(BIO_s_mem());
    if (!bio) {
        EVP_PKEY_free(pkey);
        return "";
    }
    
    if (PEM_write_bio_PUBKEY(bio, pkey) != 1) {
        BIO_free(bio);
        EVP_PKEY_free(pkey);
        return "";
    }
    
    // Get PEM string
    char* pem_data = nullptr;
    long pem_len = BIO_get_mem_data(bio, &pem_data);
    std::string pem(pem_data, pem_len);
    
    // Clean up
    BIO_free(bio);
    EVP_PKEY_free(pkey);
    
    return pem;
}

// Parse JWKS JSON and extract keys
static bool parse_jwks(const std::string& jwks_json, std::vector<jwks_key>& keys) {
    // Find "keys" array in JSON
    size_t keys_pos = jwks_json.find("\"keys\"");
    if (keys_pos == std::string::npos) {
        set_error(MCP_AUTH_ERROR_INVALID_KEY, "JWKS missing 'keys' array");
        return false;
    }
    
    // Find array start
    size_t array_start = jwks_json.find('[', keys_pos);
    if (array_start == std::string::npos) {
        set_error(MCP_AUTH_ERROR_INVALID_KEY, "JWKS 'keys' is not an array");
        return false;
    }
    
    // Parse each key in the array
    size_t pos = array_start + 1;
    while (pos < jwks_json.length()) {
        // Find start of key object
        size_t obj_start = jwks_json.find('{', pos);
        if (obj_start == std::string::npos) break;
        
        // Find end of key object (simple brace matching)
        int brace_count = 1;
        size_t obj_end = obj_start + 1;
        while (obj_end < jwks_json.length() && brace_count > 0) {
            if (jwks_json[obj_end] == '{') brace_count++;
            else if (jwks_json[obj_end] == '}') brace_count--;
            obj_end++;
        }
        
        if (brace_count != 0) break;
        
        // Extract key object JSON
        std::string key_json = jwks_json.substr(obj_start, obj_end - obj_start);
        
        // Parse key fields
        jwks_key key;
        extract_json_string(key_json, "kid", key.kid);
        extract_json_string(key_json, "kty", key.kty);
        extract_json_string(key_json, "use", key.use);
        extract_json_string(key_json, "alg", key.alg);
        extract_json_string(key_json, "n", key.n);
        extract_json_string(key_json, "e", key.e);
        
        // Only add RSA keys used for signing
        if (key.kty == "RSA" && (key.use == "sig" || key.use.empty())) {
            // Convert to PEM format
            key.pem = jwk_to_pem(key.n, key.e);
            if (!key.pem.empty()) {
                keys.push_back(key);
            }
        }
        
        pos = obj_end;
    }
    
    if (keys.empty()) {
        set_error(MCP_AUTH_ERROR_INVALID_KEY, "No valid RSA signing keys found in JWKS");
        return false;
    }
    
    return true;
}

// Forward declaration - function defined after structs
static bool fetch_and_cache_jwks(mcp_auth_client_t client);

// ========================================================================
// JWT Signature Verification
// ========================================================================

static bool verify_rsa_signature(
    const std::string& signing_input,
    const std::string& signature,
    const std::string& public_key_pem,
    const std::string& algorithm) {
    
    // Create BIO for public key
    BIO* key_bio = BIO_new_mem_buf(public_key_pem.c_str(), -1);
    if (!key_bio) {
        set_error(MCP_AUTH_ERROR_INVALID_KEY, "Failed to create BIO for public key");
        return false;
    }
    
    // Read public key
    EVP_PKEY* pkey = PEM_read_bio_PUBKEY(key_bio, nullptr, nullptr, nullptr);
    BIO_free(key_bio);
    
    if (!pkey) {
        set_error(MCP_AUTH_ERROR_INVALID_KEY, "Failed to parse public key");
        return false;
    }
    
    // Create verification context
    EVP_MD_CTX* md_ctx = EVP_MD_CTX_new();
    if (!md_ctx) {
        EVP_PKEY_free(pkey);
        set_error(MCP_AUTH_ERROR_INTERNAL_ERROR, "Failed to create verification context");
        return false;
    }
    
    // Select hash algorithm based on JWT algorithm
    const EVP_MD* md = nullptr;
    if (algorithm == "RS256") {
        md = EVP_sha256();
    } else if (algorithm == "RS384") {
        md = EVP_sha384();
    } else if (algorithm == "RS512") {
        md = EVP_sha512();
    } else {
        EVP_MD_CTX_free(md_ctx);
        EVP_PKEY_free(pkey);
        set_error(MCP_AUTH_ERROR_INVALID_TOKEN, "Unsupported algorithm: " + algorithm);
        return false;
    }
    
    // Initialize verification
    if (EVP_DigestVerifyInit(md_ctx, nullptr, md, nullptr, pkey) != 1) {
        EVP_MD_CTX_free(md_ctx);
        EVP_PKEY_free(pkey);
        set_error(MCP_AUTH_ERROR_INTERNAL_ERROR, "Failed to initialize signature verification");
        return false;
    }
    
    // Update with signing input
    if (EVP_DigestVerifyUpdate(md_ctx, signing_input.c_str(), signing_input.length()) != 1) {
        EVP_MD_CTX_free(md_ctx);
        EVP_PKEY_free(pkey);
        set_error(MCP_AUTH_ERROR_INTERNAL_ERROR, "Failed to update signature verification");
        return false;
    }
    
    // Verify signature
    int verify_result = EVP_DigestVerifyFinal(md_ctx, 
        reinterpret_cast<const unsigned char*>(signature.c_str()), 
        signature.length());
    
    // Clean up
    EVP_MD_CTX_free(md_ctx);
    EVP_PKEY_free(pkey);
    
    if (verify_result == 1) {
        return true;
    } else if (verify_result == 0) {
        set_error(MCP_AUTH_ERROR_INVALID_SIGNATURE, "JWT signature verification failed");
        return false;
    } else {
        // Get OpenSSL error
        char err_buf[256];
        ERR_error_string_n(ERR_get_error(), err_buf, sizeof(err_buf));
        set_error(MCP_AUTH_ERROR_INTERNAL_ERROR, std::string("Signature verification error: ") + err_buf);
        return false;
    }
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
    
    // JWKS cache
    std::vector<jwks_key> cached_keys;
    std::chrono::steady_clock::time_point cache_timestamp;
    std::mutex cache_mutex;
    
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
// JWKS Fetching Implementation (requires struct definitions)
// ========================================================================

// Check if JWKS cache is still valid
static bool is_cache_valid(mcp_auth_client_t client) {
    std::lock_guard<std::mutex> lock(client->cache_mutex);
    
    // Check if we have cached keys
    if (client->cached_keys.empty()) {
        return false;
    }
    
    // Check if cache has expired
    auto now = std::chrono::steady_clock::now();
    auto age = std::chrono::duration_cast<std::chrono::seconds>(now - client->cache_timestamp).count();
    
    return age < client->cache_duration;
}

// Get cached JWKS keys with automatic refresh
static bool get_jwks_keys(mcp_auth_client_t client, std::vector<jwks_key>& keys) {
    // Check if cache is valid
    if (is_cache_valid(client)) {
        std::lock_guard<std::mutex> lock(client->cache_mutex);
        keys = client->cached_keys;
        return true;
    }
    
    // Cache is invalid or expired, fetch new keys
    return fetch_and_cache_jwks(client) && get_jwks_keys(client, keys);
}

// Invalidate cache (for when validation fails with unknown kid)
static void invalidate_cache(mcp_auth_client_t client) {
    std::lock_guard<std::mutex> lock(client->cache_mutex);
    client->cached_keys.clear();
    client->cache_timestamp = std::chrono::steady_clock::time_point();
}

// Fetch and cache JWKS keys
static bool fetch_and_cache_jwks(mcp_auth_client_t client) {
    std::string jwks_json;
    if (!fetch_jwks_json(client->jwks_uri, jwks_json)) {
        return false;
    }
    
    std::vector<jwks_key> keys;
    if (!parse_jwks(jwks_json, keys)) {
        return false;
    }
    
    // Update cache
    std::lock_guard<std::mutex> lock(client->cache_mutex);
    client->cached_keys = std::move(keys);
    client->cache_timestamp = std::chrono::steady_clock::now();
    
    fprintf(stderr, "JWKS cache updated with %zu keys\n", client->cached_keys.size());
    for (const auto& key : client->cached_keys) {
        fprintf(stderr, "  Key: kid=%s, alg=%s\n", key.kid.c_str(), key.alg.c_str());
    }
    
    return true;
}

// Find key by kid from cached JWKS
static bool find_key_by_kid(mcp_auth_client_t client, const std::string& kid, jwks_key& key) {
    std::vector<jwks_key> keys;
    if (!get_jwks_keys(client, keys)) {
        return false;
    }
    
    // Look for exact kid match
    for (const auto& k : keys) {
        if (k.kid == kid) {
            key = k;
            return true;
        }
    }
    
    // If no match found and auto-refresh is enabled, try fetching fresh keys
    if (client->auto_refresh) {
        fprintf(stderr, "Key with kid '%s' not found, refreshing JWKS cache\n", kid.c_str());
        invalidate_cache(client);
        
        if (get_jwks_keys(client, keys)) {
            // Try again with fresh keys
            for (const auto& k : keys) {
                if (k.kid == kid) {
                    key = k;
                    return true;
                }
            }
        }
    }
    
    set_error(MCP_AUTH_ERROR_INVALID_KEY, "No key found with kid: " + kid);
    return false;
}

// Try all available keys when no kid is specified
static bool try_all_keys(mcp_auth_client_t client, 
                         const std::string& signing_input,
                         const std::string& signature,
                         const std::string& algorithm) {
    std::vector<jwks_key> keys;
    if (!get_jwks_keys(client, keys)) {
        return false;
    }
    
    // Try each key that matches the algorithm
    for (const auto& key : keys) {
        // Skip if algorithm doesn't match (if specified in JWK)
        if (!key.alg.empty() && key.alg != algorithm) {
            continue;
        }
        
        // Try to verify with this key
        if (verify_rsa_signature(signing_input, signature, key.pem, algorithm)) {
            fprintf(stderr, "Successfully verified with key kid=%s\n", key.kid.c_str());
            return true;
        }
    }
    
    // If auto-refresh enabled and no key worked, try refreshing once
    if (client->auto_refresh) {
        fprintf(stderr, "No key could verify signature, refreshing JWKS cache\n");
        invalidate_cache(client);
        
        if (get_jwks_keys(client, keys)) {
            for (const auto& key : keys) {
                if (!key.alg.empty() && key.alg != algorithm) {
                    continue;
                }
                
                if (verify_rsa_signature(signing_input, signature, key.pem, algorithm)) {
                    fprintf(stderr, "Successfully verified with key kid=%s after refresh\n", key.kid.c_str());
                    return true;
                }
            }
        }
    }
    
    set_error(MCP_AUTH_ERROR_INVALID_SIGNATURE, "No key could verify the JWT signature");
    return false;
}

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
    
    // Step 4: Verify signature
    // Create the signing input (header.payload)
    std::string signing_input = header_b64 + "." + payload_b64;
    
    // Decode signature from base64url
    std::string signature_raw = base64url_decode(signature_b64);
    if (signature_raw.empty()) {
        set_error(MCP_AUTH_ERROR_INVALID_TOKEN, "Failed to decode JWT signature");
        result->error_code = MCP_AUTH_ERROR_INVALID_TOKEN;
        result->error_message = strdup("Failed to decode JWT signature");
        return MCP_AUTH_ERROR_INVALID_TOKEN;
    }
    
    // Verify signature using JWKS
    bool signature_valid = false;
    if (!kid.empty()) {
        // Use specific key by kid
        jwks_key key;
        if (find_key_by_kid(client, kid, key)) {
            signature_valid = verify_rsa_signature(signing_input, signature_raw, key.pem, alg);
        }
    } else {
        // No kid specified, try all keys
        signature_valid = try_all_keys(client, signing_input, signature_raw, alg);
    }
    
    if (!signature_valid) {
        result->error_code = g_last_error_code;
        result->error_message = strdup(g_last_error.c_str());
        return g_last_error_code;
    }
    
    // Step 5: Validate claims
    
    // Check expiration with clock skew
    int64_t now = std::chrono::system_clock::now().time_since_epoch().count() / 1000000000; // Convert to seconds
    int64_t clock_skew = options ? options->clock_skew : 60; // Default 60 seconds
    
    if (payload_data.expiration > 0) {
        if (now > payload_data.expiration + clock_skew) {
            set_error(MCP_AUTH_ERROR_EXPIRED_TOKEN, "JWT has expired");
            result->error_code = MCP_AUTH_ERROR_EXPIRED_TOKEN;
            result->error_message = strdup("JWT has expired");
            return MCP_AUTH_ERROR_EXPIRED_TOKEN;
        }
    }
    
    // Check not-before time if present
    auto nbf_it = payload_data.claims.find("nbf");
    if (nbf_it != payload_data.claims.end()) {
        try {
            int64_t nbf = std::stoll(nbf_it->second);
            if (now < nbf - clock_skew) {
                set_error(MCP_AUTH_ERROR_INVALID_TOKEN, "JWT not yet valid (nbf)");
                result->error_code = MCP_AUTH_ERROR_INVALID_TOKEN;
                result->error_message = strdup("JWT not yet valid (nbf)");
                return MCP_AUTH_ERROR_INVALID_TOKEN;
            }
        } catch (...) {
            // Invalid nbf value, ignore
        }
    }
    
    // Validate issuer
    if (!client->issuer.empty()) {
        // Check exact match first
        if (payload_data.issuer != client->issuer) {
            // Try with/without trailing slash for compatibility
            std::string iss1 = payload_data.issuer;
            std::string iss2 = client->issuer;
            
            // Remove trailing slash from both for comparison
            if (!iss1.empty() && iss1.back() == '/') iss1.pop_back();
            if (!iss2.empty() && iss2.back() == '/') iss2.pop_back();
            
            if (iss1 != iss2) {
                set_error(MCP_AUTH_ERROR_INVALID_ISSUER, 
                         "Invalid issuer. Expected: " + client->issuer + ", Got: " + payload_data.issuer);
                result->error_code = MCP_AUTH_ERROR_INVALID_ISSUER;
                result->error_message = strdup(g_last_error.c_str());
                return MCP_AUTH_ERROR_INVALID_ISSUER;
            }
        }
    }
    
    // Validate audience if specified
    if (options && !options->audience.empty()) {
        if (payload_data.audience.empty()) {
            set_error(MCP_AUTH_ERROR_INVALID_AUDIENCE, "JWT has no audience claim");
            result->error_code = MCP_AUTH_ERROR_INVALID_AUDIENCE;
            result->error_message = strdup("JWT has no audience claim");
            return MCP_AUTH_ERROR_INVALID_AUDIENCE;
        }
        
        // Check if the required audience matches the token audience
        // Token audience can be a single string or array (we handle single string from parsing)
        if (payload_data.audience != options->audience) {
            set_error(MCP_AUTH_ERROR_INVALID_AUDIENCE, 
                     "Invalid audience. Expected: " + options->audience + ", Got: " + payload_data.audience);
            result->error_code = MCP_AUTH_ERROR_INVALID_AUDIENCE;
            result->error_message = strdup(g_last_error.c_str());
            return MCP_AUTH_ERROR_INVALID_AUDIENCE;
        }
    }
    
    // TODO: Validate scopes (Task 10)
    
    // Token is valid
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