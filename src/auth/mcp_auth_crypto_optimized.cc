/**
 * @file mcp_c_auth_api_crypto_optimized.cc
 * @brief Optimized cryptographic operations for JWT validation
 * 
 * Implements performance optimizations for signature verification
 */

#ifdef USE_CPP11_COMPAT
#include "cpp11_compat.h"
#endif

#include <openssl/evp.h>
#include <openssl/rsa.h>
#include <openssl/pem.h>
#include <openssl/bio.h>
#include <openssl/err.h>
#include <string>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <vector>
#include <chrono>
#include <atomic>

namespace crypto_optimized {

// ========================================================================
// Optimized Certificate Cache
// ========================================================================

struct ParsedKey {
    EVP_PKEY* pkey;
    std::chrono::steady_clock::time_point parse_time;
    size_t use_count;
    
    ParsedKey() : pkey(nullptr), use_count(0) {}
    
    ~ParsedKey() {
        if (pkey) {
            EVP_PKEY_free(pkey);
        }
    }
};

class CertificateCache {
public:
    static CertificateCache& getInstance() {
        static CertificateCache instance;
        return instance;
    }
    
    // Get or parse a public key
    EVP_PKEY* getKey(const std::string& pem_key) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        total_requests_++;
        
        // Check cache first
        auto it = cache_.find(pem_key);
        if (it != cache_.end()) {
            it->second->use_count++;
            cache_hits_++;
            return it->second->pkey;
        }
        
        // Parse new key
        auto parsed = parseKey(pem_key);
        if (parsed) {
            auto key_ptr = std::make_unique<ParsedKey>();
            key_ptr->pkey = parsed;
            key_ptr->parse_time = std::chrono::steady_clock::now();
            key_ptr->use_count = 1;
            
            cache_[pem_key] = std::move(key_ptr);
            
            // Limit cache size
            if (cache_.size() > max_cache_size_) {
                evictOldest();
            }
            
            return parsed;
        }
        
        return nullptr;
    }
    
    // Clear cache
    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        cache_.clear();
    }
    
    // Get cache statistics
    struct CacheStats {
        size_t entries;
        size_t total_uses;
        double hit_rate;
    };
    
    CacheStats getStats() const {
        std::lock_guard<std::mutex> lock(mutex_);
        CacheStats stats;
        stats.entries = cache_.size();
        stats.total_uses = 0;
        
        for (const auto& entry : cache_) {
            stats.total_uses += entry.second->use_count;
        }
        
        // Calculate hit rate (approximate)
        if (total_requests_ > 0) {
            stats.hit_rate = static_cast<double>(cache_hits_) / total_requests_;
        } else {
            stats.hit_rate = 0.0;
        }
        
        return stats;
    }
    
private:
    CertificateCache() : max_cache_size_(100), cache_hits_(0), total_requests_(0) {}
    
    EVP_PKEY* parseKey(const std::string& pem_key) {
        BIO* bio = BIO_new_mem_buf(pem_key.c_str(), -1);
        if (!bio) return nullptr;
        
        EVP_PKEY* pkey = PEM_read_bio_PUBKEY(bio, nullptr, nullptr, nullptr);
        BIO_free(bio);
        
        return pkey;
    }
    
    void evictOldest() {
        if (cache_.empty()) return;
        
        // Find least recently used entry
        auto oldest = cache_.begin();
        for (auto it = cache_.begin(); it != cache_.end(); ++it) {
            if (it->second->use_count < oldest->second->use_count) {
                oldest = it;
            }
        }
        
        cache_.erase(oldest);
    }
    
    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::unique_ptr<ParsedKey>> cache_;
    size_t max_cache_size_;
    std::atomic<size_t> cache_hits_;
    std::atomic<size_t> total_requests_;
};

// ========================================================================
// Optimized Verification Context Pool
// ========================================================================

class VerificationContextPool {
public:
    static VerificationContextPool& getInstance() {
        static VerificationContextPool instance;
        return instance;
    }
    
