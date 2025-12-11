/**
 * @file mcp_c_auth_api_network_optimized.cc
 * @brief Optimized network operations for JWKS fetching
 * 
 * Implements connection pooling, DNS caching, and efficient parsing
 */

#ifdef USE_CPP11_COMPAT
#include "cpp11_compat.h"
#endif

#include <curl/curl.h>
#include <string>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <chrono>
#include <atomic>
#include <cstring>
#include <vector>
#include <queue>
// RapidJSON would be used here for optimized parsing
// For now, using simple JSON parsing
#include <thread>

namespace network_optimized {

// ========================================================================
// Connection Pool Management
// ========================================================================

class ConnectionPool {
public:
    struct PooledConnection {
        CURL* handle;
        std::chrono::steady_clock::time_point last_used;
        std::string last_host;
        size_t use_count;
        
        PooledConnection() : handle(nullptr), use_count(0) {}
        
        ~PooledConnection() {
            if (handle) {
                curl_easy_cleanup(handle);
            }
        }
    };
    
    static ConnectionPool& getInstance() {
        static ConnectionPool instance;
        return instance;
    }
    
    // Borrow a connection from the pool
    CURL* borrowConnection(const std::string& host) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        total_requests_++;
        
        // Try to find a connection for this host
        for (auto it = available_.begin(); it != available_.end(); ++it) {
            if ((*it)->last_host == host || (*it)->last_host.empty()) {
                auto conn = std::move(*it);
                available_.erase(it);
                
                if (conn->last_host == host) {
                    reuse_count_++;
                }
                
                conn->last_used = std::chrono::steady_clock::now();
                conn->use_count++;
                
                CURL* handle = conn->handle;
                in_use_[handle] = std::move(conn);
                
                // Reset for new request but keep connection alive
                curl_easy_reset(handle);
                setupKeepAlive(handle);
                
                return handle;
            }
        }
        
        // Create new connection if pool is empty or no match
        auto conn = std::make_unique<PooledConnection>();
        conn->handle = curl_easy_init();
        if (!conn->handle) {
            return nullptr;
        }
        
        conn->last_used = std::chrono::steady_clock::now();
        conn->use_count = 1;
        conn->last_host = host;
        
        CURL* handle = conn->handle;
        in_use_[handle] = std::move(conn);
        
        setupKeepAlive(handle);
        new_connections_++;
        
        return handle;
    }
    
    // Return connection to pool
    void returnConnection(CURL* handle, const std::string& host) {
        if (!handle) return;
        
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto it = in_use_.find(handle);
        if (it != in_use_.end()) {
            auto conn = std::move(it->second);
            in_use_.erase(it);
            
            conn->last_host = host;
            conn->last_used = std::chrono::steady_clock::now();
            
            // Keep connection if pool not full
            if (available_.size() < max_pool_size_) {
                available_.push_back(std::move(conn));
            }
            // Otherwise clean up oldest connection
            else {
                evictOldest();
                available_.push_back(std::move(conn));
            }
        } else {
            // Not from pool, clean up
            curl_easy_cleanup(handle);
        }
    }
    
    // Get pool statistics
    struct PoolStats {
        size_t total_requests;
        size_t reuse_count;
        size_t new_connections;
        size_t pool_size;
        double reuse_rate;
    };
    
    PoolStats getStats() const {
        std::lock_guard<std::mutex> lock(mutex_);
        PoolStats stats;
        stats.total_requests = total_requests_;
        stats.reuse_count = reuse_count_;
        stats.new_connections = new_connections_;
        stats.pool_size = available_.size() + in_use_.size();
        
        if (total_requests_ > 0) {
            stats.reuse_rate = static_cast<double>(reuse_count_) / total_requests_;
        } else {
            stats.reuse_rate = 0.0;
        }
        
        return stats;
    }
    
    // Clear all connections
    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        available_.clear();
        in_use_.clear();
    }
    
