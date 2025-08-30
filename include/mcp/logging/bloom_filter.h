#pragma once

#include <vector>
#include <functional>
#include <algorithm>

namespace mcp {
namespace logging {

// Bloom filter for fast log filtering
template<typename T>
class BloomFilter {
public:
  explicit BloomFilter(size_t size = 1024, size_t num_hashes = 3)
    : bits_(size), num_hashes_(num_hashes) {}
  
  void add(const T& item) {
    for (size_t i = 0; i < num_hashes_; ++i) {
      size_t hash = hash_function(item, i) % bits_.size();
      bits_[hash] = true;
    }
  }
  
  bool mayContain(const T& item) const {
    for (size_t i = 0; i < num_hashes_; ++i) {
      size_t hash = hash_function(item, i) % bits_.size();
      if (!bits_[hash]) {
        return false; // Definitely not in set
      }
    }
    return true; // Maybe in set
  }
  
  void clear() {
    std::fill(bits_.begin(), bits_.end(), false);
  }
  
  size_t size() const { return bits_.size(); }
  size_t numHashes() const { return num_hashes_; }
  
private:
  std::vector<bool> bits_;
  size_t num_hashes_;
  
  size_t hash_function(const T& item, size_t seed) const {
    // MurmurHash-inspired mixing
    size_t h = std::hash<T>{}(item);
    h ^= seed * 0x9e3779b9;
    h ^= (h >> 16);
    h *= 0x85ebca6b;
    h ^= (h >> 13);
    h *= 0xc2b2ae35;
    h ^= (h >> 16);
    return h;
  }
};

} // namespace logging
} // namespace mcp