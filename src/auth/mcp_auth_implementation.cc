/**
 * @file mcp_c_auth_api.cc
 * @brief C API implementation for authentication module
 * 
 * Provides JWT validation and OAuth support matching gopher-auth-sdk-nodejs functionality
 */

#ifdef USE_CPP11_COMPAT
#include "cpp11_compat.h"
#endif

#include "mcp/auth/auth_c_api.h"
#include <string>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <sstream>
#include <chrono>
#include <cstring>
#include <mutex>
#ifndef USE_CPP11_COMPAT
#include <shared_mutex>
#endif
#include <algorithm>
#include <random>
#include <thread>
#include <atomic>
#include <openssl/evp.h>
#include <openssl/rsa.h>
#include <openssl/pem.h>
#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/bn.h>
#include <curl/curl.h>

// Thread-local error storage with context (per-thread isolation)
static thread_local std::string g_last_error;
static thread_local std::string g_last_error_context;  // Additional context information
static thread_local mcp_auth_error_t g_last_error_code = MCP_AUTH_SUCCESS;

// Thread-local error buffer for safe string returns
static thread_local char g_error_buffer[4096];

// Global initialization state with atomic flag
static std::atomic<bool> g_initialized{false};
static std::mutex g_init_mutex;

// Performance optimization flags
static bool g_use_crypto_cache = true;  // Use optimized crypto caching
static std::atomic<size_t> g_verification_count{0};  // Track verification count

// Set error state
static void set_error(mcp_auth_error_t code, const std::string& message) {
    g_last_error_code = code;
    g_last_error = message;
    g_last_error_context.clear();
}

// Set error with additional context
static void set_error_with_context(mcp_auth_error_t code, const std::string& message, 
                                   const std::string& context) {
    g_last_error_code = code;
    g_last_error = message;
    g_last_error_context = context;
}

// Clear error state
static void clear_error() {
    g_last_error_code = MCP_AUTH_SUCCESS;
    g_last_error.clear();
    g_last_error_context.clear();
}

// ========================================================================
// Configuration Validation Utilities
// ========================================================================

// Validate URL format
static bool is_valid_url(const std::string& url) {
    if (url.empty()) {
        return false;
    }
    
    // Check for protocol
    if (url.find("http://") != 0 && url.find("https://") != 0) {
        return false;
    }
    
    // Check for minimum URL structure (protocol://host)
    size_t protocol_end = url.find("://");
    if (protocol_end == std::string::npos) {
        return false;
    }
    
    // Check there's something after protocol
    if (url.length() <= protocol_end + 3) {
        return false;
    }
    
    // Check for valid host part
    std::string host_part = url.substr(protocol_end + 3);
    if (host_part.empty() || host_part[0] == '/' || host_part[0] == ':') {
        return false;
    }
    
    return true;
}

// Normalize URL (remove trailing slash)
static std::string normalize_url(const std::string& url) {
    std::string normalized = url;
    while (!normalized.empty() && normalized.back() == '/') {
        normalized.pop_back();
    }
    return normalized;
}

// ========================================================================
// Memory Management Utilities
// ========================================================================

// Safe string duplication with error handling
static char* safe_strdup(const std::string& str) {
    if (str.empty()) {
        return nullptr;
    }
    
    size_t len = str.length() + 1;
    char* result = static_cast<char*>(malloc(len));
    if (!result) {
        set_error(MCP_AUTH_ERROR_OUT_OF_MEMORY, "Failed to allocate memory for string");
        return nullptr;
    }
    
    memcpy(result, str.c_str(), len);
    return result;
}

// Safe memory allocation with error handling
static void* safe_malloc(size_t size) {
    if (size == 0) {
        return nullptr;
    }
    
    void* result = malloc(size);
    if (!result) {
        set_error(MCP_AUTH_ERROR_OUT_OF_MEMORY, 
                 "Failed to allocate " + std::to_string(size) + " bytes");
        return nullptr;
    }
    
    // Zero-initialize for safety
    memset(result, 0, size);
    return result;
}

// Safe memory reallocation
static void* safe_realloc(void* ptr, size_t new_size) {
    if (new_size == 0) {
        free(ptr);
        return nullptr;
    }
    
    void* result = realloc(ptr, new_size);
    if (!result && new_size > 0) {
        set_error(MCP_AUTH_ERROR_OUT_OF_MEMORY, 
                 "Failed to reallocate to " + std::to_string(new_size) + " bytes");
        // Original pointer is still valid on realloc failure
        return nullptr;
    }
    
    return result;
}

// Secure memory cleanup for sensitive data
static void secure_free(void* ptr, size_t size) {
    if (ptr) {
        // Overwrite memory before freeing (for sensitive data)
        if (size > 0) {
            volatile unsigned char* p = static_cast<volatile unsigned char*>(ptr);
            while (size--) {
                *p++ = 0;
            }
        }
        free(ptr);
    }
}

// RAII wrapper for C memory
template<typename T>
class c_memory_guard {
private:
    T* ptr;
    size_t size;
    bool secure;
    
public:
    c_memory_guard(T* p = nullptr, size_t s = 0, bool sec = false) 
        : ptr(p), size(s), secure(sec) {}
    
    ~c_memory_guard() {
        if (ptr) {
            if (secure && size > 0) {
                secure_free(ptr, size);
            } else {
                free(ptr);
            }
        }
    }
    
    // Disable copy
    c_memory_guard(const c_memory_guard&) = delete;
    c_memory_guard& operator=(const c_memory_guard&) = delete;
    
    // Enable move
    c_memory_guard(c_memory_guard&& other) noexcept 
        : ptr(other.ptr), size(other.size), secure(other.secure) {
        other.ptr = nullptr;
        other.size = 0;
    }
    
    c_memory_guard& operator=(c_memory_guard&& other) noexcept {
        if (this != &other) {
            if (ptr) {
                if (secure && size > 0) {
                    secure_free(ptr, size);
                } else {
                    free(ptr);
                }
            }
            ptr = other.ptr;
            size = other.size;
            secure = other.secure;
            other.ptr = nullptr;
            other.size = 0;
        }
        return *this;
    }
    
    T* get() { return ptr; }
    T* release() { 
        T* p = ptr; 
        ptr = nullptr; 
        size = 0;
        return p; 
    }
    void reset(T* p = nullptr, size_t s = 0) {
        if (ptr) {
            if (secure && size > 0) {
                secure_free(ptr, size);
            } else {
                free(ptr);
            }
        }
        ptr = p;
        size = s;
    }
};

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

// HTTP client configuration
struct http_client_config {
    long timeout = 10L;           // Request timeout in seconds
    long connect_timeout = 5L;    // Connection timeout in seconds
    bool follow_redirects = true;
    long max_redirects = 3L;
    bool verify_ssl = true;
    std::string user_agent = "MCP-Auth-Client/1.0.0";
};

// Perform HTTP GET request with robust error handling
static bool http_get(const std::string& url, 
                     std::string& response, 
                     const http_client_config& config = http_client_config()) {
    // Initialize CURL
    CURL* curl = curl_easy_init();
    if (!curl) {
        set_error(MCP_AUTH_ERROR_NETWORK_ERROR, "Failed to initialize HTTP client");
        return false;
    }
    
    // Use RAII for cleanup
    struct curl_cleanup {
        CURL* handle;
        curl_slist* headers;
        ~curl_cleanup() {
            if (headers) curl_slist_free_all(headers);
            if (handle) curl_easy_cleanup(handle);
        }
    } cleanup{curl, nullptr};
    
    // Set basic options
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, jwks_curl_write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    
    // Set timeouts
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, config.timeout);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, config.connect_timeout);
    
    // Set redirect handling
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, config.follow_redirects ? 1L : 0L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, config.max_redirects);
    
    // Set SSL/TLS options
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, config.verify_ssl ? 1L : 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, config.verify_ssl ? 2L : 0L);
    
    // Set protocol to HTTP and HTTPS (allow both for local development)
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS, CURLPROTO_HTTP | CURLPROTO_HTTPS);
    curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS, CURLPROTO_HTTP | CURLPROTO_HTTPS);
    
    // Set user agent
    curl_easy_setopt(curl, CURLOPT_USERAGENT, config.user_agent.c_str());
    
    // Enable TCP keep-alive
    curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);
    curl_easy_setopt(curl, CURLOPT_TCP_KEEPIDLE, 120L);
    curl_easy_setopt(curl, CURLOPT_TCP_KEEPINTVL, 60L);
    
    // Set headers
    cleanup.headers = curl_slist_append(cleanup.headers, "Accept: application/json");
    cleanup.headers = curl_slist_append(cleanup.headers, "Cache-Control: no-cache");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, cleanup.headers);
    
    // Enable verbose output for debugging (only in debug builds)