private:
    ConnectionPool() : max_pool_size_(10), total_requests_(0), 
                      reuse_count_(0), new_connections_(0) {}
    
    void setupKeepAlive(CURL* handle) {
        // Enable keep-alive
        curl_easy_setopt(handle, CURLOPT_TCP_KEEPALIVE, 1L);
        curl_easy_setopt(handle, CURLOPT_TCP_KEEPIDLE, 120L);
        curl_easy_setopt(handle, CURLOPT_TCP_KEEPINTVL, 60L);
        
        // Connection reuse
        curl_easy_setopt(handle, CURLOPT_FRESH_CONNECT, 0L);
        curl_easy_setopt(handle, CURLOPT_FORBID_REUSE, 0L);
        
        // DNS caching (1 hour)
        curl_easy_setopt(handle, CURLOPT_DNS_CACHE_TIMEOUT, 3600L);
        
        // HTTP/2 support if available
        curl_easy_setopt(handle, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2_0);
    }
    
    void evictOldest() {
        if (available_.empty()) return;
        
        auto oldest = available_.begin();
        auto oldest_time = (*oldest)->last_used;
        
        for (auto it = available_.begin(); it != available_.end(); ++it) {
            if ((*it)->last_used < oldest_time) {
                oldest = it;
                oldest_time = (*it)->last_used;
            }
        }
        
        available_.erase(oldest);
    }
    
    mutable std::mutex mutex_;
    std::vector<std::unique_ptr<PooledConnection>> available_;
    std::unordered_map<CURL*, std::unique_ptr<PooledConnection>> in_use_;
    size_t max_pool_size_;
    std::atomic<size_t> total_requests_;
    std::atomic<size_t> reuse_count_;
    std::atomic<size_t> new_connections_;
};

// ========================================================================
// DNS Cache Management
// ========================================================================

class DNSCache {
public:
    struct DNSEntry {
        std::string ip_address;
        std::chrono::steady_clock::time_point cached_at;
        size_t hit_count;
    };
    
    static DNSCache& getInstance() {
        static DNSCache instance;
        return instance;
    }
    
    std::string resolve(const std::string& hostname) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto it = cache_.find(hostname);
        if (it != cache_.end()) {
            auto age = std::chrono::steady_clock::now() - it->second.cached_at;
            if (age < cache_ttl_) {
                it->second.hit_count++;
                cache_hits_++;
                return it->second.ip_address;
            }
        }
        
        // DNS resolution happens in CURL
        cache_misses_++;
        return "";
    }
    
    void cache(const std::string& hostname, const std::string& ip) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        DNSEntry entry;
        entry.ip_address = ip;
        entry.cached_at = std::chrono::steady_clock::now();
        entry.hit_count = 0;
        
        cache_[hostname] = entry;
        
        // Limit cache size
        if (cache_.size() > max_cache_size_) {
            evictOldest();
        }
    }
    
    double getHitRate() const {
        std::lock_guard<std::mutex> lock(mutex_);
        size_t total = cache_hits_ + cache_misses_;
        return total > 0 ? static_cast<double>(cache_hits_) / total : 0.0;
    }
    
private:
    DNSCache() : max_cache_size_(100), cache_hits_(0), cache_misses_(0),
                cache_ttl_(std::chrono::hours(1)) {}
    
    void evictOldest() {
        if (cache_.empty()) return;
        
        auto oldest = cache_.begin();
        for (auto it = cache_.begin(); it != cache_.end(); ++it) {
            if (it->second.cached_at < oldest->second.cached_at) {
                oldest = it;
            }
        }
        cache_.erase(oldest);
    }
    
    mutable std::mutex mutex_;
    std::unordered_map<std::string, DNSEntry> cache_;
    size_t max_cache_size_;
    std::atomic<size_t> cache_hits_;
    std::atomic<size_t> cache_misses_;
    std::chrono::seconds cache_ttl_;
};

// ========================================================================
// Optimized JSON Parsing
// ========================================================================

class FastJSONParser {
public:
    static FastJSONParser& getInstance() {
        static FastJSONParser instance;
        return instance;
    }
    
