#ifndef MCP_AUTH_JWKS_CLIENT_H
#define MCP_AUTH_JWKS_CLIENT_H

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <chrono>
#include "mcp/core/optional.h"

/**
 * @file jwks_client.h
 * @brief JWKS client with caching and key rotation support
 */

namespace mcp {
namespace auth {

// Forward declarations
class HttpClient;
template <typename Key, typename Value, typename Hash> class MemoryCache;

/**
 * @brief JSON Web Key representation
 */
struct JsonWebKey {
  std::string kid;          // Key ID
  std::string kty;          // Key type (RSA, EC, etc.)
  std::string use;          // Key use (sig, enc)
  std::string alg;          // Algorithm (RS256, ES256, etc.)
  
  // RSA specific fields
  std::string n;            // Modulus (RSA)
  std::string e;            // Exponent (RSA)
  
  // EC specific fields  
  std::string crv;          // Curve (EC)
  std::string x;            // X coordinate (EC)
  std::string y;            // Y coordinate (EC)
  
  // Optional fields
  mcp::optional<std::string> x5c;  // X.509 certificate chain
  mcp::optional<std::string> x5t;  // X.509 thumbprint
  
  /**
   * @brief Check if key is valid
   * @return true if key has required fields
   */
  bool is_valid() const;
  
  /**
   * @brief Get key type as enum
   */
  enum class KeyType {
    RSA,
    EC,
    OCT,
    UNKNOWN
  };
  
  KeyType get_key_type() const;
};

/**
 * @brief JWKS response containing multiple keys
 */
struct JwksResponse {
  std::vector<JsonWebKey> keys;
  std::chrono::system_clock::time_point fetched_at;
  std::chrono::seconds cache_duration;
  
  /**
   * @brief Find key by ID
   * @param kid Key ID to find
   * @return Key if found, nullopt otherwise
   */
  mcp::optional<JsonWebKey> find_key(const std::string& kid) const;
  
  /**
   * @brief Check if response is expired
   * @return true if cache duration has elapsed
   */
  bool is_expired() const;
};

/**
 * @brief JWKS client configuration
 */
struct JwksClientConfig {
  std::string jwks_uri;                               // JWKS endpoint URL
  std::chrono::seconds default_cache_duration;        // Default cache duration
  std::chrono::seconds min_cache_duration;            // Minimum cache duration
  std::chrono::seconds max_cache_duration;            // Maximum cache duration
  bool respect_cache_control;                         // Honor cache-control headers
  size_t max_keys_cached;                             // Maximum keys to cache
  std::chrono::seconds request_timeout;               // HTTP request timeout
  bool auto_refresh;                                  // Enable automatic refresh
  std::chrono::seconds refresh_before_expiry;         // Refresh N seconds before expiry
  
  JwksClientConfig();
};

/**
 * @brief JWKS client with caching and key rotation support
 */
class JwksClient {
public:
  using RefreshCallback = std::function<void(const JwksResponse&)>;
  using ErrorCallback = std::function<void(const std::string&)>;
  
  /**
   * @brief Construct JWKS client
   * @param config Client configuration
   */
  explicit JwksClient(const JwksClientConfig& config);
  
  /**
   * @brief Destructor
   */
  ~JwksClient();
  
  /**
   * @brief Fetch JWKS from endpoint
   * @param force_refresh Force refresh even if cached
   * @return JWKS response or error
   */
  mcp::optional<JwksResponse> fetch_keys(bool force_refresh = false);
  
  /**
   * @brief Get key by ID
   * @param kid Key ID
   * @return Key if found and valid
   */
  mcp::optional<JsonWebKey> get_key(const std::string& kid);
  
  /**
   * @brief Get all cached keys
   * @return Vector of all cached keys
   */
  std::vector<JsonWebKey> get_all_keys() const;
  
  /**
   * @brief Start automatic refresh
   * @param on_refresh Callback on successful refresh
   * @param on_error Callback on refresh error
   */
  void start_auto_refresh(RefreshCallback on_refresh = nullptr,
                         ErrorCallback on_error = nullptr);
  
  /**
   * @brief Stop automatic refresh
   */
  void stop_auto_refresh();
  
  /**
   * @brief Check if auto refresh is running
   * @return true if auto refresh is active
   */
  bool is_auto_refresh_active() const;
  
  /**
   * @brief Clear all cached keys
   */
  void clear_cache();
  
  /**
   * @brief Get cache statistics
   */
  struct CacheStats {
    size_t keys_cached;
    size_t cache_hits;
    size_t cache_misses;
    size_t refresh_count;
    size_t error_count;
    std::chrono::system_clock::time_point last_refresh;
    std::chrono::system_clock::time_point next_refresh;
  };
  
  CacheStats get_cache_stats() const;
  
  /**
   * @brief Parse JWKS JSON response
   * @param json JSON string containing JWKS
   * @return Parsed JWKS response
   */
  static mcp::optional<JwksResponse> parse_jwks(const std::string& json);
  
  /**
   * @brief Parse cache-control header
   * @param header Cache-control header value
   * @return Cache duration in seconds
   */
  static std::chrono::seconds parse_cache_control(const std::string& header);

private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace auth
} // namespace mcp

#endif // MCP_AUTH_JWKS_CLIENT_H