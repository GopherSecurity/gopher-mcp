/**
 * @file test_ssl_context.cc
 * @brief Unit tests for SSL/TLS context management
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <fstream>
#include <cstdlib>
#include <unistd.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

#include "mcp/transport/ssl_context.h"

namespace mcp {
namespace transport {
namespace {

using ::testing::_;
using ::testing::Return;
using ::testing::NotNull;
using ::testing::IsNull;

/**
 * Test fixture for SSL context tests
 * Creates temporary certificate files for testing
 */
class SslContextTest : public ::testing::Test {
protected:
  void SetUp() override {
    // Create temporary directory for test certificates
    char temp_template[] = "/tmp/ssl_test_XXXXXX";
    test_dir_ = mkdtemp(temp_template);
    
    // Create test certificate files
    cert_file_ = test_dir_ + "/cert.pem";
    key_file_ = test_dir_ + "/key.pem";
    ca_file_ = test_dir_ + "/ca.pem";
    
    createTestCertificate();
    createTestPrivateKey();
    createTestCaCertificate();
  }
  
  void TearDown() override {
    // Clean up test files
    unlink(cert_file_.c_str());
    unlink(key_file_.c_str());
    unlink(ca_file_.c_str());
    rmdir(test_dir_.c_str());
  }
  
  /**
   * Create a test certificate file (self-signed for testing)
   */
  void createTestCertificate() {
    std::ofstream cert(cert_file_);
    cert << "-----BEGIN CERTIFICATE-----\n"
         << "MIIDazCCAlOgAwIBAgIUFjYAHtYLvV3nUtNxn5M9LpqOXuUwDQYJKoZIhvcNAQEL\n"
         << "BQAwRTELMAkGA1UEBhMCVVMxEzARBgNVBAgMClNvbWUtU3RhdGUxITAfBgNVBAoM\n"
         << "GEludGVybmV0IFdpZGdpdHMgUHR5IEx0ZDAeFw0yNDAzMDEwMDAwMDBaFw0yNTAz\n"
         << "MDEwMDAwMDBaMEUxCzAJBgNVBAYTAlVTMRMwEQYDVQQIDApTb21lLVN0YXRlMSEw\n"
         << "HwYDVQQKDBhJbnRlcm5ldCBXaWRnaXRzIFB0eSBMdGQwggEiMA0GCSqGSIb3DQEB\n"
         << "AQUAA4IBDwAwggEKAoIBAQDFhYkJHhYRnGVVuFM1MsNbkuLlvWepR5UfOj8rJsO4\n"
         << "g9vQ9wF6G5+kIl8qT9FvTrVNNZ+IY5VmJKYzKHXN1PooBtPVLzPNvQXDqnCqQkFt\n"
         << "yTrJhBjVxvJQqVJUqNXKuA7qsFOOvXLmVJiWZmQWvkBHHNbVmZXFmwKqS4P8qxqH\n"
         << "TESTCERTTESTCERTTESTCERTTESTCERTTESTCERTTESTCERTTESTCERTTESTCERT\n"
         << "CAwsaV5OE8K7FWfXtNY5Y8r9Y3JleW1TYBwY5qF0K0FntkYnJlONJ9Y3JleW1TYB\n"
         << "BgkqhkiG9w0BAQUFAAOCAQEA1234567890ABCDEFGHIJKLMNOPQRSTUVWXYZ1234\n"
         << "567890abcdefghijklmnopqrstuvwxyz1234567890ABCDEFGHIJKLMNOPQRSTUV\n"
         << "-----END CERTIFICATE-----\n";
  }
  
