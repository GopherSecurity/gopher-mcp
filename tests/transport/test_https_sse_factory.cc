/**
 * @file test_https_sse_factory.cc
 * @brief Unit tests for HTTPS+SSE transport factory
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <memory>

#include "mcp/transport/https_sse_transport_factory.h"
#include "mcp/event/libevent_dispatcher.h"

namespace mcp {
namespace transport {
namespace {

using ::testing::_;
using ::testing::Return;
using ::testing::NotNull;
using ::testing::Eq;

/**
 * Test fixture for HTTPS+SSE factory tests
 */
class HttpsSseFactoryTest : public ::testing::Test {
protected:
  void SetUp() override {
    // Create dispatcher
    dispatcher_ = std::make_unique<event::LibeventDispatcher>();
  }
  
  void TearDown() override {
    dispatcher_.reset();
  }
  
  /**
   * Create test configuration
   */
  HttpSseTransportSocketConfig createTestConfig(bool use_ssl = false) {
    HttpSseTransportSocketConfig config;
    config.endpoint_url = use_ssl ? "https://example.com:443" : "http://example.com:80";
    config.use_ssl = use_ssl;
    config.verify_ssl = false;  // Disable for testing
    config.request_method = "POST";
    config.sse_method = "GET";
    config.sse_endpoint_path = "/events";
    config.request_endpoint_path = "/rpc";
    return config;
  }

protected:
  std::unique_ptr<event::Dispatcher> dispatcher_;
};

/**
 * Test factory creation with HTTP configuration
 */
TEST_F(HttpsSseFactoryTest, CreateFactoryWithHttp) {
  auto config = createTestConfig(false);
  
  auto factory = std::make_unique<HttpsSseTransportFactory>(config, *dispatcher_);
  
  ASSERT_NE(factory, nullptr);
  EXPECT_EQ(factory->name(), "http+sse");
  EXPECT_FALSE(factory->implementsSecureTransport());
}

/**
 * Test factory creation with HTTPS configuration
 */
TEST_F(HttpsSseFactoryTest, CreateFactoryWithHttps) {
  auto config = createTestConfig(true);
  
  auto factory = std::make_unique<HttpsSseTransportFactory>(config, *dispatcher_);
  
  ASSERT_NE(factory, nullptr);
  EXPECT_EQ(factory->name(), "https+sse");
  EXPECT_TRUE(factory->implementsSecureTransport());
}

/**
 * Test auto-detection of SSL from URL
 */
TEST_F(HttpsSseFactoryTest, AutoDetectSslFromUrl) {
  // Test HTTPS detection
  {
    HttpSseTransportSocketConfig config;
    config.endpoint_url = "https://secure.example.com";
    // Don't explicitly set use_ssl
    
    auto factory = std::make_unique<HttpsSseTransportFactory>(config, *dispatcher_);
    EXPECT_TRUE(factory->implementsSecureTransport());
  }
  
  // Test HTTP (no SSL)
  {
    HttpSseTransportSocketConfig config;
    config.endpoint_url = "http://plain.example.com";
    
    auto factory = std::make_unique<HttpsSseTransportFactory>(config, *dispatcher_);
    EXPECT_FALSE(factory->implementsSecureTransport());
  }
  
  // Test case insensitive
  {
    HttpSseTransportSocketConfig config;
    config.endpoint_url = "HTTPS://UPPERCASE.EXAMPLE.COM";
    
    auto factory = std::make_unique<HttpsSseTransportFactory>(config, *dispatcher_);
    EXPECT_TRUE(factory->implementsSecureTransport());
  }
}

/**
 * Test SNI hostname extraction
 */
TEST_F(HttpsSseFactoryTest, SniHostnameExtraction) {
  // Test with port
  {
    HttpSseTransportSocketConfig config;
    config.endpoint_url = "https://example.com:8443/path";
    
    auto factory = std::make_unique<HttpsSseTransportFactory>(config, *dispatcher_);
    EXPECT_EQ(factory->defaultServerNameIndication(), "example.com");
  }
  
  // Test without port
  {
    HttpSseTransportSocketConfig config;
    config.endpoint_url = "https://example.com/path";
    
    auto factory = std::make_unique<HttpsSseTransportFactory>(config, *dispatcher_);
    EXPECT_EQ(factory->defaultServerNameIndication(), "example.com");
  }
  
  // Test with subdomain
  {
    HttpSseTransportSocketConfig config;
    config.endpoint_url = "https://api.example.com:443";
    
    auto factory = std::make_unique<HttpsSseTransportFactory>(config, *dispatcher_);
    EXPECT_EQ(factory->defaultServerNameIndication(), "api.example.com");
  }
}