#ifdef DEBUG
    curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
#endif
    
    // Perform the request
    CURLcode res = curl_easy_perform(curl);
    
    // Handle the result
    if (res != CURLE_OK) {
        // Detailed error message based on error code
        std::string error_msg = "HTTP request failed: ";
        error_msg += curl_easy_strerror(res);
        
        // Add more context for common errors
        switch (res) {
            case CURLE_OPERATION_TIMEDOUT:
                error_msg += " (timeout after " + std::to_string(config.timeout) + " seconds)";
                break;
            case CURLE_SSL_CONNECT_ERROR:
            case CURLE_SSL_CERTPROBLEM:
            case CURLE_SSL_CIPHER:
            case CURLE_SSL_CACERT:
                error_msg += " (SSL/TLS error - check certificates)";
                break;
            case CURLE_COULDNT_RESOLVE_HOST:
                error_msg += " (DNS resolution failed)";
                break;
            case CURLE_COULDNT_CONNECT:
                error_msg += " (connection refused or network unreachable)";
                break;
        }
        
        set_error(MCP_AUTH_ERROR_NETWORK_ERROR, error_msg);
        return false;
    }
    
    // Check HTTP response code
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    
    // Get the final URL after redirects
    char* final_url = nullptr;
    curl_easy_getinfo(curl, CURLINFO_EFFECTIVE_URL, &final_url);
    
    if (http_code >= 200 && http_code < 300) {
        // Success
        // HTTP GET successful
        return true;
    } else {
        // HTTP error
        std::string error_msg = "HTTP request returned status " + std::to_string(http_code);
        
        // Add specific messages for common HTTP errors
        switch (http_code) {
            case 401:
                error_msg += " (Unauthorized - check authentication)";
                break;
            case 403:
                error_msg += " (Forbidden - access denied)";
                break;
            case 404:
                error_msg += " (Not Found - check URL)";
                break;
            case 500:
            case 502:
            case 503:
            case 504:
                error_msg += " (Server error - may be temporary)";
                break;
        }
        
        if (final_url && std::string(final_url) != url) {
            error_msg += " after redirect to " + std::string(final_url);
        }
        
        set_error(http_code >= 500 ? MCP_AUTH_ERROR_NETWORK_ERROR : MCP_AUTH_ERROR_JWKS_FETCH_FAILED, error_msg);
        return false;
    }
}

// HTTP retry configuration
struct http_retry_config {
    int max_retries = 3;
    int initial_delay_ms = 1000;  // 1 second
    int max_delay_ms = 16000;      // 16 seconds
    double backoff_multiplier = 2.0;
    int jitter_ms = 500;           // Random jitter up to 500ms
};

// Check if HTTP status code is retryable
static bool is_retryable_status(long http_code) {
    // Retry on 5xx server errors and specific 4xx errors
    return (http_code >= 500 && http_code < 600) || 
           http_code == 408 ||  // Request Timeout
           http_code == 429 ||  // Too Many Requests
           http_code == 0;      // Network error (no HTTP response)
}

// Check if CURL error is retryable
static bool is_retryable_curl_error(CURLcode code) {
    switch (code) {
        case CURLE_OPERATION_TIMEDOUT:
        case CURLE_COULDNT_CONNECT:
        case CURLE_COULDNT_RESOLVE_HOST:
        case CURLE_COULDNT_RESOLVE_PROXY:
        case CURLE_GOT_NOTHING:
        case CURLE_SEND_ERROR:
        case CURLE_RECV_ERROR:
        case CURLE_HTTP2:
        case CURLE_HTTP2_STREAM:
            return true;
        default:
            return false;
    }
}

// Add random jitter to delay
static int add_jitter(int delay_ms, int max_jitter_ms) {
    if (max_jitter_ms <= 0) {
        return delay_ms;
    }
    
    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, max_jitter_ms);
    
    return delay_ms + dis(gen);
}

// Perform HTTP GET request with retry logic
static bool http_get_with_retry(const std::string& url,
                                std::string& response,
                                const http_client_config& config = http_client_config(),
                                const http_retry_config& retry = http_retry_config()) {
    
    int delay_ms = retry.initial_delay_ms;
    std::string last_error;
    
    for (int attempt = 0; attempt <= retry.max_retries; attempt++) {
        // Clear response for each attempt
        response.clear();
        
        // Store original error handler state
        std::string saved_error = g_last_error;
        mcp_auth_error_t saved_code = g_last_error_code;
        
        // Try the request
        bool success = http_get(url, response, config);
        
        if (success) {
            if (attempt > 0) {
                fprintf(stderr, "HTTP request succeeded after %d retries\n", attempt);
            }
            return true;
        }
        
        // Check if error is retryable
        bool should_retry = false;
        
        // Get HTTP status code if available
        long http_code = 0;
        if (!response.empty()) {
            // Response might contain error details
            // For now, check the error code
            if (saved_code == MCP_AUTH_ERROR_NETWORK_ERROR) {
                should_retry = true;
            } else if (saved_code == MCP_AUTH_ERROR_JWKS_FETCH_FAILED) {
                // Parse HTTP code from error message if possible
                size_t pos = saved_error.find("status ");
                if (pos != std::string::npos) {
                    try {
                        http_code = std::stol(saved_error.substr(pos + 7, 3));
                        should_retry = is_retryable_status(http_code);
                    } catch (...) {
                        // Couldn't parse status, don't retry
                    }
                }
            }
        } else {
            // Network error, likely retryable
            should_retry = (saved_code == MCP_AUTH_ERROR_NETWORK_ERROR);
        }
        
        // Save the last error
        last_error = saved_error;
        
        // Check if we should retry
        if (!should_retry || attempt >= retry.max_retries) {
            // Restore error state and fail
            set_error(saved_code, last_error);
            if (attempt > 0) {
                g_last_error += " (failed after " + std::to_string(attempt + 1) + " attempts)";
            }
            return false;
        }
        
        // Calculate delay with exponential backoff and jitter
        int actual_delay = add_jitter(delay_ms, retry.jitter_ms);
        
        // HTTP request failed, retrying...
        
        // Sleep before retry
        std::this_thread::sleep_for(std::chrono::milliseconds(actual_delay));
        
        // Increase delay for next attempt (exponential backoff)
        delay_ms = static_cast<int>(delay_ms * retry.backoff_multiplier);
        if (delay_ms > retry.max_delay_ms) {
            delay_ms = retry.max_delay_ms;
        }
    }
    
    // Should never reach here, but just in case
    set_error(MCP_AUTH_ERROR_NETWORK_ERROR, last_error + " (retry logic error)");
    return false;
}

// Fetch JWKS from the specified URI with retry
static bool fetch_jwks_json(const std::string& uri, std::string& response, int64_t timeout_seconds = 10) {
    http_client_config config;
    config.timeout = timeout_seconds;
    config.connect_timeout = (timeout_seconds / 2 < 5) ? timeout_seconds / 2 : 5;
    config.verify_ssl = true;
    
    http_retry_config retry;
    retry.max_retries = 3;
    retry.initial_delay_ms = 1000;
    retry.jitter_ms = 500;
    
    return http_get_with_retry(uri, response, config, retry);
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
    int64_t cache_duration = 3600;   // Default: 1 hour
    bool auto_refresh = true;        // Default: enabled
    int64_t request_timeout = 10;    // Default: 10 seconds
    
    // OAuth client credentials for token exchange
    std::string client_id;
    std::string client_secret;
    std::string token_endpoint;      // Token exchange endpoint
    std::string exchange_idps;       // Comma-separated IDP aliases
    
    // Cached JWT header info for last validated token
    std::string last_alg;
    std::string last_kid;
    
    // Error context for debugging
    std::string last_error_context;
    mcp_auth_error_t last_error_code = MCP_AUTH_SUCCESS;
    
    // JWKS cache with read-write lock for better concurrency
    std::vector<jwks_key> cached_keys;
    std::chrono::steady_clock::time_point cache_timestamp;
    mutable std::shared_mutex cache_mutex;  // mutable for const methods
    
    // Auto-refresh state
    std::atomic<bool> refresh_in_progress{false};
    
    mcp_auth_client(const char* uri, const char* iss) 
        : jwks_uri(uri ? normalize_url(uri) : "")
        , issuer(iss ? normalize_url(iss) : "") {
        // Apply configuration defaults
        cache_duration = 3600;    // 1 hour default
        auto_refresh = true;      // Auto-refresh enabled by default
        request_timeout = 10;     // 10 seconds default
    }
};