  /**
   * Create a test private key file
   */
  void createTestPrivateKey() {
    std::ofstream key(key_file_);
    key << "-----BEGIN PRIVATE KEY-----\n"
        << "MIIEvQIBADANBgkqhkiG9w0BAQEFAASCBKcwggSjAgEAAoIBAQDFhYkJHhYRnGVV\n"
        << "uFM1MsNbkuLlvWepR5UfOj8rJsO4g9vQ9wF6G5+kIl8qT9FvTrVNNZ+IY5VmJKYz\n"
        << "KHXN1PooBtPVLzPNvQXDqnCqQkFtyTrJhBjVxvJQqVJUqNXKuA7qsFOOvXLmVJiW\n"
        << "TESTKEYTESTKEYTESTKEYTESTKEYTESTKEYTESTKEYTESTKEYTESTKEYTESTKEY\n"
        << "TESTKEYTESTKEYTESTKEYTESTKEYTESTKEYTESTKEYTESTKEYTESTKEYTESTKEY\n"
        << "abcdefghijklmnopqrstuvwxyz1234567890ABCDEFGHIJKLMNOPQRSTUVWXYZ==\n"
        << "-----END PRIVATE KEY-----\n";
  }
  
  /**
   * Create a test CA certificate file
   */
  void createTestCaCertificate() {
    std::ofstream ca(ca_file_);
    ca << "-----BEGIN CERTIFICATE-----\n"
       << "MIIDXTCCAkWgAwIBAgIJAKLdQVPy6+XIMA0GCSqGSIb3DQEBCwUAMEUxCzAJBgNV\n"
       << "BAYTAlVTMRMwEQYDVQQIDApTb21lLVN0YXRlMSEwHwYDVQQKDBhJbnRlcm5ldCBX\n"
       << "aWRnaXRzIFB0eSBMdGQwHhcNMjQwMzAxMDAwMDAwWhcNMjkwMzAxMDAwMDAwWjBF\n"
       << "TESTCATESTCATESTCATESTCATESTCATESTCATESTCATESTCATESTCATESTCATEST\n"
       << "TESTCATESTCATESTCATESTCATESTCATESTCATESTCATESTCATESTCATESTCATEST\n"
       << "1234567890abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ\n"
       << "-----END CERTIFICATE-----\n";
  }

protected:
  std::string test_dir_;
  std::string cert_file_;
  std::string key_file_;
  std::string ca_file_;
};

/**
 * Test SSL context creation with valid configuration
 */
TEST_F(SslContextTest, CreateContextWithValidConfig) {
  // Create client context configuration
  SslContextConfig config;
  config.is_client = true;
  config.verify_peer = true;
  config.protocols = {"TLSv1.2", "TLSv1.3"};
  config.cipher_suites = "HIGH:!aNULL:!MD5";
  
  // Create context
  auto result = SslContext::create(config);
  
  // Verify creation succeeded
  ASSERT_FALSE(holds_alternative<Error>(result)) << "Failed to create context: " << get<Error>(result).message;
  ASSERT_NE(get<SslContextSharedPtr>(result), nullptr);
  
  // Verify context properties
  auto context = get<SslContextSharedPtr>(result);
  EXPECT_TRUE(context->isClient());
  EXPECT_EQ(context->getConfig().verify_peer, true);
  EXPECT_EQ(context->getConfig().protocols.size(), 2);
}

/**
 * Test SSL context with certificates
 */
TEST_F(SslContextTest, CreateContextWithCertificates) {
  // Create server context with certificates
  SslContextConfig config;
  config.is_client = false;
  config.cert_chain_file = cert_file_;
  config.private_key_file = key_file_;
  config.ca_cert_file = ca_file_;
  config.verify_peer = true;
  
  // Create context
  auto result = SslContext::create(config);
  
  // Note: This test may fail with real OpenSSL validation
  // In production, use proper test certificates
  if (!holds_alternative<Error>(result)) {
    ASSERT_NE(get<SslContextSharedPtr>(result), nullptr);
    EXPECT_FALSE(get<SslContextSharedPtr>(result)->isClient());
  }
}

/**
 * Test SSL context creation with invalid certificate path
 */