/**
 * Test ALPN support
 */
TEST_F(HttpsSseFactoryTest, AlpnSupport) {
  // With ALPN protocols
  {
    HttpSseTransportSocketConfig config;
    config.endpoint_url = "https://example.com";
    config.alpn_protocols = {"h2", "http/1.1"};
    
    auto factory = std::make_unique<HttpsSseTransportFactory>(config, *dispatcher_);
    EXPECT_TRUE(factory->supportsAlpn());
  }
  
  // Without ALPN protocols (should set defaults)
  {
    HttpSseTransportSocketConfig config;
    config.endpoint_url = "https://example.com";
    // alpn_protocols not set, should get defaults
    
    auto factory = std::make_unique<HttpsSseTransportFactory>(config, *dispatcher_);
    EXPECT_TRUE(factory->supportsAlpn());
  }
  
  // HTTP (no SSL, no ALPN)
  {
    HttpSseTransportSocketConfig config;
    config.endpoint_url = "http://example.com";
    
    auto factory = std::make_unique<HttpsSseTransportFactory>(config, *dispatcher_);
    EXPECT_FALSE(factory->supportsAlpn());
  }
}

/**
 * Test hash key generation
 */
TEST_F(HttpsSseFactoryTest, HashKeyGeneration) {
  HttpSseTransportSocketConfig config;
  config.endpoint_url = "https://example.com";
  config.verify_ssl = true;
  config.sni_hostname = "example.com";
  
  auto factory = std::make_unique<HttpsSseTransportFactory>(config, *dispatcher_);
  
  std::vector<uint8_t> key;
  factory->hashKey(key, nullptr);
  
  // Hash should contain factory name and config elements
  EXPECT_FALSE(key.empty());
  
  // Hash should be different for different configs
  HttpSseTransportSocketConfig config2;
  config2.endpoint_url = "https://other.com";
  auto factory2 = std::make_unique<HttpsSseTransportFactory>(config2, *dispatcher_);
  
  std::vector<uint8_t> key2;
  factory2->hashKey(key2, nullptr);
  
  EXPECT_NE(key, key2);
}

/**
 * Test certificate configuration
 */
TEST_F(HttpsSseFactoryTest, CertificateConfiguration) {
  HttpSseTransportSocketConfig config;
  config.endpoint_url = "https://example.com";
  config.client_cert_path = "/path/to/client.crt";
  config.client_key_path = "/path/to/client.key";
  config.ca_cert_path = "/path/to/ca.crt";
  config.verify_ssl = true;
  
  // Factory should accept certificate configuration
  auto factory = std::make_unique<HttpsSseTransportFactory>(config, *dispatcher_);
  ASSERT_NE(factory, nullptr);
  EXPECT_TRUE(factory->implementsSecureTransport());
}

/**
 * Test factory with custom SNI
 */
TEST_F(HttpsSseFactoryTest, CustomSniHostname) {
  HttpSseTransportSocketConfig config;
  config.endpoint_url = "https://192.168.1.1";  // IP address
  config.sni_hostname = "example.com";  // Custom SNI
  
  auto factory = std::make_unique<HttpsSseTransportFactory>(config, *dispatcher_);
  EXPECT_EQ(factory->defaultServerNameIndication(), "example.com");
}

/**
 * Test helper function for creating factory
 */
TEST_F(HttpsSseFactoryTest, CreateFactoryHelper) {
  auto config = createTestConfig(true);
  
  auto factory = createHttpsSseTransportFactory(config, *dispatcher_);
  
  ASSERT_NE(factory, nullptr);
  EXPECT_EQ(factory->name(), "https+sse");
}

/**
 * Test URL parsing edge cases
 */