struct mcp_auth_validation_options {
    std::string scopes;
    std::string audience;
    int64_t clock_skew = 60;  // Default: 60 seconds
    
    // Constructor with defaults
    mcp_auth_validation_options() 
        : clock_skew(60) {}  // Ensure default is set
    
    // Helper to check if options require scope validation
    bool requires_scope_validation() const {
        return !scopes.empty();
    }
    
    // Helper to check if options require audience validation
    bool requires_audience_validation() const {
        return !audience.empty();
    }
};

// Store error in client structure with context (thread-safe)
static void set_client_error(mcp_auth_client_t client, mcp_auth_error_t code,
                            const std::string& message, const std::string& context = "") {
    if (client) {
        // Client error is not thread-local, so we just store it
        // Each client instance maintains its own error state
        client->last_error_code = code;
        client->last_error_context = context.empty() ? message : message + " (" + context + ")";
    }
    // Also set thread-local error for immediate retrieval
    set_error_with_context(code, message, context);
}

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
static bool is_cache_valid(const mcp_auth_client_t client) {
    // Use shared lock for read-only access
    std::shared_lock<std::shared_mutex> lock(client->cache_mutex);
    
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
        // Use shared lock for reading cached data
        std::shared_lock<std::shared_mutex> lock(client->cache_mutex);
        keys = client->cached_keys;
        return true;
    }
    
    // Prevent multiple simultaneous refreshes using atomic flag
    bool expected = false;
    if (client->refresh_in_progress.compare_exchange_strong(expected, true)) {
        // This thread won the race to refresh
        bool success = fetch_and_cache_jwks(client);
        client->refresh_in_progress = false;
        
        if (success) {
            // Read the newly cached keys
            std::shared_lock<std::shared_mutex> lock(client->cache_mutex);
            keys = client->cached_keys;
            return true;
        }
        return false;
    } else {
        // Another thread is refreshing, wait a bit and retry
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        return get_jwks_keys(client, keys);
    }
}

// Invalidate cache (for when validation fails with unknown kid)
static void invalidate_cache(mcp_auth_client_t client) {
    // Use unique lock for write access
    std::unique_lock<std::shared_mutex> lock(client->cache_mutex);
    client->cached_keys.clear();
    client->cache_timestamp = std::chrono::steady_clock::time_point();
}