    // Parse JWKS response efficiently (simplified JSON parsing)
    bool parseJWKS(const std::string& json, 
                  std::vector<std::pair<std::string, std::string>>& keys) {
        // Simple JSON parsing without external library
        // In production, would use RapidJSON for better performance
        
        // Find "keys" array
        size_t keys_pos = json.find("\"keys\"");
        if (keys_pos == std::string::npos) return false;
        
        size_t array_start = json.find('[', keys_pos);
        if (array_start == std::string::npos) return false;
        
        size_t array_end = json.find(']', array_start);
        if (array_end == std::string::npos) return false;
        
        // Extract each key object
        size_t pos = array_start + 1;
        while (pos < array_end) {
            size_t obj_start = json.find('{', pos);
            if (obj_start == std::string::npos || obj_start >= array_end) break;
            
            size_t obj_end = json.find('}', obj_start);
            if (obj_end == std::string::npos || obj_end >= array_end) break;
            
            std::string obj = json.substr(obj_start, obj_end - obj_start + 1);
            
            // Extract kid
            std::string kid;
            size_t kid_pos = obj.find("\"kid\"");
            if (kid_pos != std::string::npos) {
                size_t value_start = obj.find('\"', kid_pos + 5);
                if (value_start != std::string::npos) {
                    size_t value_end = obj.find('\"', value_start + 1);
                    if (value_end != std::string::npos) {
                        kid = obj.substr(value_start + 1, value_end - value_start - 1);
                    }
                }
            }
            
            // Extract x5c certificate
            std::string cert;
            size_t x5c_pos = obj.find("\"x5c\"");
            if (x5c_pos != std::string::npos) {
                size_t cert_start = obj.find('\"', obj.find('[', x5c_pos));
                if (cert_start != std::string::npos) {
                    size_t cert_end = obj.find('\"', cert_start + 1);
                    if (cert_end != std::string::npos) {
                        cert = obj.substr(cert_start + 1, cert_end - cert_start - 1);
                    }
                }
            }
            
            if (!kid.empty() && !cert.empty()) {
                std::string pem = "-----BEGIN CERTIFICATE-----\n" + cert + 
                                 "\n-----END CERTIFICATE-----";
                keys.emplace_back(std::move(kid), std::move(pem));
            }
            
            pos = obj_end + 1;
        }
        
        return !keys.empty();
    }
    
    // Parse JWT header efficiently  
    bool parseJWTHeader(const std::string& json,
                       std::string& alg, std::string& kid) {
        // Extract alg
        size_t alg_pos = json.find("\"alg\"");
        if (alg_pos != std::string::npos) {
            size_t value_start = json.find('\"', alg_pos + 5);
            if (value_start != std::string::npos) {
                size_t value_end = json.find('\"', value_start + 1);
                if (value_end != std::string::npos) {
                    alg = json.substr(value_start + 1, value_end - value_start - 1);
                }
            }
        }
        
        // Extract kid
        size_t kid_pos = json.find("\"kid\"");
        if (kid_pos != std::string::npos) {
            size_t value_start = json.find('\"', kid_pos + 5);
            if (value_start != std::string::npos) {
                size_t value_end = json.find('\"', value_start + 1);
                if (value_end != std::string::npos) {
                    kid = json.substr(value_start + 1, value_end - value_start - 1);
                }
            }
        }
        
        return !alg.empty();
    }
};

// ========================================================================
// Optimized HTTP Client
// ========================================================================

struct HTTPResponse {
    std::string body;
    long status_code;
    std::chrono::milliseconds latency;
};

class OptimizedHTTPClient {
public:
    static OptimizedHTTPClient& getInstance() {
        static OptimizedHTTPClient instance;
        return instance;
    }
    
    // Optimized JWKS fetch with all optimizations
    bool fetchJWKS(const std::string& url, HTTPResponse& response) {
        auto start = std::chrono::high_resolution_clock::now();
        
        // Extract host from URL
        std::string host = extractHost(url);
        
        // Get pooled connection
        CURL* curl = ConnectionPool::getInstance().borrowConnection(host);
        if (!curl) {
            return false;
        }
        
        // Response buffer
        std::string response_buffer;
        response_buffer.reserve(8192); // Pre-allocate for typical JWKS size
        
        // Set URL and options
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_buffer);
        
        // Optimizations
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 3L);
        
        // SSL optimizations
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
        curl_easy_setopt(curl, CURLOPT_SSL_SESSIONID_CACHE, 1L);
        
        // Headers for keep-alive
        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, "Connection: keep-alive");
        headers = curl_slist_append(headers, "Accept: application/json");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        
        // Perform request
        CURLcode res = curl_easy_perform(curl);
        
        // Get status code
        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        
        // Clean up headers
        curl_slist_free_all(headers);
        
        // Return connection to pool
        ConnectionPool::getInstance().returnConnection(curl, host);
        
        auto end = std::chrono::high_resolution_clock::now();
        response.latency = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        response.status_code = http_code;
        response.body = std::move(response_buffer);
        
        recordMetrics(response.latency.count(), res == CURLE_OK);
        
        return res == CURLE_OK && http_code == 200;
    }
    
    // Get performance metrics
    struct Metrics {
        size_t total_requests;
        size_t successful_requests;
        double avg_latency_ms;
        double min_latency_ms;
        double max_latency_ms;
        double success_rate;
    };
    
    Metrics getMetrics() const {
        std::lock_guard<std::mutex> lock(metrics_mutex_);
        Metrics m;
        m.total_requests = total_requests_;
        m.successful_requests = successful_requests_;
        
        if (!latencies_.empty()) {
            long total = 0;
            m.min_latency_ms = latencies_[0];
            m.max_latency_ms = latencies_[0];
            
            for (long lat : latencies_) {
                total += lat;
                if (lat < m.min_latency_ms) m.min_latency_ms = lat;
                if (lat > m.max_latency_ms) m.max_latency_ms = lat;
            }
            
            m.avg_latency_ms = static_cast<double>(total) / latencies_.size();
        } else {
            m.avg_latency_ms = 0;
            m.min_latency_ms = 0;
            m.max_latency_ms = 0;
        }
        
        m.success_rate = m.total_requests > 0 ? 
            static_cast<double>(m.successful_requests) / m.total_requests : 0;
        
        return m;
    }
    