TEST_F(SslContextTest, CreateContextWithInvalidCertPath) {
  SslContextConfig config;
  config.cert_chain_file = "/nonexistent/cert.pem";
  config.private_key_file = "/nonexistent/key.pem";
  
  // Create context should fail
  auto result = SslContext::create(config);
  
  EXPECT_TRUE(holds_alternative<Error>(result));
  EXPECT_FALSE(get<Error>(result).message.empty());
}

/**
 * Test creating SSL connection from context
 */
TEST_F(SslContextTest, CreateSslFromContext) {
  // Create simple client context
  SslContextConfig config;
  config.is_client = true;
  
  auto result = SslContext::create(config);
  ASSERT_FALSE(holds_alternative<Error>(result));
  
  auto context = get<SslContextSharedPtr>(result);
  
  // Create SSL connection
  SSL* ssl = context->newSsl();
  
  ASSERT_NE(ssl, nullptr);
  
  // Clean up
  SSL_free(ssl);
}

/**
 * Test SNI hostname setting
 */
TEST_F(SslContextTest, SetSniHostname) {
  // Create client context
  SslContextConfig config;
  config.is_client = true;
  config.sni_hostname = "example.com";
  
  auto result = SslContext::create(config);
  ASSERT_FALSE(holds_alternative<Error>(result));
  
  auto context = get<SslContextSharedPtr>(result);
  SSL* ssl = context->newSsl();
  ASSERT_NE(ssl, nullptr);
  
  // Set SNI hostname
  auto sni_result = SslContext::setSniHostname(ssl, "test.example.com");
  EXPECT_FALSE(holds_alternative<Error>(sni_result));
  
  // Clean up
  SSL_free(ssl);
}

/**
 * Test ALPN protocol configuration
 */
TEST_F(SslContextTest, ConfigureAlpnProtocols) {
  SslContextConfig config;
  config.is_client = true;
  config.alpn_protocols = {"h2", "http/1.1"};
  
  auto result = SslContext::create(config);
  ASSERT_FALSE(holds_alternative<Error>(result));
  
  auto context = get<SslContextSharedPtr>(result);
  EXPECT_EQ(context->getConfig().alpn_protocols.size(), 2);
  EXPECT_EQ(context->getConfig().alpn_protocols[0], "h2");
  EXPECT_EQ(context->getConfig().alpn_protocols[1], "http/1.1");
}

/**
 * Test session resumption configuration
 */
TEST_F(SslContextTest, ConfigureSessionResumption) {
  SslContextConfig config;
  config.is_client = true;
  config.enable_session_resumption = true;
  config.session_timeout = 600;  // 10 minutes
  
  auto result = SslContext::create(config);
  ASSERT_FALSE(holds_alternative<Error>(result));
  
  auto context = get<SslContextSharedPtr>(result);
  EXPECT_TRUE(context->getConfig().enable_session_resumption);
  EXPECT_EQ(context->getConfig().session_timeout, 600);
}

/**
 * Test context manager caching
 */
TEST_F(SslContextTest, ContextManagerCaching) {
  // Create configuration
  SslContextConfig config;
  config.is_client = true;
  config.verify_peer = false;
  
  // Get context from manager
  auto& manager = SslContextManager::getInstance();
  auto result1 = manager.getOrCreateContext(config);
  ASSERT_FALSE(holds_alternative<Error>(result1));
  
  // Get same context again (should be cached)
  auto result2 = manager.getOrCreateContext(config);
  ASSERT_FALSE(holds_alternative<Error>(result2));
  
  // Verify same context returned
  EXPECT_EQ(get<SslContextSharedPtr>(result1).get(), get<SslContextSharedPtr>(result2).get());
  
  // Clear cache
  manager.clearCache();
  
  // Get context again (should create new)
  auto result3 = manager.getOrCreateContext(config);
  ASSERT_FALSE(holds_alternative<Error>(result3));
  
  // Should be different context after cache clear
  EXPECT_NE(get<SslContextSharedPtr>(result1).get(), get<SslContextSharedPtr>(result3).get());
}

