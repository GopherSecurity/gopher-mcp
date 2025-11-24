#include "mcp/auth/jwks_client.h"
#include <gtest/gtest.h>
#include <thread>
#include <chrono>

namespace mcp {
namespace auth {
namespace {

class JwksClientTest : public ::testing::Test {
protected:
  void SetUp() override {
    // Use Google's public JWKS endpoint for testing
    config_.jwks_uri = "https://www.googleapis.com/oauth2/v3/certs";
    config_.default_cache_duration = std::chrono::seconds(30);
    config_.min_cache_duration = std::chrono::seconds(10);
    config_.max_cache_duration = std::chrono::seconds(3600);
  }
  
  JwksClientConfig config_;
};

// Test JSON parsing
TEST_F(JwksClientTest, ParseValidJwks) {
  std::string valid_jwks = R"({
    "keys": [
      {
        "kid": "test-key-1",
        "kty": "RSA",
        "use": "sig",
        "alg": "RS256",
        "n": "xGOr-H7A-PWG3z",
        "e": "AQAB"
      },
      {
        "kid": "test-key-2",
        "kty": "EC",
        "use": "sig",
        "alg": "ES256",
        "crv": "P-256",
        "x": "WKn-ZIGevcwG",
        "y": "IueRXDLkwZkj"
      }
    ]
  })";
  
  auto response = JwksClient::parse_jwks(valid_jwks);
  ASSERT_TRUE(response.has_value());
  EXPECT_EQ(response.value().keys.size(), 2);
  
  // Check first key (RSA)
  EXPECT_EQ(response.value().keys[0].kid, "test-key-1");
  EXPECT_EQ(response.value().keys[0].kty, "RSA");
  EXPECT_EQ(response.value().keys[0].get_key_type(), JsonWebKey::KeyType::RSA);
  EXPECT_TRUE(response.value().keys[0].is_valid());
  