private:
    OptimizedHTTPClient() : total_requests_(0), successful_requests_(0) {
        curl_global_init(CURL_GLOBAL_ALL);
    }
    
    ~OptimizedHTTPClient() {
        ConnectionPool::getInstance().clear();
        curl_global_cleanup();
    }
    
    static size_t writeCallback(void* contents, size_t size, size_t nmemb, void* userp) {
        size_t total_size = size * nmemb;
        std::string* response = static_cast<std::string*>(userp);
        response->append(static_cast<char*>(contents), total_size);
        return total_size;
    }
    
    std::string extractHost(const std::string& url) {
        size_t start = url.find("://");
        if (start == std::string::npos) return "";
        start += 3;
        
        size_t end = url.find('/', start);
        if (end == std::string::npos) {
            return url.substr(start);
        }
        return url.substr(start, end - start);
    }
    
    void recordMetrics(long latency_ms, bool success) {
        std::lock_guard<std::mutex> lock(metrics_mutex_);
        total_requests_++;
        if (success) successful_requests_++;
        
        latencies_.push_back(latency_ms);
        if (latencies_.size() > 1000) {
            latencies_.erase(latencies_.begin());
        }
    }
    
    mutable std::mutex metrics_mutex_;
    std::atomic<size_t> total_requests_;
    std::atomic<size_t> successful_requests_;
    std::vector<long> latencies_;
};

// ========================================================================
// Public Interface
// ========================================================================

extern "C" {

bool mcp_auth_fetch_jwks_optimized(
    const char* jwks_uri,
    char** response_json,
    long* status_code,
    long* latency_ms) {
    
    if (!jwks_uri || !response_json) {
        return false;
    }
    
    HTTPResponse response;
    bool success = OptimizedHTTPClient::getInstance().fetchJWKS(jwks_uri, response);
    
    if (success) {
        *response_json = strdup(response.body.c_str());
        if (status_code) *status_code = response.status_code;
        if (latency_ms) *latency_ms = response.latency.count();
    }
    
    return success;
}

bool mcp_auth_parse_jwks_optimized(
    const char* jwks_json,
    char*** kids,
    char*** certificates,
    size_t* count) {
    
    if (!jwks_json || !kids || !certificates || !count) {
        return false;
    }
    
    std::vector<std::pair<std::string, std::string>> keys;
    if (!FastJSONParser::getInstance().parseJWKS(jwks_json, keys)) {
        return false;
    }
    
    *count = keys.size();
    *kids = (char**)malloc(sizeof(char*) * keys.size());
    *certificates = (char**)malloc(sizeof(char*) * keys.size());
    
    for (size_t i = 0; i < keys.size(); i++) {
        (*kids)[i] = strdup(keys[i].first.c_str());
        (*certificates)[i] = strdup(keys[i].second.c_str());
    }
    
    return true;
}

bool mcp_auth_get_network_stats(
    size_t* total_requests,
    size_t* connection_reuses,
    double* reuse_rate,
    double* dns_hit_rate,
    double* avg_latency_ms) {
    
    auto pool_stats = ConnectionPool::getInstance().getStats();
    auto metrics = OptimizedHTTPClient::getInstance().getMetrics();
    
    if (total_requests) *total_requests = pool_stats.total_requests;
    if (connection_reuses) *connection_reuses = pool_stats.reuse_count;
    if (reuse_rate) *reuse_rate = pool_stats.reuse_rate;
    if (dns_hit_rate) *dns_hit_rate = DNSCache::getInstance().getHitRate();
    if (avg_latency_ms) *avg_latency_ms = metrics.avg_latency_ms;
    
    return true;
}

void mcp_auth_clear_connection_pool() {
    ConnectionPool::getInstance().clear();
}

void mcp_auth_set_pool_size(size_t size) {
    // Would need to add this capability to ConnectionPool
}

} // extern "C"

} // namespace network_optimized