TEST_F(HttpsSseFactoryTest, UrlParsingEdgeCases) {
  // Empty URL
  {
    HttpSseTransportSocketConfig config;
    config.endpoint_url = "";
    
    auto factory = std::make_unique<HttpsSseTransportFactory>(config, *dispatcher_);
    EXPECT_FALSE(factory->implementsSecureTransport());
    EXPECT_TRUE(factory->defaultServerNameIndication().empty());
  }
  
  // URL without protocol
  {
    HttpSseTransportSocketConfig config;
    config.endpoint_url = "example.com";
    
    auto factory = std::make_unique<HttpsSseTransportFactory>(config, *dispatcher_);
    EXPECT_FALSE(factory->implementsSecureTransport());
  }
  
  // URL with path only
  {
    HttpSseTransportSocketConfig config;
    config.endpoint_url = "/path/to/resource";
    
    auto factory = std::make_unique<HttpsSseTransportFactory>(config, *dispatcher_);
    EXPECT_FALSE(factory->implementsSecureTransport());
  }
}

/**
 * Test HTTP version preferences
 */
TEST_F(HttpsSseFactoryTest, HttpVersionPreferences) {
  // HTTP/2 preference
  {
    HttpSseTransportSocketConfig config;
    config.endpoint_url = "https://example.com";
    config.preferred_version = http::HttpVersion::HTTP_2;
    
    auto factory = std::make_unique<HttpsSseTransportFactory>(config, *dispatcher_);
    ASSERT_NE(factory, nullptr);
    // Would affect ALPN protocols in real implementation
  }
  
  // HTTP/1.1 preference
  {
    HttpSseTransportSocketConfig config;
    config.endpoint_url = "https://example.com";
    config.preferred_version = http::HttpVersion::HTTP_1_1;
    
    auto factory = std::make_unique<HttpsSseTransportFactory>(config, *dispatcher_);
    ASSERT_NE(factory, nullptr);
  }
}

/**
 * Test forced SSL configuration
 */
TEST_F(HttpsSseFactoryTest, ForcedSslConfiguration) {
  // Force SSL even with HTTP URL
  {
    HttpSseTransportSocketConfig config;
    config.endpoint_url = "http://example.com";  // HTTP URL
    config.use_ssl = true;  // Force SSL
    
    auto factory = std::make_unique<HttpsSseTransportFactory>(config, *dispatcher_);
    EXPECT_TRUE(factory->implementsSecureTransport());
  }
  
  // Explicitly disable SSL with HTTPS URL (unusual but allowed)
  {
    HttpSseTransportSocketConfig config;
    config.endpoint_url = "https://example.com";  // HTTPS URL
    config.use_ssl = false;  // Force no SSL
    
    auto factory = std::make_unique<HttpsSseTransportFactory>(config, *dispatcher_);
    EXPECT_FALSE(factory->implementsSecureTransport());
  }
}

/**
 * Test server mode factory creation
 */
TEST_F(HttpsSseFactoryTest, ServerModeFactory) {
  HttpSseTransportSocketConfig config;
  config.endpoint_url = "https://0.0.0.0:8443";
  config.client_cert_path = "/path/to/server.crt";  // Server cert
  config.client_key_path = "/path/to/server.key";   // Server key
  config.verify_ssl = false;  // Don't verify client certs
  
  auto factory = std::make_unique<HttpsSseTransportFactory>(config, *dispatcher_);
  
  // Should be able to create server transport
  // Note: Actual transport creation would fail without real implementation
  ASSERT_NE(factory, nullptr);
  EXPECT_TRUE(factory->implementsSecureTransport());
}

/**
 * Test session resumption configuration
 */
TEST_F(HttpsSseFactoryTest, SessionResumptionConfig) {
  HttpSseTransportSocketConfig config;
  config.endpoint_url = "https://example.com";
  // Session resumption would be configured in SSL context
  
  auto factory = std::make_unique<HttpsSseTransportFactory>(config, *dispatcher_);
  ASSERT_NE(factory, nullptr);
}

/**
 * Test verification settings
 */
TEST_F(HttpsSseFactoryTest, VerificationSettings) {
  // With verification
  {
    HttpSseTransportSocketConfig config;
    config.endpoint_url = "https://example.com";
    config.verify_ssl = true;
    config.ca_cert_path = "/path/to/ca.crt";
    
    auto factory = std::make_unique<HttpsSseTransportFactory>(config, *dispatcher_);
    ASSERT_NE(factory, nullptr);
  }
  
  // Without verification (for testing/development)
  {
    HttpSseTransportSocketConfig config;
    config.endpoint_url = "https://example.com";
    config.verify_ssl = false;
    
    auto factory = std::make_unique<HttpsSseTransportFactory>(config, *dispatcher_);
    ASSERT_NE(factory, nullptr);
  }
}

}  // namespace
}  // namespace transport
}  // namespace mcp