  // Check second key (EC)
  EXPECT_EQ(response.value().keys[1].kid, "test-key-2");
  EXPECT_EQ(response.value().keys[1].kty, "EC");
  EXPECT_EQ(response.value().keys[1].get_key_type(), JsonWebKey::KeyType::EC);
  EXPECT_TRUE(response.value().keys[1].is_valid());
}

// Test invalid JSON parsing
TEST_F(JwksClientTest, ParseInvalidJwks) {
  std::string invalid_jwks = "not valid json";
  auto response = JwksClient::parse_jwks(invalid_jwks);
  EXPECT_FALSE(response.has_value());
  
  std::string missing_keys = R"({"not_keys": []})";
  response = JwksClient::parse_jwks(missing_keys);
  EXPECT_FALSE(response.has_value());
}

// Test cache-control parsing
TEST_F(JwksClientTest, ParseCacheControl) {
  auto duration = JwksClient::parse_cache_control("max-age=3600");
  EXPECT_EQ(duration.count(), 3600);
  
  duration = JwksClient::parse_cache_control("no-cache, max-age=86400");
  EXPECT_EQ(duration.count(), 86400);
  
  duration = JwksClient::parse_cache_control("no-cache");
  EXPECT_EQ(duration.count(), 3600); // Default
}

// Test key validation
TEST_F(JwksClientTest, KeyValidation) {
  JsonWebKey rsa_key;
  rsa_key.kid = "test-rsa";
  rsa_key.kty = "RSA";
  
  // Missing n and e
  EXPECT_FALSE(rsa_key.is_valid());
  
  rsa_key.n = "modulus";
  rsa_key.e = "AQAB";
  EXPECT_TRUE(rsa_key.is_valid());
  
  JsonWebKey ec_key;
  ec_key.kid = "test-ec";
  ec_key.kty = "EC";
  
  // Missing curve and coordinates
  EXPECT_FALSE(ec_key.is_valid());
  
  ec_key.crv = "P-256";
  ec_key.x = "x-coord";
  ec_key.y = "y-coord";
  EXPECT_TRUE(ec_key.is_valid());
}

// Test JWKS response expiry
TEST_F(JwksClientTest, JwksResponseExpiry) {
  JwksResponse response;
  response.fetched_at = std::chrono::system_clock::now();
  response.cache_duration = std::chrono::seconds(1);
  
  EXPECT_FALSE(response.is_expired());
  
  std::this_thread::sleep_for(std::chrono::milliseconds(1100));
  
  EXPECT_TRUE(response.is_expired());
}

// Test finding keys in response
TEST_F(JwksClientTest, FindKeyInResponse) {
  JwksResponse response;
  
  JsonWebKey key1;
  key1.kid = "key-1";
  key1.kty = "RSA";
  key1.n = "n";
  key1.e = "e";
  response.keys.push_back(key1);
  
  JsonWebKey key2;
  key2.kid = "key-2";
  key2.kty = "EC";
  key2.crv = "P-256";
  key2.x = "x";
  key2.y = "y";
  response.keys.push_back(key2);
  
  auto found = response.find_key("key-1");
  ASSERT_TRUE(found.has_value());
  EXPECT_EQ(found.value().kid, "key-1");
  
  found = response.find_key("key-2");
  ASSERT_TRUE(found.has_value());
  EXPECT_EQ(found.value().kid, "key-2");
  
  found = response.find_key("non-existent");
  EXPECT_FALSE(found.has_value());
}

// Test basic client functionality (without network)
TEST_F(JwksClientTest, ClientCaching) {
  // Create client with test configuration
  config_.jwks_uri = "https://httpbin.org/status/404"; // Will fail
  JwksClient client(config_);
  
  // Clear cache first
  client.clear_cache();
  
  auto stats = client.get_cache_stats();
  EXPECT_EQ(stats.keys_cached, 0);
  EXPECT_EQ(stats.cache_hits, 0);
  EXPECT_EQ(stats.cache_misses, 0);
}

// Test cache statistics
TEST_F(JwksClientTest, CacheStatistics) {
  config_.jwks_uri = "https://httpbin.org/json"; // Returns JSON but not JWKS
  JwksClient client(config_);
  
  client.clear_cache();
  
  // First fetch (cache miss)
  auto keys = client.fetch_keys(false);
  
  auto stats = client.get_cache_stats();
  EXPECT_GE(stats.cache_misses, 1);
  
  // Force refresh
  keys = client.fetch_keys(true);
  stats = client.get_cache_stats();
  EXPECT_GE(stats.refresh_count, 1);
}

// Test auto refresh control
TEST_F(JwksClientTest, AutoRefreshControl) {
  JwksClient client(config_);
  
  EXPECT_FALSE(client.is_auto_refresh_active());
  
  client.start_auto_refresh();
  EXPECT_TRUE(client.is_auto_refresh_active());
  
  // Starting again should be idempotent
  client.start_auto_refresh();
  EXPECT_TRUE(client.is_auto_refresh_active());
  
  client.stop_auto_refresh();
  EXPECT_FALSE(client.is_auto_refresh_active());
}

// Test configuration defaults
TEST_F(JwksClientTest, ConfigDefaults) {
  JwksClientConfig default_config;
  
  EXPECT_EQ(default_config.default_cache_duration.count(), 3600);
  EXPECT_EQ(default_config.min_cache_duration.count(), 60);
  EXPECT_EQ(default_config.max_cache_duration.count(), 86400);
  EXPECT_TRUE(default_config.respect_cache_control);
  EXPECT_EQ(default_config.max_keys_cached, 100);
  EXPECT_EQ(default_config.request_timeout.count(), 30);
  EXPECT_FALSE(default_config.auto_refresh);
  EXPECT_EQ(default_config.refresh_before_expiry.count(), 60);
}

// Integration test with real endpoint (skip if no network)
TEST_F(JwksClientTest, DISABLED_RealEndpointFetch) {
  // This test is disabled by default as it requires network
  // Enable with --gtest_also_run_disabled_tests
  
  JwksClient client(config_);
  
  auto response = client.fetch_keys(false);
  if (response.has_value()) {
    EXPECT_GT(response.value().keys.size(), 0);
    
    // Google's JWKS should have valid RSA keys
    for (const auto& key : response.value().keys) {
      EXPECT_TRUE(key.is_valid());
      EXPECT_FALSE(key.kid.empty());
    }
    
    // Test getting specific key
    if (!response.value().keys.empty()) {
      auto first_kid = response.value().keys[0].kid;
      auto key = client.get_key(first_kid);
      EXPECT_TRUE(key.has_value());
      EXPECT_EQ(key.value().kid, first_kid);
    }
  }
}

} // namespace
} // namespace auth
} // namespace mcp