    struct ContextGuard {
        EVP_MD_CTX* ctx;
        VerificationContextPool* pool;
        
        ContextGuard(EVP_MD_CTX* c, VerificationContextPool* p) 
            : ctx(c), pool(p) {}
        
        ~ContextGuard() {
            if (ctx && pool) {
                pool->returnContext(ctx);
            }
        }
        
        EVP_MD_CTX* get() { return ctx; }
        EVP_MD_CTX* release() { 
            EVP_MD_CTX* c = ctx;
            ctx = nullptr;
            return c;
        }
    };
    
    std::unique_ptr<ContextGuard> borrowContext() {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (!pool_.empty()) {
            EVP_MD_CTX* ctx = pool_.back();
            pool_.pop_back();
            EVP_MD_CTX_reset(ctx);  // Reset for reuse
            return std::make_unique<ContextGuard>(ctx, this);
        }
        
        // Create new context if pool is empty
        EVP_MD_CTX* ctx = EVP_MD_CTX_new();
        return std::make_unique<ContextGuard>(ctx, this);
    }
    
    void returnContext(EVP_MD_CTX* ctx) {
        if (!ctx) return;
        
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (pool_.size() < max_pool_size_) {
            EVP_MD_CTX_reset(ctx);
            pool_.push_back(ctx);
        } else {
            EVP_MD_CTX_free(ctx);
        }
    }
    
    ~VerificationContextPool() {
        for (auto ctx : pool_) {
            EVP_MD_CTX_free(ctx);
        }
    }
    
private:
    VerificationContextPool() : max_pool_size_(10) {}
    
    std::mutex mutex_;
    std::vector<EVP_MD_CTX*> pool_;
    size_t max_pool_size_;
};

// ========================================================================
// Optimized Signature Verification
// ========================================================================

bool verify_rsa_signature_optimized(
    const std::string& signing_input,
    const std::string& signature,
    const std::string& public_key_pem,
    const std::string& algorithm) {
    
    // Get cached public key
    EVP_PKEY* pkey = CertificateCache::getInstance().getKey(public_key_pem);
    if (!pkey) {
        return false;
    }
    
    // Get pooled verification context
    auto ctx_guard = VerificationContextPool::getInstance().borrowContext();
    EVP_MD_CTX* md_ctx = ctx_guard->get();
    if (!md_ctx) {
        return false;
    }
    
    // Select hash algorithm (optimized with static lookup)
    static const std::unordered_map<std::string, const EVP_MD*> hash_algos = {
        {"RS256", EVP_sha256()},
        {"RS384", EVP_sha384()},
        {"RS512", EVP_sha512()}
    };
    
    auto algo_it = hash_algos.find(algorithm);
    if (algo_it == hash_algos.end()) {
        return false;
    }
    const EVP_MD* md = algo_it->second;
    
    // Initialize verification
    if (EVP_DigestVerifyInit(md_ctx, nullptr, md, nullptr, pkey) != 1) {
        return false;
    }
    
    // Update with signing input
    if (EVP_DigestVerifyUpdate(md_ctx, signing_input.c_str(), signing_input.length()) != 1) {
        return false;
    }
    
    // Verify signature
    int result = EVP_DigestVerifyFinal(md_ctx, 
                                       reinterpret_cast<const unsigned char*>(signature.c_str()),
                                       signature.length());
    
    return (result == 1);
}

// ========================================================================
// Batch Verification Support
// ========================================================================

class BatchVerifier {
public:
    struct VerificationTask {
        std::string signing_input;
        std::string signature;
        std::string public_key_pem;
        std::string algorithm;
        bool result;
    };
    
    // Verify multiple signatures in parallel
    static void verifyBatch(std::vector<VerificationTask>& tasks) {
        #pragma omp parallel for
        for (size_t i = 0; i < tasks.size(); ++i) {
            tasks[i].result = verify_rsa_signature_optimized(
                tasks[i].signing_input,
                tasks[i].signature,
                tasks[i].public_key_pem,
                tasks[i].algorithm
            );
        }
    }
};