// Fetch and cache JWKS keys (thread-safe)
static bool fetch_and_cache_jwks(mcp_auth_client_t client) {
    std::string jwks_json;
    if (!fetch_jwks_json(client->jwks_uri, jwks_json, client->request_timeout)) {
        set_client_error(client, MCP_AUTH_ERROR_JWKS_FETCH_FAILED,
                        "Failed to fetch JWKS",
                        "URI: " + client->jwks_uri + ", Error: " + g_last_error);
        return false;
    }
    
    std::vector<jwks_key> keys;
    if (!parse_jwks(jwks_json, keys)) {
        set_client_error(client, MCP_AUTH_ERROR_JWKS_FETCH_FAILED,
                        "Failed to parse JWKS response",
                        "URI: " + client->jwks_uri);
        return false;
    }
    
    // Update cache atomically with exclusive lock
    {
        std::unique_lock<std::shared_mutex> lock(client->cache_mutex);
        // Use swap for atomic update
        client->cached_keys.swap(keys);
        client->cache_timestamp = std::chrono::steady_clock::now();
    }
    
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
// Validation Result Cleanup
// ========================================================================

// Forward declaration for cleanup
void mcp_auth_free_string(char* str);

// Clean up validation result error message
static void cleanup_validation_result(mcp_auth_validation_result_t* result) {
    if (result && result->error_message) {
        // The error_message was allocated with safe_strdup
        mcp_auth_free_string(const_cast<char*>(result->error_message));
        result->error_message = nullptr;
    }
}

// ========================================================================
// Library Initialization
// ========================================================================

extern "C" {

mcp_auth_error_t mcp_auth_init(void) {
    std::lock_guard<std::mutex> lock(g_init_mutex);
    
    // Check atomic flag with memory ordering
    if (g_initialized.load(std::memory_order_acquire)) {
        return MCP_AUTH_SUCCESS;
    }
    
    // Initialize libcurl globally (thread-safe initialization)
    CURLcode res = curl_global_init(CURL_GLOBAL_ALL);
    if (res != CURLE_OK) {
        set_error(MCP_AUTH_ERROR_INTERNAL_ERROR, 
                 "Failed to initialize HTTP client library: " + std::string(curl_easy_strerror(res)));
        return MCP_AUTH_ERROR_INTERNAL_ERROR;
    }
    
    clear_error();
    
    // Set atomic flag with memory ordering
    g_initialized.store(true, std::memory_order_release);
    return MCP_AUTH_SUCCESS;
}

mcp_auth_error_t mcp_auth_shutdown(void) {
    std::lock_guard<std::mutex> lock(g_init_mutex);
    if (!g_initialized.load()) {
        set_error(MCP_AUTH_ERROR_NOT_INITIALIZED, "Library not initialized");
        return MCP_AUTH_ERROR_NOT_INITIALIZED;
    }
    
    // Clean up any global libcurl state
    curl_global_cleanup();
    
    // Clear any cached errors
    clear_error();
    
    // Clear atomic flag with memory ordering
    g_initialized.store(false, std::memory_order_release);
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
    
    if (!g_initialized.load()) {
        set_error(MCP_AUTH_ERROR_NOT_INITIALIZED, "Library not initialized");
        return MCP_AUTH_ERROR_NOT_INITIALIZED;
    }
    
    if (!client || !jwks_uri || !issuer) {
        std::string context = "Missing: ";
        if (!client) context += "client ptr, ";
        if (!jwks_uri) context += "jwks_uri, ";
        if (!issuer) context += "issuer";
        set_error_with_context(MCP_AUTH_ERROR_INVALID_PARAMETER, 
                              "Invalid parameters", context);
        return MCP_AUTH_ERROR_INVALID_PARAMETER;
    }
    
    // Validate JWKS URI format
    if (!is_valid_url(jwks_uri)) {
        set_error_with_context(MCP_AUTH_ERROR_INVALID_CONFIG,
                              "Invalid JWKS URI format",
                              std::string("URI: ") + jwks_uri);
        return MCP_AUTH_ERROR_INVALID_CONFIG;
    }
    
    // Validate issuer URL format
    if (!is_valid_url(issuer)) {
        set_error_with_context(MCP_AUTH_ERROR_INVALID_CONFIG,
                              "Invalid issuer URL format",
                              std::string("Issuer: ") + issuer);
        return MCP_AUTH_ERROR_INVALID_CONFIG;
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
    if (!g_initialized.load()) {
        set_error(MCP_AUTH_ERROR_NOT_INITIALIZED, "Library not initialized");
        return MCP_AUTH_ERROR_NOT_INITIALIZED;
    }
    
    if (!client) {
        set_error(MCP_AUTH_ERROR_INVALID_PARAMETER, "Invalid client");
        return MCP_AUTH_ERROR_INVALID_PARAMETER;
    }
    
    clear_error();
    
    // Clean up cached JWKS keys
    {
        std::unique_lock<std::shared_mutex> lock(client->cache_mutex);
        
        // Clear PEM strings in cached keys
        for (auto& key : client->cached_keys) {
            // PEM strings are automatically cleaned up by std::string destructor
            // But we can explicitly clear sensitive data
            if (!key.pem.empty()) {
                // Overwrite PEM key data
                std::fill(key.pem.begin(), key.pem.end(), '\0');
            }
            if (!key.n.empty()) {
                std::fill(key.n.begin(), key.n.end(), '\0');
            }
            if (!key.e.empty()) {
                std::fill(key.e.begin(), key.e.end(), '\0');
            }
        }
        client->cached_keys.clear();
    }
    
    // Clear any cached credentials
    if (!client->last_alg.empty()) {
        std::fill(client->last_alg.begin(), client->last_alg.end(), '\0');
    }
    if (!client->last_kid.empty()) {
        std::fill(client->last_kid.begin(), client->last_kid.end(), '\0');
    }
    
    // Delete the client object
    delete client;
    return MCP_AUTH_SUCCESS;
}

mcp_auth_error_t mcp_auth_client_set_option(
    mcp_auth_client_t client,
    const char* option,
    const char* value) {
    
    if (!g_initialized.load()) {
        set_error(MCP_AUTH_ERROR_NOT_INITIALIZED, "Library not initialized");
        return MCP_AUTH_ERROR_NOT_INITIALIZED;
    }
    
    if (!client || !option || !value) {
        set_error(MCP_AUTH_ERROR_INVALID_PARAMETER, "Invalid parameter");
        return MCP_AUTH_ERROR_INVALID_PARAMETER;
    }
    
    clear_error();
    
    std::string opt(option);
    std::string val(value);
    
    try {
        if (opt == "cache_duration") {
            int64_t duration = std::stoll(val);
            if (duration < 0) {
                set_error_with_context(MCP_AUTH_ERROR_INVALID_CONFIG,
                                      "Invalid cache duration",
                                      "Duration must be non-negative: " + val);
                return MCP_AUTH_ERROR_INVALID_CONFIG;
            }
            client->cache_duration = duration;
            
        } else if (opt == "auto_refresh") {
            // Accept various boolean representations
            std::transform(val.begin(), val.end(), val.begin(), ::tolower);
            client->auto_refresh = (val == "true" || val == "1" || val == "yes" || val == "on");
            
        } else if (opt == "request_timeout") {
            int64_t timeout = std::stoll(val);
            if (timeout <= 0 || timeout > 300) {  // Max 5 minutes
                set_error_with_context(MCP_AUTH_ERROR_INVALID_CONFIG,
                                      "Invalid request timeout",
                                      "Timeout must be between 1-300 seconds: " + val);
                return MCP_AUTH_ERROR_INVALID_CONFIG;
            }
            client->request_timeout = timeout;
            
        } else if (opt == "client_id") {
            fprintf(stderr, "[MCP AUTH SET OPTION] Setting client_id: %s (was: %s)\n", 
                    val.c_str(), client->client_id.c_str());
            client->client_id = val;
        } else if (opt == "client_secret") {
            fprintf(stderr, "[MCP AUTH SET OPTION] Setting client_secret: %s...%s (length: %zu, was length: %zu)\n", 
                    val.substr(0, 8).c_str(),
                    val.length() > 16 ? val.substr(val.length() - 8).c_str() : "",
                    val.length(),
                    client->client_secret.length());
            client->client_secret = val;
        } else if (opt == "token_endpoint") {
            if (!val.empty() && !is_valid_url(val)) {
                set_error_with_context(MCP_AUTH_ERROR_INVALID_CONFIG, 
                                      "Invalid token endpoint URL",
                                      "URL: " + val);
                return MCP_AUTH_ERROR_INVALID_CONFIG;
            }
            client->token_endpoint = val;
        } else if (opt == "exchange_idps") {
            client->exchange_idps = val;
        } else {
            set_error_with_context(MCP_AUTH_ERROR_INVALID_PARAMETER,
                                  "Unknown configuration option",
                                  "Option: " + opt);
            return MCP_AUTH_ERROR_INVALID_PARAMETER;
        }
    } catch (const std::exception& e) {
        set_error_with_context(MCP_AUTH_ERROR_INVALID_CONFIG,
                              "Failed to parse option value",
                              "Option: " + opt + ", Value: " + val + ", Error: " + e.what());
        return MCP_AUTH_ERROR_INVALID_CONFIG;
    }
    
    return MCP_AUTH_SUCCESS;
}

// ========================================================================
// Validation Options
// ========================================================================

mcp_auth_error_t mcp_auth_validation_options_create(
    mcp_auth_validation_options_t* options) {
    
    if (!g_initialized.load()) {
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
        // Options are already initialized with defaults via constructor
        // clock_skew = 60, scopes = "", audience = ""
        return MCP_AUTH_SUCCESS;
    } catch (const std::exception& e) {
        set_error_with_context(MCP_AUTH_ERROR_OUT_OF_MEMORY, 
                              "Failed to create validation options",
                              std::string("Exception: ") + e.what());
        return MCP_AUTH_ERROR_OUT_OF_MEMORY;
    }
}

mcp_auth_error_t mcp_auth_validation_options_destroy(
    mcp_auth_validation_options_t options) {
    
    if (!g_initialized.load()) {
        set_error(MCP_AUTH_ERROR_NOT_INITIALIZED, "Library not initialized");
        return MCP_AUTH_ERROR_NOT_INITIALIZED;
    }
    
    if (!options) {
        set_error(MCP_AUTH_ERROR_INVALID_PARAMETER, "Invalid options");
        return MCP_AUTH_ERROR_INVALID_PARAMETER;
    }
    
    clear_error();
    
    // Clear sensitive data before deletion
    if (!options->scopes.empty()) {
        std::fill(options->scopes.begin(), options->scopes.end(), '\0');
    }
    if (!options->audience.empty()) {
        std::fill(options->audience.begin(), options->audience.end(), '\0');
    }
    
    delete options;
    return MCP_AUTH_SUCCESS;
}

mcp_auth_error_t mcp_auth_validation_options_set_scopes(
    mcp_auth_validation_options_t options,
    const char* scopes) {
    
    if (!g_initialized.load()) {
        set_error(MCP_AUTH_ERROR_NOT_INITIALIZED, "Library not initialized");
        return MCP_AUTH_ERROR_NOT_INITIALIZED;
    }
    
    if (!options) {
        set_error(MCP_AUTH_ERROR_INVALID_PARAMETER, "Invalid options");
        return MCP_AUTH_ERROR_INVALID_PARAMETER;
    }
    
    clear_error();
    
    // Validate and normalize scope string
    if (scopes) {
        // Trim whitespace from scopes
        std::string scope_str(scopes);
        size_t first = scope_str.find_first_not_of(' ');
        if (first != std::string::npos) {
            size_t last = scope_str.find_last_not_of(' ');
            options->scopes = scope_str.substr(first, (last - first + 1));
        } else {
            options->scopes = "";  // All whitespace
        }
    } else {
        options->scopes = "";  // Clear scopes if NULL
    }
    
    return MCP_AUTH_SUCCESS;
}

mcp_auth_error_t mcp_auth_validation_options_set_audience(
    mcp_auth_validation_options_t options,
    const char* audience) {
    
    if (!g_initialized.load()) {
        set_error(MCP_AUTH_ERROR_NOT_INITIALIZED, "Library not initialized");
        return MCP_AUTH_ERROR_NOT_INITIALIZED;
    }
    
    if (!options) {
        set_error(MCP_AUTH_ERROR_INVALID_PARAMETER, "Invalid options");
        return MCP_AUTH_ERROR_INVALID_PARAMETER;
    }
    
    clear_error();
    
    // Validate and store audience
    if (audience) {
        // Trim whitespace from audience
        std::string aud_str(audience);
        size_t first = aud_str.find_first_not_of(' ');
        if (first != std::string::npos) {
            size_t last = aud_str.find_last_not_of(' ');
            options->audience = aud_str.substr(first, (last - first + 1));
        } else {
            options->audience = "";  // All whitespace
        }
        
        // Optionally validate audience format (e.g., URL or identifier)
        // For now, accept any non-empty string
    } else {
        options->audience = "";  // Clear audience if NULL
    }
    
    return MCP_AUTH_SUCCESS;
}

mcp_auth_error_t mcp_auth_validation_options_set_clock_skew(
    mcp_auth_validation_options_t options,
    int64_t seconds) {
    
    if (!g_initialized.load()) {
        set_error(MCP_AUTH_ERROR_NOT_INITIALIZED, "Library not initialized");
        return MCP_AUTH_ERROR_NOT_INITIALIZED;
    }
    
    if (!options) {
        set_error(MCP_AUTH_ERROR_INVALID_PARAMETER, "Invalid options");
        return MCP_AUTH_ERROR_INVALID_PARAMETER;
    }
    
    clear_error();
    
    // Validate clock skew range
    if (seconds < 0) {
        set_error_with_context(MCP_AUTH_ERROR_INVALID_CONFIG,
                              "Invalid clock skew",
                              "Clock skew must be non-negative: " + std::to_string(seconds));
        return MCP_AUTH_ERROR_INVALID_CONFIG;
    }
    
    // Warn if clock skew is unusually large (> 5 minutes)
    if (seconds > 300) {
        fprintf(stderr, "Warning: Large clock skew configured: %lld seconds\n", (long long)seconds);
    }
    
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
    
    if (!g_initialized.load()) {
        set_error(MCP_AUTH_ERROR_NOT_INITIALIZED, "Library not initialized");
        return MCP_AUTH_ERROR_NOT_INITIALIZED;
    }
    
    if (!client || !token || !result) {
        std::string context = "Missing: ";
        if (!client) context += "client, ";
        if (!token) context += "token, ";
        if (!result) context += "result";
        
        if (result) {
            result->valid = false;
            result->error_code = MCP_AUTH_ERROR_INVALID_PARAMETER;
            result->error_message = "Invalid parameters";
        }
        set_client_error(client, MCP_AUTH_ERROR_INVALID_PARAMETER,
                        "Invalid parameters for token validation", context);
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
        result->error_message = safe_strdup(g_last_error);
        return g_last_error_code;
    }
    
    // Step 2: Decode and parse header
    std::string header_json = base64url_decode(header_b64);
    if (header_json.empty()) {
        set_error(MCP_AUTH_ERROR_INVALID_TOKEN, "Failed to decode JWT header");
        result->error_code = MCP_AUTH_ERROR_INVALID_TOKEN;
        result->error_message = safe_strdup("Failed to decode JWT header");
        return MCP_AUTH_ERROR_INVALID_TOKEN;
    }
    
    std::string alg, kid;
    if (!parse_jwt_header(header_json, alg, kid)) {
        result->error_code = g_last_error_code;
        result->error_message = safe_strdup(g_last_error);
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
        result->error_message = safe_strdup("Failed to decode JWT payload");
        return MCP_AUTH_ERROR_INVALID_TOKEN;
    }
    
    // Parse payload claims
    mcp_auth_token_payload payload_data;
    if (!parse_jwt_payload(payload_json, &payload_data)) {
        result->error_code = g_last_error_code;
        result->error_message = safe_strdup(g_last_error);
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
        result->error_message = safe_strdup("Failed to decode JWT signature");
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
        result->error_message = safe_strdup(g_last_error);
        return g_last_error_code;
    }
    
    // Step 5: Validate claims
    
    // Check expiration with clock skew
    int64_t now = std::chrono::system_clock::now().time_since_epoch().count() / 1000000000; // Convert to seconds
    int64_t clock_skew = options ? options->clock_skew : 60; // Default 60 seconds
    
    if (payload_data.expiration > 0) {
        if (now > payload_data.expiration + clock_skew) {
            std::string context = "Token expired at " + std::to_string(payload_data.expiration) + 
                                ", current time: " + std::to_string(now) +
                                ", clock skew: " + std::to_string(clock_skew) + "s";
            set_client_error(client, MCP_AUTH_ERROR_EXPIRED_TOKEN, 
                           "JWT has expired", context);
            result->error_code = MCP_AUTH_ERROR_EXPIRED_TOKEN;
            result->error_message = safe_strdup(("JWT has expired [" + context + "]").c_str());
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
                result->error_message = safe_strdup("JWT not yet valid (nbf)");
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
                result->error_message = safe_strdup(g_last_error);
                return MCP_AUTH_ERROR_INVALID_ISSUER;
            }
        }
    }
    
    // Validate audience if specified (using helper method for clarity)
    if (options && options->requires_audience_validation()) {
        if (payload_data.audience.empty()) {
            set_error(MCP_AUTH_ERROR_INVALID_AUDIENCE, "JWT has no audience claim");
            result->error_code = MCP_AUTH_ERROR_INVALID_AUDIENCE;
            result->error_message = safe_strdup("JWT has no audience claim");
            return MCP_AUTH_ERROR_INVALID_AUDIENCE;
        }
        
        // Check if the required audience matches the token audience
        // Token audience can be a single string or array (we handle single string from parsing)
        if (payload_data.audience != options->audience) {
            set_error(MCP_AUTH_ERROR_INVALID_AUDIENCE, 
                     "Invalid audience. Expected: " + options->audience + ", Got: " + payload_data.audience);
            result->error_code = MCP_AUTH_ERROR_INVALID_AUDIENCE;
            result->error_message = safe_strdup(g_last_error);
            return MCP_AUTH_ERROR_INVALID_AUDIENCE;
        }
    }
    
    // Validate scopes if required (using helper method for clarity)
    if (options && options->requires_scope_validation()) {
        if (payload_data.scopes.empty()) {
            set_error(MCP_AUTH_ERROR_INSUFFICIENT_SCOPE, "JWT has no scope claim");
            result->error_code = MCP_AUTH_ERROR_INSUFFICIENT_SCOPE;
            result->error_message = safe_strdup("JWT has no scope claim");
            return MCP_AUTH_ERROR_INSUFFICIENT_SCOPE;
        }
        
        // Check if all required scopes are present
        if (!mcp_auth_validate_scopes(options->scopes.c_str(), payload_data.scopes.c_str())) {
            set_error(MCP_AUTH_ERROR_INSUFFICIENT_SCOPE, 
                     "Insufficient scope. Required: " + options->scopes + ", Available: " + payload_data.scopes);
            result->error_code = MCP_AUTH_ERROR_INSUFFICIENT_SCOPE;
            result->error_message = safe_strdup(g_last_error);
            return MCP_AUTH_ERROR_INSUFFICIENT_SCOPE;
        }
    }
    
    // Token is valid
    result->valid = true;
    result->error_code = MCP_AUTH_SUCCESS;
    
    return MCP_AUTH_SUCCESS;
}

mcp_auth_error_t mcp_auth_extract_payload(
    const char* token,
    mcp_auth_token_payload_t* payload) {
    
    if (!g_initialized.load()) {
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
    
    if (!g_initialized.load()) {
        set_error(MCP_AUTH_ERROR_NOT_INITIALIZED, "Library not initialized");
        return MCP_AUTH_ERROR_NOT_INITIALIZED;
    }
    
    if (!payload || !value) {
        set_error(MCP_AUTH_ERROR_INVALID_PARAMETER, "Invalid parameter");
        return MCP_AUTH_ERROR_INVALID_PARAMETER;
    }
    
    clear_error();
    *value = safe_strdup(payload->subject);
    return MCP_AUTH_SUCCESS;
}

mcp_auth_error_t mcp_auth_payload_get_issuer(
    mcp_auth_token_payload_t payload,
    char** value) {
    
    if (!g_initialized.load()) {
        set_error(MCP_AUTH_ERROR_NOT_INITIALIZED, "Library not initialized");
        return MCP_AUTH_ERROR_NOT_INITIALIZED;
    }
    
    if (!payload || !value) {
        set_error(MCP_AUTH_ERROR_INVALID_PARAMETER, "Invalid parameter");
        return MCP_AUTH_ERROR_INVALID_PARAMETER;
    }
    
    clear_error();
    *value = safe_strdup(payload->issuer);
    return MCP_AUTH_SUCCESS;
}

mcp_auth_error_t mcp_auth_payload_get_audience(
    mcp_auth_token_payload_t payload,
    char** value) {
    
    if (!g_initialized.load()) {
        set_error(MCP_AUTH_ERROR_NOT_INITIALIZED, "Library not initialized");
        return MCP_AUTH_ERROR_NOT_INITIALIZED;
    }
    
    if (!payload || !value) {
        set_error(MCP_AUTH_ERROR_INVALID_PARAMETER, "Invalid parameter");
        return MCP_AUTH_ERROR_INVALID_PARAMETER;
    }
    
    clear_error();
    *value = safe_strdup(payload->audience);
    return MCP_AUTH_SUCCESS;
}

mcp_auth_error_t mcp_auth_payload_get_scopes(
    mcp_auth_token_payload_t payload,
    char** value) {
    
    if (!g_initialized.load()) {
        set_error(MCP_AUTH_ERROR_NOT_INITIALIZED, "Library not initialized");
        return MCP_AUTH_ERROR_NOT_INITIALIZED;
    }
    
    if (!payload || !value) {
        set_error(MCP_AUTH_ERROR_INVALID_PARAMETER, "Invalid parameter");
        return MCP_AUTH_ERROR_INVALID_PARAMETER;
    }
    
    clear_error();
    *value = safe_strdup(payload->scopes);
    return MCP_AUTH_SUCCESS;
}

mcp_auth_error_t mcp_auth_payload_get_expiration(
    mcp_auth_token_payload_t payload,
    int64_t* value) {
    
    if (!g_initialized.load()) {
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
    
    if (!g_initialized.load()) {
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
        *value = safe_strdup(it->second);
    } else {
        *value = nullptr;
    }
    
    return MCP_AUTH_SUCCESS;
}

mcp_auth_error_t mcp_auth_payload_destroy(mcp_auth_token_payload_t payload) {
    if (!g_initialized.load()) {
        set_error(MCP_AUTH_ERROR_NOT_INITIALIZED, "Library not initialized");
        return MCP_AUTH_ERROR_NOT_INITIALIZED;
    }
    
    if (!payload) {
        set_error(MCP_AUTH_ERROR_INVALID_PARAMETER, "Invalid payload");
        return MCP_AUTH_ERROR_INVALID_PARAMETER;
    }
    
    clear_error();
    
    // Clear sensitive token data before deletion
    if (!payload->subject.empty()) {
        std::fill(payload->subject.begin(), payload->subject.end(), '\0');
    }
    if (!payload->scopes.empty()) {
        std::fill(payload->scopes.begin(), payload->scopes.end(), '\0');
    }
    
    // Clear all claims
#ifdef USE_CPP11_COMPAT
    // C++11 compatible version
    for (auto& claim : payload->claims) {
        if (!claim.second.empty()) {
            std::fill(claim.second.begin(), claim.second.end(), '\0');
        }
    }
#else
    // C++17 structured binding version
    for (auto& [key, value] : payload->claims) {
        if (!value.empty()) {
            std::fill(value.begin(), value.end(), '\0');
        }
    }
#endif
    
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
    
    if (!g_initialized.load()) {
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
    
    *header = safe_strdup(oss.str());
    return MCP_AUTH_SUCCESS;
}

// ========================================================================
// Memory Management
// ========================================================================

void mcp_auth_free_string(char* str) {
    if (str) {
        // Clear the string contents first for security
        size_t len = strlen(str);
        if (len > 0) {
            volatile char* p = str;
            while (len--) {
                *p++ = '\0';
            }
        }
        free(str);
    }
}

const char* mcp_auth_get_last_error(void) {
    // Thread-safe: Each thread has its own error state
    // Copy to thread-local buffer for safe return
    if (g_last_error.empty()) {
        return "";
    }
    
    // Copy error to thread-local buffer to ensure string lifetime
    size_t len = g_last_error.length();
    if (len >= sizeof(g_error_buffer)) {
        len = sizeof(g_error_buffer) - 1;
    }
    memcpy(g_error_buffer, g_last_error.c_str(), len);
    g_error_buffer[len] = '\0';
    
    return g_error_buffer;
}

mcp_auth_error_t mcp_auth_get_last_error_code(void) {
    return g_last_error_code;
}

// Get last error with full context information (thread-safe)
mcp_auth_error_t mcp_auth_get_last_error_full(char** error_message) {
    if (!error_message) {
        return MCP_AUTH_ERROR_INVALID_PARAMETER;
    }
    
    // Thread-safe: Build message from thread-local storage
    std::string full_message = g_last_error;
    if (!g_last_error_context.empty()) {
        full_message += " [Context: " + g_last_error_context + "]";
    }
    
    // Allocate new string for caller
    *error_message = safe_strdup(full_message);
    return g_last_error_code;
}

// Get error details from client
mcp_auth_error_t mcp_auth_client_get_last_error(mcp_auth_client_t client, char** error_message) {
    if (!client || !error_message) {
        return MCP_AUTH_ERROR_INVALID_PARAMETER;
    }
    
    *error_message = safe_strdup(client->last_error_context);
    return client->last_error_code;
}

void mcp_auth_clear_error(void) {
    clear_error();
}

// Clear error for a specific client
mcp_auth_error_t mcp_auth_client_clear_error(mcp_auth_client_t client) {
    if (!client) {
        return MCP_AUTH_ERROR_INVALID_PARAMETER;
    }
    
    client->last_error_code = MCP_AUTH_SUCCESS;
    client->last_error_context.clear();
    return MCP_AUTH_SUCCESS;
}

bool mcp_auth_has_error(void) {
    return g_last_error_code != MCP_AUTH_SUCCESS;
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
    
    // Parse available scopes into a set for efficient lookup
    std::unordered_set<std::string> available_set;
    std::istringstream available(available_scopes);
    std::string scope;
    while (available >> scope) {
        available_set.insert(scope);
        
        // Also add hierarchical scopes (e.g., "mcp:weather" includes "mcp:weather:read")
        size_t colon = scope.find(':');
        if (colon != std::string::npos) {
            // Add base scope (e.g., "mcp" from "mcp:weather")
            available_set.insert(scope.substr(0, colon));
        }
    }
    
    // Check if all required scopes are present
    std::istringstream required(required_scopes);
    while (required >> scope) {
        // Check exact match
        if (available_set.find(scope) != available_set.end()) {
            continue;
        }
        
        // Check hierarchical match (e.g., "mcp:weather:read" satisfied by "mcp:weather")
        bool found = false;
        size_t pos = scope.rfind(':');
        while (pos != std::string::npos && !found) {
            std::string parent = scope.substr(0, pos);
            if (available_set.find(parent) != available_set.end()) {
                found = true;
                break;
            }
            pos = parent.rfind(':');
        }
        
        if (!found) {
            return false;
        }
    }
    
    return true;
}

// ========================================================================
// Token Exchange Implementation (OAuth 2.0 Token Exchange - RFC 8693)
// ========================================================================

// URL encoding helper for form data
static std::string url_encode(const std::string& str) {
    CURL* curl = curl_easy_init();
    if (!curl) return str;
    
    char* encoded = curl_easy_escape(curl, str.c_str(), str.length());
    std::string result(encoded ? encoded : str);
    
    if (encoded) curl_free(encoded);
    curl_easy_cleanup(curl);
    
    return result;
}

// Helper to perform token exchange request
static bool perform_token_exchange(mcp_auth_client_t client,
                                  const std::string& subject_token,
                                  const std::string& idp_alias,
                                  const std::string& audience,
                                  const std::string& scope,
                                  mcp_auth_token_exchange_result_t& result) {
    
    // Initialize result
    memset(&result, 0, sizeof(result));
    
    // Validate required parameters
    if (client->token_endpoint.empty()) {
        result.error_code = MCP_AUTH_ERROR_INVALID_CONFIG;
        result.error_description = safe_strdup("Token endpoint not configured");
        return false;
    }
    
    if (client->client_id.empty() || client->client_secret.empty()) {
        result.error_code = MCP_AUTH_ERROR_INVALID_CONFIG;
        result.error_description = safe_strdup("Client credentials not configured");
        return false;
    }
    
    // Build request body (application/x-www-form-urlencoded)
    std::stringstream body;
    body << "grant_type=urn:ietf:params:oauth:grant-type:token-exchange";
    body << "&subject_token=" << url_encode(subject_token);
    body << "&subject_token_type=urn:ietf:params:oauth:token-type:access_token";
    body << "&requested_issuer=" << url_encode(idp_alias);
    
    // Add client credentials to body for Keycloak compatibility
    // Only add client_secret if it's available (for confidential clients)
    body << "&client_id=" << url_encode(client->client_id);
    if (!client->client_secret.empty()) {
        body << "&client_secret=" << url_encode(client->client_secret);
    }
    
    if (!audience.empty()) {
        body << "&audience=" << url_encode(audience);
    }
    
    if (!scope.empty()) {
        body << "&scope=" << url_encode(scope);
    }
    
    // Always log client credentials being used (mask the secret)
    fprintf(stderr, "[MCP AUTH TOKEN EXCHANGE] Using client credentials:\n");
    fprintf(stderr, "  Client ID: %s\n", client->client_id.c_str());
    fprintf(stderr, "  Client Secret: %s...%s (length: %zu)\n", 
            client->client_secret.substr(0, 8).c_str(),
            client->client_secret.length() > 16 ? client->client_secret.substr(client->client_secret.length() - 8).c_str() : "",
            client->client_secret.length());
    fprintf(stderr, "  Token Endpoint: %s\n", client->token_endpoint.c_str());
    fprintf(stderr, "  IDP Alias: %s\n", idp_alias.c_str());
    
    // Debug: Log the request details
    fprintf(stderr, "[MCP AUTH DEBUG] Token exchange request:\n");
    fprintf(stderr, "  URL: %s\n", client->token_endpoint.c_str());
    fprintf(stderr, "  Body: %s\n", body.str().c_str());
    
    // Perform HTTP POST request
    CURL* curl = curl_easy_init();
    if (!curl) {
        result.error_code = MCP_AUTH_ERROR_INTERNAL_ERROR;
        result.error_description = safe_strdup("Failed to initialize CURL");
        return false;
    }
    
    // RAII cleanup
    struct curl_guard {
        CURL* curl;
        ~curl_guard() { if (curl) curl_easy_cleanup(curl); }
    } guard{curl};
    
    // Store the body string to ensure it remains valid during the request
    std::string body_str = body.str();
    
    std::string response;
    std::string error_buffer;
    error_buffer.resize(CURL_ERROR_SIZE);
    
    // Configure CURL
    curl_easy_setopt(curl, CURLOPT_URL, client->token_endpoint.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body_str.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)body_str.length());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, jwks_curl_write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, error_buffer.data());
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, client->request_timeout);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    
    // Client credentials are now sent in the request body for Keycloak compatibility
    // (Some OAuth providers like Keycloak require this instead of Basic Auth)
    // std::string auth = client->client_id + ":" + client->client_secret;
    // curl_easy_setopt(curl, CURLOPT_USERPWD, auth.c_str());
    // curl_easy_setopt(curl, CURLOPT_HTTPAUTH, CURLAUTH_BASIC);
    
    // Set headers
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/x-www-form-urlencoded");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    
    // Perform request
    CURLcode res = curl_easy_perform(curl);
    
    // Clean up headers
    curl_slist_free_all(headers);
    
    // Check for CURL errors
    if (res != CURLE_OK) {
        result.error_code = MCP_AUTH_ERROR_NETWORK_ERROR;
        std::string error_msg = error_buffer.empty() ? curl_easy_strerror(res) : error_buffer;
        result.error_description = safe_strdup(error_msg);
        return false;
    }
    
    // Check HTTP status
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    
    // Always log the response for debugging token exchange issues
    fprintf(stderr, "[MCP AUTH DEBUG] Token exchange response:\n");
    fprintf(stderr, "  HTTP Code: %ld\n", http_code);
    fprintf(stderr, "  Response length: %zu\n", response.length());
    if (response.length() < 2000) {
        fprintf(stderr, "  Response: %s\n", response.c_str());
    } else {
        fprintf(stderr, "  Response (first 500 chars): %.500s...\n", response.c_str());
    }
    
    if (http_code != 200) {
        // Parse error response
        std::string error_type, error_desc;
        extract_json_string(response, "error", error_type);
        extract_json_string(response, "error_description", error_desc);
        
        // Map error types to our error codes
        if (error_type == "invalid_request") {
            result.error_code = MCP_AUTH_ERROR_INVALID_PARAMETER;
        } else if (error_type == "invalid_token") {
            result.error_code = MCP_AUTH_ERROR_INVALID_TOKEN;
        } else if (error_type == "insufficient_scope") {
            result.error_code = MCP_AUTH_ERROR_INSUFFICIENT_SCOPE;
        } else if (error_type == "invalid_target" || error_desc.find("not linked") != std::string::npos) {
            result.error_code = MCP_AUTH_ERROR_IDP_NOT_LINKED;
        } else {
            result.error_code = MCP_AUTH_ERROR_TOKEN_EXCHANGE_FAILED;
        }
        
        std::string error_msg = error_desc.empty() ? 
            ("Token exchange failed with status " + std::to_string(http_code)) : 
            error_desc;
        result.error_description = safe_strdup(error_msg);
        return false;
    }
    
    // Parse successful response
    // Note: Some Keycloak configurations may include account-link-url even on success,
    // so we don't treat its presence as an error. Check for access_token first.
    std::string access_token, token_type, scope_str, refresh_token;
    int64_t expires_in = 0;
    
    if (!extract_json_string(response, "access_token", access_token)) {
        // Check if this is an account linking response (no access_token but has account-link-url)
        std::string account_link_url;
        if (extract_json_string(response, "account-link-url", account_link_url) && !account_link_url.empty()) {
            // This is an account linking response - the user needs to link their account to the external IDP
            fprintf(stderr, "[MCP AUTH] Account linking required for IDP: %s\n", idp_alias.c_str());
            fprintf(stderr, "  Link URL: %s\n", account_link_url.c_str());
            
            // Return a special error code to indicate account linking is needed
            result.error_code = MCP_AUTH_ERROR_INVALID_IDP_ALIAS; // Using existing error code
            std::string msg = "Account not linked to IDP '" + idp_alias + "'. User must link account at: " + account_link_url;
            result.error_description = safe_strdup(msg.c_str());
            
            // Store the link URL in the access_token field for the application to handle if needed
            result.access_token = safe_strdup(account_link_url.c_str());
            result.token_type = safe_strdup("account-link-required");
            return false;
        }
        
        result.error_code = MCP_AUTH_ERROR_INTERNAL_ERROR;
        result.error_description = safe_strdup("Invalid response: missing access_token");
        return false;
    }
    
    extract_json_string(response, "token_type", token_type);
    extract_json_string(response, "scope", scope_str);
    extract_json_string(response, "refresh_token", refresh_token);
    extract_json_number(response, "expires_in", expires_in);
    
    // Populate result
    result.access_token = safe_strdup(access_token);
    result.token_type = safe_strdup(token_type.empty() ? "Bearer" : token_type);
    result.scope = scope_str.empty() ? nullptr : safe_strdup(scope_str);
    result.refresh_token = refresh_token.empty() ? nullptr : safe_strdup(refresh_token);
    result.expires_in = expires_in;
    result.error_code = MCP_AUTH_SUCCESS;
    
    return true;
}

mcp_auth_token_exchange_result_t mcp_auth_exchange_token(
    mcp_auth_client_t client,
    const char* subject_token,
    const char* idp_alias,
    const char* audience,
    const char* scope) {
    
    mcp_auth_token_exchange_result_t result;
    memset(&result, 0, sizeof(result));
    
    // Validate parameters
    if (!client) {
        result.error_code = MCP_AUTH_ERROR_INVALID_PARAMETER;
        result.error_description = safe_strdup("Client is required");
        return result;
    }
    
    if (!subject_token || !subject_token[0]) {
        result.error_code = MCP_AUTH_ERROR_INVALID_PARAMETER;
        result.error_description = safe_strdup("Subject token is required");
        return result;
    }
    
    if (!idp_alias || !idp_alias[0]) {
        result.error_code = MCP_AUTH_ERROR_INVALID_IDP_ALIAS;
        result.error_description = safe_strdup("IDP alias is required");
        return result;
    }
    
    // Auto-detect token endpoint if not set
    if (client->token_endpoint.empty() && !client->issuer.empty()) {
        // Derive from issuer (Keycloak pattern)
        client->token_endpoint = client->issuer + "/protocol/openid-connect/token";
    }
    
    // Perform the exchange
    perform_token_exchange(client, subject_token, idp_alias,
                          audience ? audience : "",
                          scope ? scope : "",
                          result);
    
    return result;
}

mcp_auth_error_t mcp_auth_exchange_token_multi(
    mcp_auth_client_t client,
    const char* subject_token,
    const char* idp_aliases,
    mcp_auth_token_exchange_result_t* results,
    size_t* result_count) {
    
    if (!client || !subject_token || !idp_aliases || !results || !result_count) {
        set_error(MCP_AUTH_ERROR_INVALID_PARAMETER, "All parameters are required");
        return MCP_AUTH_ERROR_INVALID_PARAMETER;
    }
    
    // Parse comma-separated IDP aliases
    std::vector<std::string> idps;
    std::stringstream ss(idp_aliases);
    std::string idp;
    
    while (std::getline(ss, idp, ',')) {
        // Trim whitespace
        idp.erase(0, idp.find_first_not_of(" \t"));
        idp.erase(idp.find_last_not_of(" \t") + 1);
        
        if (!idp.empty()) {
            idps.push_back(idp);
        }
    }
    
    if (idps.empty()) {
        set_error(MCP_AUTH_ERROR_INVALID_IDP_ALIAS, "No valid IDP aliases provided");
        return MCP_AUTH_ERROR_INVALID_IDP_ALIAS;
    }
    
    // Check result array size
    if (*result_count < idps.size()) {
        set_error(MCP_AUTH_ERROR_INVALID_PARAMETER, 
                 "Result array too small: need " + std::to_string(idps.size()));
        return MCP_AUTH_ERROR_INVALID_PARAMETER;
    }
    
    // Perform parallel exchanges using threads
    std::vector<std::thread> threads;
    threads.reserve(idps.size());
    
    for (size_t i = 0; i < idps.size(); i++) {
        threads.emplace_back([client, subject_token, &idps, &results, i]() {
            results[i] = mcp_auth_exchange_token(client, subject_token, 
                                                idps[i].c_str(), nullptr, nullptr);
        });
    }
    
    // Wait for all threads to complete
    for (auto& t : threads) {
        t.join();
    }
    
    // Update result count
    *result_count = idps.size();
    
    return MCP_AUTH_SUCCESS;
}

mcp_auth_error_t mcp_auth_set_exchange_idps(mcp_auth_client_t client, const char* idp_aliases) {
    if (!client) {
        set_error(MCP_AUTH_ERROR_INVALID_PARAMETER, "Client is required");
        return MCP_AUTH_ERROR_INVALID_PARAMETER;
    }
    
    client->exchange_idps = idp_aliases ? idp_aliases : "";
    return MCP_AUTH_SUCCESS;
}

void mcp_auth_free_exchange_result(mcp_auth_token_exchange_result_t* result) {
    if (!result) return;
    
    free(result->access_token);
    free(result->token_type);
    free(result->refresh_token);
    free(result->scope);
    
    memset(result, 0, sizeof(*result));
}

mcp_auth_error_t mcp_auth_validate_and_exchange(
    mcp_auth_client_t client,
    const char* token,
    mcp_auth_validation_options_t options,
    mcp_auth_validation_result_t* validation_result,
    mcp_auth_token_exchange_result_t* exchange_results,
    size_t* exchange_count) {
    
    if (!client || !token || !validation_result) {
        set_error(MCP_AUTH_ERROR_INVALID_PARAMETER, "Required parameters missing");
        return MCP_AUTH_ERROR_INVALID_PARAMETER;
    }
    
    // First validate the token
    mcp_auth_error_t validation_err = mcp_auth_validate_token(client, token, options, validation_result);
    
    if (validation_err != MCP_AUTH_SUCCESS || !validation_result->valid) {
        // Validation failed, don't attempt exchange
        if (exchange_count) *exchange_count = 0;
        return validation_err != MCP_AUTH_SUCCESS ? validation_err : MCP_AUTH_ERROR_INVALID_TOKEN;
    }
    
    // Check if automatic exchange is configured
    if (client->exchange_idps.empty() || !exchange_results || !exchange_count) {
        // No exchange configured or no result buffer
        if (exchange_count) *exchange_count = 0;
        return MCP_AUTH_SUCCESS;
    }
    
    // Perform multi-IDP exchange
    return mcp_auth_exchange_token_multi(client, token, client->exchange_idps.c_str(),
                                        exchange_results, exchange_count);
}

const char* mcp_auth_error_to_string(mcp_auth_error_t error_code) {
    switch (error_code) {
        case MCP_AUTH_SUCCESS:
            return "Success";
        case MCP_AUTH_ERROR_INVALID_TOKEN:
            return "Invalid or malformed JWT token";
        case MCP_AUTH_ERROR_EXPIRED_TOKEN:
            return "JWT token has expired";
        case MCP_AUTH_ERROR_INVALID_SIGNATURE:
            return "JWT signature verification failed";
        case MCP_AUTH_ERROR_INVALID_ISSUER:
            return "Token issuer does not match expected value";
        case MCP_AUTH_ERROR_INVALID_AUDIENCE:
            return "Token audience does not match expected value";
        case MCP_AUTH_ERROR_INSUFFICIENT_SCOPE:
            return "Token lacks required scopes for operation";
        case MCP_AUTH_ERROR_JWKS_FETCH_FAILED:
            return "Failed to fetch JWKS from authorization server";
        case MCP_AUTH_ERROR_INVALID_KEY:
            return "No valid signing key found in JWKS";
        case MCP_AUTH_ERROR_NETWORK_ERROR:
            return "Network communication error";
        case MCP_AUTH_ERROR_INVALID_CONFIG:
            return "Invalid authentication configuration";
        case MCP_AUTH_ERROR_OUT_OF_MEMORY:
            return "Memory allocation failed";
        case MCP_AUTH_ERROR_INVALID_PARAMETER:
            return "Invalid parameter passed to function";
        case MCP_AUTH_ERROR_NOT_INITIALIZED:
            return "Authentication library not initialized";
        case MCP_AUTH_ERROR_INTERNAL_ERROR:
            return "Internal library error";
        case MCP_AUTH_ERROR_INVALID_IDP_ALIAS:
            return "Invalid or missing IDP alias";
        case MCP_AUTH_ERROR_IDP_NOT_LINKED:
            return "User account not linked to requested IDP";
        case MCP_AUTH_ERROR_TOKEN_EXCHANGE_FAILED:
            return "Token exchange failed";
        default:
            return "Unknown error code";
    }
}

int mcp_auth_error_to_http_status(mcp_auth_error_t error_code) {
    // Map error codes to appropriate HTTP status codes
    switch (error_code) {
        case MCP_AUTH_SUCCESS: 
            return 200;  // OK
        case MCP_AUTH_ERROR_INVALID_TOKEN:
        case MCP_AUTH_ERROR_EXPIRED_TOKEN:
        case MCP_AUTH_ERROR_INVALID_SIGNATURE:
        case MCP_AUTH_ERROR_INVALID_ISSUER:
        case MCP_AUTH_ERROR_INVALID_AUDIENCE:
            return 401;  // Unauthorized
        case MCP_AUTH_ERROR_INSUFFICIENT_SCOPE:
            return 403;  // Forbidden
        case MCP_AUTH_ERROR_INVALID_PARAMETER:
        case MCP_AUTH_ERROR_INVALID_CONFIG:
            return 400;  // Bad Request
        case MCP_AUTH_ERROR_JWKS_FETCH_FAILED:
        case MCP_AUTH_ERROR_NETWORK_ERROR:
            return 502;  // Bad Gateway
        case MCP_AUTH_ERROR_OUT_OF_MEMORY:
        case MCP_AUTH_ERROR_NOT_INITIALIZED:
        case MCP_AUTH_ERROR_INTERNAL_ERROR:
        case MCP_AUTH_ERROR_INVALID_KEY:
        default:
            return 500;  // Internal Server Error
    }
}

} // extern "C"