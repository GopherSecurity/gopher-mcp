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
    dispatcher_ = std::make_unique<event::LibeventDispatcher>("test");
  }
  
  void TearDown() override {
    dispatcher_.reset();
  }
  
  /**
   * Create test configuration
   */
  HttpSseTransportSocketConfig createTestConfig(bool use_ssl = false) {
    HttpSseTransportSocketConfig config;
    config.server_address = "example.com:" + std::string(use_ssl ? "443" : "80");
    config.mode = HttpSseTransportSocketConfig::Mode::CLIENT;
    config.underlying_transport = use_ssl ? 
        HttpSseTransportSocketConfig::UnderlyingTransport::SSL :
        HttpSseTransportSocketConfig::UnderlyingTransport::TCP;
    
    if (use_ssl) {
      config.ssl_config = HttpSseTransportSocketConfig::SslConfig{};
      config.ssl_config->verify_peer = false;  // Disable for testing
    }
    
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
 * Test SSL configuration detection
 */
TEST_F(HttpsSseFactoryTest, SslConfigurationDetection) {
  // Test SSL transport
  {
    HttpSseTransportSocketConfig config;
    config.server_address = "secure.example.com:443";
    config.underlying_transport = HttpSseTransportSocketConfig::UnderlyingTransport::SSL;
    config.ssl_config = HttpSseTransportSocketConfig::SslConfig{};
    
    auto factory = std::make_unique<HttpsSseTransportFactory>(config, *dispatcher_);
    EXPECT_TRUE(factory->implementsSecureTransport());
  }
  
  // Test TCP (no SSL)
  {
    HttpSseTransportSocketConfig config;
    config.server_address = "plain.example.com:80";
    config.underlying_transport = HttpSseTransportSocketConfig::UnderlyingTransport::TCP;
    
    auto factory = std::make_unique<HttpsSseTransportFactory>(config, *dispatcher_);
    EXPECT_FALSE(factory->implementsSecureTransport());
  }
  
}

/**
 * Test SNI hostname extraction
 */
TEST_F(HttpsSseFactoryTest, SniHostnameExtraction) {
  // Test with port
  {
    HttpSseTransportSocketConfig config;
    config.server_address = "example.com:8443";
    config.underlying_transport = HttpSseTransportSocketConfig::UnderlyingTransport::SSL;
    config.ssl_config = HttpSseTransportSocketConfig::SslConfig{};
    config.ssl_config->sni_hostname = "example.com";
    
    auto factory = std::make_unique<HttpsSseTransportFactory>(config, *dispatcher_);
    EXPECT_EQ(factory->defaultServerNameIndication(), "example.com");
  }
  
  // Test with subdomain
  {
    HttpSseTransportSocketConfig config;
    config.server_address = "api.example.com:443";
    config.underlying_transport = HttpSseTransportSocketConfig::UnderlyingTransport::SSL;
    config.ssl_config = HttpSseTransportSocketConfig::SslConfig{};
    config.ssl_config->sni_hostname = "api.example.com";
    
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
    config.server_address = "example.com:443";
    config.underlying_transport = HttpSseTransportSocketConfig::UnderlyingTransport::SSL;
    config.ssl_config = HttpSseTransportSocketConfig::SslConfig{};
    config.ssl_config->alpn_protocols = {"h2", "http/1.1"};
    
    auto factory = std::make_unique<HttpsSseTransportFactory>(config, *dispatcher_);
    EXPECT_TRUE(factory->supportsAlpn());
  }
  
  // Without ALPN protocols (should set defaults)
  {
    HttpSseTransportSocketConfig config;
    config.server_address = "example.com:443";
    config.underlying_transport = HttpSseTransportSocketConfig::UnderlyingTransport::SSL;
    config.ssl_config = HttpSseTransportSocketConfig::SslConfig{};
    // alpn_protocols not set, should get defaults
    
    auto factory = std::make_unique<HttpsSseTransportFactory>(config, *dispatcher_);
    EXPECT_TRUE(factory->supportsAlpn());
  }
  
  // TCP (no SSL, no ALPN)
  {
    HttpSseTransportSocketConfig config;
    config.server_address = "example.com:80";
    config.underlying_transport = HttpSseTransportSocketConfig::UnderlyingTransport::TCP;
    
    auto factory = std::make_unique<HttpsSseTransportFactory>(config, *dispatcher_);
    EXPECT_FALSE(factory->supportsAlpn());
  }
}

/**
 * Test hash key generation
 */
TEST_F(HttpsSseFactoryTest, HashKeyGeneration) {
  HttpSseTransportSocketConfig config;
  config.server_address = "example.com:443";
  config.underlying_transport = HttpSseTransportSocketConfig::UnderlyingTransport::SSL;
  config.ssl_config = HttpSseTransportSocketConfig::SslConfig{};
  config.ssl_config->verify_peer = true;
  config.ssl_config->sni_hostname = "example.com";
  
  auto factory = std::make_unique<HttpsSseTransportFactory>(config, *dispatcher_);
  
  std::vector<uint8_t> key;
  factory->hashKey(key, nullptr);
  
  // Hash should contain factory name and config elements
  EXPECT_FALSE(key.empty());
  
  // Hash should be different for different configs
  HttpSseTransportSocketConfig config2;
  config2.server_address = "other.com:443";
  config2.underlying_transport = HttpSseTransportSocketConfig::UnderlyingTransport::SSL;
  config2.ssl_config = HttpSseTransportSocketConfig::SslConfig{};
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
  config.server_address = "example.com:443";
  config.underlying_transport = HttpSseTransportSocketConfig::UnderlyingTransport::SSL;
  config.ssl_config = HttpSseTransportSocketConfig::SslConfig{};
  config.ssl_config->client_cert_path = "/path/to/client.crt";
  config.ssl_config->client_key_path = "/path/to/client.key";
  config.ssl_config->ca_cert_path = "/path/to/ca.crt";
  config.ssl_config->verify_peer = true;
  
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
  config.server_address = "192.168.1.1:443";  // IP address
  config.underlying_transport = HttpSseTransportSocketConfig::UnderlyingTransport::SSL;
  config.ssl_config = HttpSseTransportSocketConfig::SslConfig{};
  config.ssl_config->sni_hostname = "example.com";  // Custom SNI
  
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
 * Test factory creation with different configurations
 */
TEST_F(HttpsSseFactoryTest, FactoryCreationConfigurations) {
  // Client mode with SSL
  {
    HttpSseTransportSocketConfig config = createTestConfig(true);
    auto factory = std::make_unique<HttpsSseTransportFactory>(config, *dispatcher_);
    ASSERT_NE(factory, nullptr);
    EXPECT_TRUE(factory->implementsSecureTransport());
  }
  
  // Client mode without SSL
  {
    HttpSseTransportSocketConfig config = createTestConfig(false);
    auto factory = std::make_unique<HttpsSseTransportFactory>(config, *dispatcher_);
    ASSERT_NE(factory, nullptr);
    EXPECT_FALSE(factory->implementsSecureTransport());
  }
}


/**
 * Test server mode factory creation
 */
TEST_F(HttpsSseFactoryTest, ServerModeFactory) {
  HttpSseTransportSocketConfig config;
  config.server_address = "0.0.0.0:8443";
  config.mode = HttpSseTransportSocketConfig::Mode::SERVER;
  config.underlying_transport = HttpSseTransportSocketConfig::UnderlyingTransport::SSL;
  config.ssl_config = HttpSseTransportSocketConfig::SslConfig{};
  config.ssl_config->client_cert_path = "/path/to/server.crt";  // Server cert
  config.ssl_config->client_key_path = "/path/to/server.key";   // Server key
  config.ssl_config->verify_peer = false;  // Don't verify client certs
  
  auto factory = std::make_unique<HttpsSseTransportFactory>(config, *dispatcher_);
  
  // Should be able to create server transport
  // Note: Actual transport creation would fail without real implementation
  ASSERT_NE(factory, nullptr);
  EXPECT_TRUE(factory->implementsSecureTransport());
}


/**
 * Test verification settings
 */
TEST_F(HttpsSseFactoryTest, VerificationSettings) {
  // With verification
  {
    HttpSseTransportSocketConfig config;
    config.server_address = "example.com:443";
    config.underlying_transport = HttpSseTransportSocketConfig::UnderlyingTransport::SSL;
    config.ssl_config = HttpSseTransportSocketConfig::SslConfig{};
    config.ssl_config->verify_peer = true;
    config.ssl_config->ca_cert_path = "/path/to/ca.crt";
    
    auto factory = std::make_unique<HttpsSseTransportFactory>(config, *dispatcher_);
    ASSERT_NE(factory, nullptr);
  }
  
  // Without verification (for testing/development)
  {
    HttpSseTransportSocketConfig config;
    config.server_address = "example.com:443";
    config.underlying_transport = HttpSseTransportSocketConfig::UnderlyingTransport::SSL;
    config.ssl_config = HttpSseTransportSocketConfig::SslConfig{};
    config.ssl_config->verify_peer = false;
    
    auto factory = std::make_unique<HttpsSseTransportFactory>(config, *dispatcher_);
    ASSERT_NE(factory, nullptr);
  }
}

}  // namespace
}  // namespace transport
}  // namespace mcp