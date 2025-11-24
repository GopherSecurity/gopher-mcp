#include "mcp/auth/memory_cache.h"
#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include <random>

namespace mcp {
namespace auth {
namespace {

class MemoryCacheTest : public ::testing::Test {
protected:
  void SetUp() override {
    cache_ = std::make_unique<MemoryCache<std::string, int, std::hash<std::string>>>(100, std::chrono::seconds(10));
  }
  
  void TearDown() override {
    cache_.reset();
  }
  
  std::unique_ptr<MemoryCache<std::string, int, std::hash<std::string>>> cache_;
};

// Test basic insertion and retrieval
TEST_F(MemoryCacheTest, BasicPutAndGet) {
  cache_->put("key1", 100);
  cache_->put("key2", 200);
  cache_->put("key3", 300);
  
  auto val1 = cache_->get("key1");
  auto val2 = cache_->get("key2");
  auto val3 = cache_->get("key3");
  auto val4 = cache_->get("nonexistent");
  
  ASSERT_TRUE(val1.has_value());
  EXPECT_EQ(val1.value(), 100);
  
  ASSERT_TRUE(val2.has_value());
  EXPECT_EQ(val2.value(), 200);
  
  ASSERT_TRUE(val3.has_value());
  EXPECT_EQ(val3.value(), 300);
  
  EXPECT_FALSE(val4.has_value());
  
  EXPECT_EQ(cache_->size(), 3);
}

// Test cache update (key already exists)
TEST_F(MemoryCacheTest, UpdateExistingKey) {
  cache_->put("key1", 100);
  EXPECT_EQ(cache_->size(), 1);
  
  auto val = cache_->get("key1");
  ASSERT_TRUE(val.has_value());
  EXPECT_EQ(val.value(), 100);
  
  // Update the value
  cache_->put("key1", 200);
  EXPECT_EQ(cache_->size(), 1); // Size shouldn't change
  
  val = cache_->get("key1");
  ASSERT_TRUE(val.has_value());
  EXPECT_EQ(val.value(), 200); // Value should be updated
}

// Test TTL expiration
TEST_F(MemoryCacheTest, TTLExpiration) {
  // Insert with 1 second TTL
  cache_->put("key1", 100, std::chrono::seconds(1));
  
  // Value should be available immediately
  auto val = cache_->get("key1");
  ASSERT_TRUE(val.has_value());
  EXPECT_EQ(val.value(), 100);
  
  // Wait for expiration
  std::this_thread::sleep_for(std::chrono::milliseconds(1100));
  
  // Value should be expired
  val = cache_->get("key1");
  EXPECT_FALSE(val.has_value());
  
  // Size should be 0 after expired entry is removed by get()
  EXPECT_EQ(cache_->size(), 0);
}

// Test LRU eviction policy
TEST_F(MemoryCacheTest, LRUEviction) {
  // Create cache with capacity 3
  MemoryCache<int, std::string, std::hash<int>> small_cache(3);
  
  small_cache.put(1, "one");
  small_cache.put(2, "two");
  small_cache.put(3, "three");
  
  EXPECT_EQ(small_cache.size(), 3);
  
  // Access key 1 to make it recently used
  auto val = small_cache.get(1);
  ASSERT_TRUE(val.has_value());
  
  // Add a fourth item - should evict key 2 (LRU)
  small_cache.put(4, "four");
  
  EXPECT_EQ(small_cache.size(), 3);
  EXPECT_TRUE(small_cache.get(1).has_value()); // Still present (recently used)
  EXPECT_FALSE(small_cache.get(2).has_value()); // Evicted (LRU)
  EXPECT_TRUE(small_cache.get(3).has_value()); // Still present
  EXPECT_TRUE(small_cache.get(4).has_value()); // New entry
}

// Test remove operation
TEST_F(MemoryCacheTest, RemoveEntry) {
  cache_->put("key1", 100);
  cache_->put("key2", 200);
  
  EXPECT_EQ(cache_->size(), 2);
  
  bool removed = cache_->remove("key1");
  EXPECT_TRUE(removed);
  EXPECT_EQ(cache_->size(), 1);
  
  auto val = cache_->get("key1");
  EXPECT_FALSE(val.has_value());
  
  val = cache_->get("key2");
  ASSERT_TRUE(val.has_value());
  EXPECT_EQ(val.value(), 200);
  
  // Try to remove non-existent key
  removed = cache_->remove("nonexistent");
  EXPECT_FALSE(removed);
}

// Test clear operation
TEST_F(MemoryCacheTest, ClearCache) {
  cache_->put("key1", 100);
  cache_->put("key2", 200);
  cache_->put("key3", 300);
  
  EXPECT_EQ(cache_->size(), 3);
  EXPECT_FALSE(cache_->empty());
  
  cache_->clear();
  
  EXPECT_EQ(cache_->size(), 0);
  EXPECT_TRUE(cache_->empty());
  
  EXPECT_FALSE(cache_->get("key1").has_value());
  EXPECT_FALSE(cache_->get("key2").has_value());
  EXPECT_FALSE(cache_->get("key3").has_value());
}

// Test thread safety with concurrent operations
TEST_F(MemoryCacheTest, ThreadSafety) {
  const int num_threads = 10;
  const int ops_per_thread = 1000;
  
  std::vector<std::thread> threads;
  
  // Writer threads
  for (int t = 0; t < num_threads / 2; ++t) {
    threads.emplace_back([this, t, ops_per_thread]() {
      std::random_device rd;
      std::mt19937 gen(rd());
      std::uniform_int_distribution<> dis(0, 99);
      
      for (int i = 0; i < ops_per_thread; ++i) {
        std::string key = "key" + std::to_string(dis(gen));
        int value = t * 1000 + i;
        cache_->put(key, value);
      }
    });
  }
  
  // Reader threads
  for (int t = 0; t < num_threads / 2; ++t) {
    threads.emplace_back([this, ops_per_thread]() {
      std::random_device rd;
      std::mt19937 gen(rd());
      std::uniform_int_distribution<> dis(0, 99);
      
      for (int i = 0; i < ops_per_thread; ++i) {
        std::string key = "key" + std::to_string(dis(gen));
        auto val = cache_->get(key);
        // Just access the value, don't assert (may or may not exist)
        if (val.has_value()) {
          [[maybe_unused]] int v = val.value();
        }
      }
    });
  }
  
  // Wait for all threads
  for (auto& t : threads) {
    t.join();
  }
  
  // Cache should still be consistent
  EXPECT_LE(cache_->size(), 100); // Should respect max size
  EXPECT_EQ(cache_->capacity(), 100);
}

// Test evict_expired functionality
TEST_F(MemoryCacheTest, EvictExpired) {
  // Add entries with different TTLs
  cache_->put("key1", 100, std::chrono::seconds(1));
  cache_->put("key2", 200, std::chrono::seconds(2));
  cache_->put("key3", 300, std::chrono::seconds(3));
  cache_->put("key4", 400, std::chrono::seconds(10));
  
  EXPECT_EQ(cache_->size(), 4);
  
  // No entries should be expired yet
  size_t evicted = cache_->evict_expired();
  EXPECT_EQ(evicted, 0);
  EXPECT_EQ(cache_->size(), 4);
  
  // Wait for some entries to expire
  std::this_thread::sleep_for(std::chrono::milliseconds(1100));
  
  evicted = cache_->evict_expired();
  EXPECT_EQ(evicted, 1); // key1 should be expired
  EXPECT_EQ(cache_->size(), 3);
  
  // Check that the right entries remain
  EXPECT_FALSE(cache_->get("key1").has_value());
  EXPECT_TRUE(cache_->get("key2").has_value());
  EXPECT_TRUE(cache_->get("key3").has_value());
  EXPECT_TRUE(cache_->get("key4").has_value());
}

// Test cache statistics
TEST_F(MemoryCacheTest, CacheStats) {
  cache_->put("key1", 100, std::chrono::seconds(1));
  cache_->put("key2", 200, std::chrono::seconds(10));
  cache_->put("key3", 300, std::chrono::seconds(10));
  
  auto stats = cache_->get_stats();
  EXPECT_EQ(stats.size, 3);
  EXPECT_EQ(stats.capacity, 100);
  EXPECT_EQ(stats.expired_count, 0);
  
  // Wait for one entry to expire
  std::this_thread::sleep_for(std::chrono::milliseconds(1100));
  
  stats = cache_->get_stats();
  EXPECT_EQ(stats.size, 3); // Still 3 entries (not removed yet)
  EXPECT_EQ(stats.expired_count, 1); // But 1 is expired
}

// Test default TTL modification
TEST_F(MemoryCacheTest, SetDefaultTTL) {
  // Set a very short default TTL
  cache_->set_default_ttl(std::chrono::seconds(1));
  
  // Add entry without specifying TTL (should use default)
  cache_->put("key1", 100);
  
  // Value should be available immediately
  EXPECT_TRUE(cache_->get("key1").has_value());
  
  // Wait for expiration
  std::this_thread::sleep_for(std::chrono::milliseconds(1100));
  
  // Value should be expired
  EXPECT_FALSE(cache_->get("key1").has_value());
}

// Test LRU ordering with get operations
TEST_F(MemoryCacheTest, LRUOrderingWithGet) {
  MemoryCache<int, std::string, std::hash<int>> small_cache(3);
  
  small_cache.put(1, "one");
  small_cache.put(2, "two");
  small_cache.put(3, "three");
  
  // Access in order: 1, 2, 3
  small_cache.get(1);
  small_cache.get(2);
  small_cache.get(3);
  
  // Now order should be 3, 2, 1 (most recent to least recent)
  // Adding a new entry should evict 1
  small_cache.put(4, "four");
  
  EXPECT_FALSE(small_cache.get(1).has_value()); // Evicted
  EXPECT_TRUE(small_cache.get(2).has_value());
  EXPECT_TRUE(small_cache.get(3).has_value());
  EXPECT_TRUE(small_cache.get(4).has_value());
}

// Test with complex key and value types
TEST_F(MemoryCacheTest, ComplexTypes) {
  struct ComplexKey {
    int id;
    std::string name;
    
    bool operator==(const ComplexKey& other) const {
      return id == other.id && name == other.name;
    }
  };
  
  struct ComplexKeyHash {
    std::size_t operator()(const ComplexKey& k) const {
      return std::hash<int>()(k.id) ^ (std::hash<std::string>()(k.name) << 1);
    }
  };
  
  struct ComplexValue {
    std::vector<int> data;
    std::string description;
  };
  
  MemoryCache<ComplexKey, ComplexValue, ComplexKeyHash> complex_cache(10);
  
  ComplexKey key1{1, "first"};
  ComplexValue val1{{1, 2, 3}, "first value"};
  
  ComplexKey key2{2, "second"};
  ComplexValue val2{{4, 5, 6}, "second value"};
  
  complex_cache.put(key1, val1);
  complex_cache.put(key2, val2);
  
  auto retrieved = complex_cache.get(key1);
  ASSERT_TRUE(retrieved.has_value());
  EXPECT_EQ(retrieved.value().data.size(), 3);
  EXPECT_EQ(retrieved.value().description, "first value");
  
  retrieved = complex_cache.get(key2);
  ASSERT_TRUE(retrieved.has_value());
  EXPECT_EQ(retrieved.value().data.size(), 3);
  EXPECT_EQ(retrieved.value().description, "second value");
}

// Performance test with large cache
TEST_F(MemoryCacheTest, PerformanceWithLargeCache) {
  MemoryCache<int, std::string, std::hash<int>> large_cache(10000);
  
  // Insert many entries
  auto start = std::chrono::high_resolution_clock::now();
  for (int i = 0; i < 10000; ++i) {
    large_cache.put(i, "value" + std::to_string(i));
  }
  auto end = std::chrono::high_resolution_clock::now();
  
  auto insert_duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
  EXPECT_LT(insert_duration.count(), 1000); // Should complete within 1 second
  
  // Random access test
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<> dis(0, 9999);
  
  start = std::chrono::high_resolution_clock::now();
  for (int i = 0; i < 10000; ++i) {
    auto val = large_cache.get(dis(gen));
    EXPECT_TRUE(val.has_value());
  }
  end = std::chrono::high_resolution_clock::now();
  
  auto access_duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
  EXPECT_LT(access_duration.count(), 100); // 10000 random accesses should be fast
}

} // namespace
} // namespace auth
} // namespace mcp