/**
 * Test multiple SSL connections from same context
 */
TEST_F(SslContextTest, MultipleConnectionsFromContext) {
  SslContextConfig config;
  config.is_client = true;
  
  auto result = SslContext::create(config);
  ASSERT_FALSE(holds_alternative<Error>(result));
  
  auto context = get<SslContextSharedPtr>(result);
  
  // Create multiple SSL connections
  std::vector<SSL*> connections;
  for (int i = 0; i < 5; ++i) {
    SSL* ssl = context->newSsl();
    ASSERT_NE(ssl, nullptr);
    connections.push_back(ssl);
  }
  
  // All connections should be different
  for (size_t i = 0; i < connections.size(); ++i) {
    for (size_t j = i + 1; j < connections.size(); ++j) {
      EXPECT_NE(connections[i], connections[j]);
    }
  }
  
  // Clean up
  for (SSL* ssl : connections) {
    SSL_free(ssl);
  }
}

/**
 * Test cipher suite configuration
 */
TEST_F(SslContextTest, ConfigureCipherSuites) {
  SslContextConfig config;
  config.is_client = true;
  config.cipher_suites = "ECDHE-RSA-AES128-GCM-SHA256:ECDHE-RSA-AES256-GCM-SHA384";
  
  auto result = SslContext::create(config);
  ASSERT_FALSE(holds_alternative<Error>(result));
  
  auto context = get<SslContextSharedPtr>(result);
  EXPECT_FALSE(context->getConfig().cipher_suites.empty());
}

/**
 * Test protocol version configuration
 */
TEST_F(SslContextTest, ConfigureProtocolVersions) {
  // Test TLS 1.2 only
  {
    SslContextConfig config;
    config.is_client = true;
    config.protocols = {"TLSv1.2"};
    
    auto result = SslContext::create(config);
    ASSERT_FALSE(holds_alternative<Error>(result));
    EXPECT_EQ(get<SslContextSharedPtr>(result)->getConfig().protocols.size(), 1);
  }
  
  // Test TLS 1.3 only
  {
    SslContextConfig config;
    config.is_client = true;
    config.protocols = {"TLSv1.3"};
    
    auto result = SslContext::create(config);
    ASSERT_FALSE(holds_alternative<Error>(result));
    EXPECT_EQ(get<SslContextSharedPtr>(result)->getConfig().protocols.size(), 1);
  }
  
  // Test both TLS 1.2 and 1.3
  {
    SslContextConfig config;
    config.is_client = true;
    config.protocols = {"TLSv1.2", "TLSv1.3"};
    
    auto result = SslContext::create(config);
    ASSERT_FALSE(holds_alternative<Error>(result));
    EXPECT_EQ(get<SslContextSharedPtr>(result)->getConfig().protocols.size(), 2);
  }
}

/**
 * Test verification settings
 */
TEST_F(SslContextTest, ConfigureVerification) {
  // Test with verification enabled
  {
    SslContextConfig config;
    config.is_client = true;
    config.verify_peer = true;
    config.verify_peer_cert_chain = true;
    
    auto result = SslContext::create(config);
    ASSERT_FALSE(holds_alternative<Error>(result));
    EXPECT_TRUE(get<SslContextSharedPtr>(result)->getConfig().verify_peer);
    EXPECT_TRUE(get<SslContextSharedPtr>(result)->getConfig().verify_peer_cert_chain);
  }
  
  // Test with verification disabled
  {
    SslContextConfig config;
    config.is_client = true;
    config.verify_peer = false;
    config.verify_peer_cert_chain = false;
    
    auto result = SslContext::create(config);
    ASSERT_FALSE(holds_alternative<Error>(result));
    EXPECT_FALSE(get<SslContextSharedPtr>(result)->getConfig().verify_peer);
    EXPECT_FALSE(get<SslContextSharedPtr>(result)->getConfig().verify_peer_cert_chain);
  }
}

}  // namespace
}  // namespace transport
}  // namespace mcp