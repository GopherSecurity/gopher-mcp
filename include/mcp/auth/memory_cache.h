#ifndef MCP_AUTH_MEMORY_CACHE_H
#define MCP_AUTH_MEMORY_CACHE_H

#include <chrono>
#include <list>
#include <memory>
#include <mutex>
#include <unordered_map>
#include "mcp/core/optional.h"  // Use MCP's optional implementation

/**
 * @file memory_cache.h
 * @brief Thread-safe LRU cache with TTL support for authentication module
 */

namespace mcp {
namespace auth {

/**
 * @brief Thread-safe LRU cache with TTL support
 * @tparam Key The key type
 * @tparam Value The value type
 * @tparam Hash The hash function for the key type
 */
template <typename Key, typename Value, typename Hash = std::hash<Key>>
class MemoryCache {
public:
  struct CacheEntry {
    Value value;
    std::chrono::steady_clock::time_point expiry;
  };

  /**
   * @brief Construct a new cache with specified capacity and default TTL
   * @param max_size Maximum number of entries in the cache
   * @param default_ttl Default time-to-live for cache entries
   */
  explicit MemoryCache(size_t max_size = 1000,
                        std::chrono::seconds default_ttl = std::chrono::seconds(3600))
      : max_size_(max_size), default_ttl_(default_ttl) {}

  /**
   * @brief Insert or update an entry in the cache
   * @param key The key to insert
   * @param value The value to associate with the key
   * @param ttl Optional custom TTL for this entry
   */
  void put(const Key& key, const Value& value,
           mcp::optional<std::chrono::seconds> ttl = mcp::nullopt) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto now = std::chrono::steady_clock::now();
    auto entry_ttl = ttl.value_or(default_ttl_);
    auto expiry = now + entry_ttl;
    
    // Remove existing entry if present
    auto map_it = cache_map_.find(key);
    if (map_it != cache_map_.end()) {
      lru_list_.erase(map_it->second.list_iterator);
      cache_map_.erase(map_it);
    }
    
    // Add new entry to front of LRU list
    lru_list_.push_front(key);
    cache_map_[key] = {lru_list_.begin(), CacheEntry{value, expiry}};
    
    // Evict LRU entries if cache is full
    while (cache_map_.size() > max_size_) {
      evict_lru();
    }
  }

  /**
   * @brief Get a value from the cache
   * @param key The key to look up
   * @return The value if found and not expired, nullopt otherwise
   */
  mcp::optional<Value> get(const Key& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = cache_map_.find(key);
    if (it == cache_map_.end()) {
      return mcp::nullopt;
    }
    
    auto now = std::chrono::steady_clock::now();
    if (now >= it->second.entry.expiry) {
      // Entry has expired
      lru_list_.erase(it->second.list_iterator);
      cache_map_.erase(it);
      return mcp::nullopt;
    }
    
    // Move to front of LRU list
    lru_list_.splice(lru_list_.begin(), lru_list_, it->second.list_iterator);
    it->second.list_iterator = lru_list_.begin();
    
    return it->second.entry.value;
  }

  /**
   * @brief Remove an entry from the cache
   * @param key The key to remove
   * @return true if the entry was removed, false if not found
   */
  bool remove(const Key& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = cache_map_.find(key);
    if (it == cache_map_.end()) {
      return false;
    }
    
    lru_list_.erase(it->second.list_iterator);
    cache_map_.erase(it);
    return true;
  }

  /**
   * @brief Clear all entries from the cache
   */
  void clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    cache_map_.clear();
    lru_list_.clear();
  }

  /**
   * @brief Get the current size of the cache
   * @return Number of entries in the cache
   */
  size_t size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return cache_map_.size();
  }

  /**
   * @brief Check if the cache is empty
   * @return true if cache is empty, false otherwise
   */
  bool empty() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return cache_map_.empty();
  }

  /**
   * @brief Get the maximum capacity of the cache
   * @return Maximum number of entries
   */
  size_t capacity() const {
    return max_size_;
  }

  /**
   * @brief Remove all expired entries from the cache
   * @return Number of entries removed
   */
  size_t evict_expired() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto now = std::chrono::steady_clock::now();
    size_t evicted = 0;
    
    auto it = cache_map_.begin();
    while (it != cache_map_.end()) {
      if (now >= it->second.entry.expiry) {
        lru_list_.erase(it->second.list_iterator);
        it = cache_map_.erase(it);
        ++evicted;
      } else {
        ++it;
      }
    }
    
    return evicted;
  }

  /**
   * @brief Set the default TTL for new entries
   * @param ttl New default TTL
   */
  void set_default_ttl(std::chrono::seconds ttl) {
    std::lock_guard<std::mutex> lock(mutex_);
    default_ttl_ = ttl;
  }

  /**
   * @brief Get cache statistics
   */
  struct CacheStats {
    size_t size;
    size_t capacity;
    size_t expired_count;
  };

  CacheStats get_stats() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto now = std::chrono::steady_clock::now();
    size_t expired = 0;
    
    for (const auto& item : cache_map_) {
      if (now >= item.second.entry.expiry) {
        ++expired;
      }
    }
    
    return CacheStats{
        .size = cache_map_.size(),
        .capacity = max_size_,
        .expired_count = expired
    };
  }

private:
  struct CacheData {
    typename std::list<Key>::iterator list_iterator;
    CacheEntry entry;
  };

  void evict_lru() {
    if (!lru_list_.empty()) {
      auto key = lru_list_.back();
      lru_list_.pop_back();
      cache_map_.erase(key);
    }
  }

  mutable std::mutex mutex_;
  size_t max_size_;
  std::chrono::seconds default_ttl_;
  std::list<Key> lru_list_; // Front = most recently used, Back = least recently used
  std::unordered_map<Key, CacheData, Hash> cache_map_;
};

} // namespace auth
} // namespace mcp

#endif // MCP_AUTH_MEMORY_CACHE_H