// ========================================================================
// Performance Monitoring
// ========================================================================

class PerformanceMonitor {
public:
    static PerformanceMonitor& getInstance() {
        static PerformanceMonitor instance;
        return instance;
    }
    
    void recordVerification(std::chrono::microseconds duration) {
        std::lock_guard<std::mutex> lock(mutex_);
        verification_times_.push_back(duration);
        
        // Keep only last N measurements
        if (verification_times_.size() > max_samples_) {
            verification_times_.erase(verification_times_.begin());
        }
    }
    
    struct PerformanceStats {
        std::chrono::microseconds avg_time;
        std::chrono::microseconds min_time;
        std::chrono::microseconds max_time;
        size_t sample_count;
        bool sub_millisecond;
    };
    
    PerformanceStats getStats() const {
        std::lock_guard<std::mutex> lock(mutex_);
        PerformanceStats stats;
        
        if (verification_times_.empty()) {
            stats.avg_time = std::chrono::microseconds(0);
            stats.min_time = std::chrono::microseconds(0);
            stats.max_time = std::chrono::microseconds(0);
            stats.sample_count = 0;
            stats.sub_millisecond = false;
            return stats;
        }
        
        // Calculate statistics
        long total = 0;
        stats.min_time = verification_times_[0];
        stats.max_time = verification_times_[0];
        
        for (const auto& time : verification_times_) {
            total += time.count();
            if (time < stats.min_time) stats.min_time = time;
            if (time > stats.max_time) stats.max_time = time;
        }
        
        stats.avg_time = std::chrono::microseconds(total / verification_times_.size());
        stats.sample_count = verification_times_.size();
        stats.sub_millisecond = (stats.avg_time < std::chrono::microseconds(1000));
        
        return stats;
    }
    
private:
    PerformanceMonitor() : max_samples_(1000) {}
    
    mutable std::mutex mutex_;
    std::vector<std::chrono::microseconds> verification_times_;
    size_t max_samples_;
};

// ========================================================================
// Optimized Public Interface
// ========================================================================

extern "C" {

bool mcp_auth_verify_signature_optimized(
    const char* signing_input,
    const char* signature,
    const char* public_key_pem,
    const char* algorithm) {
    
    if (!signing_input || !signature || !public_key_pem || !algorithm) {
        return false;
    }
    
    auto start = std::chrono::high_resolution_clock::now();
    
    bool result = verify_rsa_signature_optimized(
        std::string(signing_input),
        std::string(signature),
        std::string(public_key_pem),
        std::string(algorithm)
    );
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    PerformanceMonitor::getInstance().recordVerification(duration);
    
    return result;
}

void mcp_auth_clear_crypto_cache() {
    CertificateCache::getInstance().clear();
}

bool mcp_auth_get_crypto_performance(
    double* avg_microseconds,
    double* min_microseconds,
    double* max_microseconds,
    bool* is_sub_millisecond) {
    
    auto stats = PerformanceMonitor::getInstance().getStats();
    
    if (stats.sample_count == 0) {
        return false;
    }
    
    if (avg_microseconds) *avg_microseconds = stats.avg_time.count();
    if (min_microseconds) *min_microseconds = stats.min_time.count();
    if (max_microseconds) *max_microseconds = stats.max_time.count();
    if (is_sub_millisecond) *is_sub_millisecond = stats.sub_millisecond;
    
    return true;
}

bool mcp_auth_get_cache_stats(
    size_t* cache_entries,
    size_t* total_uses,
    double* hit_rate) {
    
    auto stats = CertificateCache::getInstance().getStats();
    
    if (cache_entries) *cache_entries = stats.entries;
    if (total_uses) *total_uses = stats.total_uses;
    if (hit_rate) *hit_rate = stats.hit_rate;
    
    return true;
}

} // extern "C"

} // namespace crypto_optimized