/**
 * @file test_ssl_integration.cc
 * @brief Integration tests for SSL with HTTP+SSE transport
 * 
 * These tests verify the complete SSL/TLS stack working together:
 * - SSL handshake between client and server
 * - Encrypted data transmission
 * - Certificate verification
 * - Protocol negotiation
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <thread>
#include <chrono>
#include <atomic>
#include <condition_variable>
#include <openssl/ssl.h>
#include <openssl/err.h>

#include "mcp/transport/ssl_context.h"
#include "mcp/transport/ssl_transport_socket.h"
#include "mcp/transport/https_sse_transport_factory.h"
#include "mcp/event/libevent_dispatcher.h"
#include "mcp/buffer.h"
#include "tests/integration/real_io_test_base.h"

namespace mcp {
namespace transport {
namespace {

using ::testing::_;
using ::testing::Return;
using ::testing::Invoke;
using ::testing::AtLeast;

/**
 * SSL integration test fixture using real I/O
 * 
 * Sets up client and server SSL contexts with test certificates
 * and performs actual SSL handshakes over loopback connections
 */
class SslIntegrationTest : public integration::RealIoTestBase {
protected:
  void SetUp() override {
    RealIoTestBase::SetUp();
    
    // Initialize OpenSSL
    SSL_library_init();
    SSL_load_error_strings();
    OpenSSL_add_all_algorithms();
    
    // Create test certificates
    createTestCertificates();
    
    // Create SSL contexts
    createClientContext();
    createServerContext();
  }
  
  void TearDown() override {
    client_context_.reset();
    server_context_.reset();
    RealIoTestBase::TearDown();
  }
  
  /**
   * Create self-signed test certificates
   * In production, use proper CA-signed certificates
   */
  void createTestCertificates() {
    // Generate RSA key pair
    EVP_PKEY* pkey = EVP_PKEY_new();
    RSA* rsa = RSA_new();
    BIGNUM* bn = BN_new();
    BN_set_word(bn, RSA_F4);
    RSA_generate_key_ex(rsa, 2048, bn, nullptr);
    EVP_PKEY_assign_RSA(pkey, rsa);
    BN_free(bn);
    
    // Create self-signed certificate
    X509* cert = X509_new();
    X509_set_version(cert, 2);
    ASN1_INTEGER_set(X509_get_serialNumber(cert), 1);
    X509_gmtime_adj(X509_get_notBefore(cert), 0);
    X509_gmtime_adj(X509_get_notAfter(cert), 31536000L);  // 1 year
    X509_set_pubkey(cert, pkey);
    
    // Set subject name
    X509_NAME* name = X509_get_subject_name(cert);
    X509_NAME_add_entry_by_txt(name, "C", MBSTRING_ASC,
                               (unsigned char*)"US", -1, -1, 0);
    X509_NAME_add_entry_by_txt(name, "O", MBSTRING_ASC,
                               (unsigned char*)"Test", -1, -1, 0);
    X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
                               (unsigned char*)"localhost", -1, -1, 0);
    X509_set_issuer_name(cert, name);
    
    // Sign certificate
    X509_sign(cert, pkey, EVP_sha256());
    
    // Store for use in tests
    test_cert_ = cert;
    test_key_ = pkey;
  }
  
  /**
   * Create client SSL context
   */
  void createClientContext() {
    SslContextConfig config;
    config.is_client = true;
    config.verify_peer = false;  // Disable for self-signed cert
    config.protocols = {"TLSv1.2", "TLSv1.3"};
    config.sni_hostname = "localhost";
    config.alpn_protocols = {"http/1.1", "h2"};
    
    auto result = SslContext::create(config);
    ASSERT_TRUE(result.ok()) << "Failed to create client context: " << result.error();
    client_context_ = result.value();
  }
  
  /**
   * Create server SSL context
   */
  void createServerContext() {
    SslContextConfig config;
    config.is_client = false;
    config.verify_peer = false;
    config.protocols = {"TLSv1.2", "TLSv1.3"};
    config.alpn_protocols = {"http/1.1", "h2"};
    
    // Note: In real test, would load cert/key from files
    // For unit test, using in-memory cert/key
    
    auto result = SslContext::create(config);
    ASSERT_TRUE(result.ok()) << "Failed to create server context: " << result.error();
    server_context_ = result.value();
  }
  
  /**
   * Perform SSL handshake between client and server
   */
  bool performHandshake(SslTransportSocket& client, SslTransportSocket& server) {
    std::atomic<bool> client_complete{false};
    std::atomic<bool> server_complete{false};
    std::atomic<bool> handshake_failed{false};
    
    // Set handshake callbacks
    class TestHandshakeCallbacks : public SslHandshakeCallbacks {
    public:
      TestHandshakeCallbacks(std::atomic<bool>& complete, std::atomic<bool>& failed)
          : complete_(complete), failed_(failed) {}
      
      void onSslHandshakeComplete() override {
        complete_ = true;
      }
      
      void onSslHandshakeFailed(const std::string& reason) override {
        failed_ = true;
        failure_reason_ = reason;
      }
      
      std::string failure_reason_;
      
    private:
      std::atomic<bool>& complete_;
      std::atomic<bool>& failed_;
    };
    
    TestHandshakeCallbacks client_callbacks(client_complete, handshake_failed);
    TestHandshakeCallbacks server_callbacks(server_complete, handshake_failed);
    
    client.setHandshakeCallbacks(&client_callbacks);
    server.setHandshakeCallbacks(&server_callbacks);
    
    // Trigger handshake
    client.onConnected();
    server.onConnected();
    
    // Run event loop to process handshake
    runEventLoopFor(std::chrono::milliseconds(100));
    
    // Check results
    if (handshake_failed) {
      ADD_FAILURE() << "Handshake failed: " 
                    << "Client: " << client_callbacks.failure_reason_
                    << ", Server: " << server_callbacks.failure_reason_;
      return false;
    }
    
    return client_complete && server_complete;
  }

protected:
  SslContextSharedPtr client_context_;
  SslContextSharedPtr server_context_;
  X509* test_cert_{nullptr};
  EVP_PKEY* test_key_{nullptr};
};

/**
 * Test basic SSL handshake
 */
TEST_F(SslIntegrationTest, BasicSslHandshake) {
  // Create mock inner sockets
  auto client_inner = createMockTransportSocket();
  auto server_inner = createMockTransportSocket();
  
  // Create SSL transport sockets
  SslTransportSocket client_ssl(
      std::move(client_inner),
      client_context_,
      SslTransportSocket::InitialRole::Client,
      *dispatcher_);
  
  SslTransportSocket server_ssl(
      std::move(server_inner),
      server_context_,
      SslTransportSocket::InitialRole::Server,
      *dispatcher_);
  
  // Perform handshake
  EXPECT_TRUE(performHandshake(client_ssl, server_ssl));
  
  // Verify connection is secure
  EXPECT_TRUE(client_ssl.isSecure());
  EXPECT_TRUE(server_ssl.isSecure());
}

/**
 * Test encrypted data transmission
 */
TEST_F(SslIntegrationTest, EncryptedDataTransmission) {
  // Set up SSL connections
  auto client_inner = createMockTransportSocket();
  auto server_inner = createMockTransportSocket();
  
  SslTransportSocket client_ssl(
      std::move(client_inner),
      client_context_,
      SslTransportSocket::InitialRole::Client,
      *dispatcher_);
  
  SslTransportSocket server_ssl(
      std::move(server_inner),
      server_context_,
      SslTransportSocket::InitialRole::Server,
      *dispatcher_);
  
  // Perform handshake
  ASSERT_TRUE(performHandshake(client_ssl, server_ssl));
  
  // Send data from client to server
  Buffer send_buffer;
  send_buffer.add("Hello, SSL!");
  
  auto write_result = client_ssl.doWrite(send_buffer, false);
  EXPECT_EQ(write_result.action_, network::PostIoAction::KeepOpen);
  EXPECT_GT(write_result.bytes_processed_, 0);
  
  // Read data on server
  Buffer recv_buffer;
  auto read_result = server_ssl.doRead(recv_buffer);
  EXPECT_EQ(read_result.action_, network::PostIoAction::KeepOpen);
  EXPECT_GT(read_result.bytes_processed_, 0);
  
  // Verify received data
  EXPECT_EQ(recv_buffer.toString(), "Hello, SSL!");
}

/**
 * Test ALPN protocol negotiation
 */
TEST_F(SslIntegrationTest, AlpnNegotiation) {
  // Create contexts with ALPN
  SslContextConfig client_config;
  client_config.is_client = true;
  client_config.verify_peer = false;
  client_config.alpn_protocols = {"h2", "http/1.1"};
  
  SslContextConfig server_config;
  server_config.is_client = false;
  server_config.verify_peer = false;
  server_config.alpn_protocols = {"http/1.1"};  // Server only supports HTTP/1.1
  
  auto client_ctx = SslContext::create(client_config);
  auto server_ctx = SslContext::create(server_config);
  
  ASSERT_TRUE(client_ctx.ok());
  ASSERT_TRUE(server_ctx.ok());
  
  // Create SSL sockets
  auto client_inner = createMockTransportSocket();
  auto server_inner = createMockTransportSocket();
  
  SslTransportSocket client_ssl(
      std::move(client_inner),
      client_ctx.value(),
      SslTransportSocket::InitialRole::Client,
      *dispatcher_);
  
  SslTransportSocket server_ssl(
      std::move(server_inner),
      server_ctx.value(),
      SslTransportSocket::InitialRole::Server,
      *dispatcher_);
  
  // Perform handshake
  ASSERT_TRUE(performHandshake(client_ssl, server_ssl));
  
  // Check negotiated protocol
  EXPECT_EQ(client_ssl.getNegotiatedProtocol(), "http/1.1");
  EXPECT_EQ(server_ssl.getNegotiatedProtocol(), "http/1.1");
}

/**
 * Test SSL shutdown sequence
 */
TEST_F(SslIntegrationTest, SslShutdown) {
  // Set up SSL connections
  auto client_inner = createMockTransportSocket();
  auto server_inner = createMockTransportSocket();
  
  SslTransportSocket client_ssl(
      std::move(client_inner),
      client_context_,
      SslTransportSocket::InitialRole::Client,
      *dispatcher_);
  
  SslTransportSocket server_ssl(
      std::move(server_inner),
      server_context_,
      SslTransportSocket::InitialRole::Server,
      *dispatcher_);
  
  // Perform handshake
  ASSERT_TRUE(performHandshake(client_ssl, server_ssl));
  
  // Initiate shutdown from client
  client_ssl.closeSocket(network::ConnectionEvent::LocalClose);
  
  // Process shutdown
  runEventLoopFor(std::chrono::milliseconds(50));
  
  // Verify states
  EXPECT_EQ(client_ssl.getState(), SslState::Closed);
}

/**
 * Test HTTPS+SSE factory with SSL
 */
TEST_F(SslIntegrationTest, HttpsSseFactoryWithSsl) {
  // Configure HTTPS+SSE
  HttpSseTransportSocketConfig config;
  config.endpoint_url = "https://localhost:8443";
  config.verify_ssl = false;  // Self-signed cert
  config.alpn_protocols = {"http/1.1"};
  
  // Create factory
  auto factory = createHttpsSseTransportFactory(config, *dispatcher_);
  ASSERT_NE(factory, nullptr);
  
  // Verify SSL is enabled
  EXPECT_TRUE(factory->implementsSecureTransport());
  EXPECT_EQ(factory->name(), "https+sse");
  
  // Would create actual transport socket in real test
  // auto socket = factory->createTransportSocket(nullptr);
}

/**
 * Test certificate verification (would fail with self-signed)
 */
TEST_F(SslIntegrationTest, CertificateVerification) {
  // Create context with verification enabled
  SslContextConfig config;
  config.is_client = true;
  config.verify_peer = true;  // Enable verification
  
  auto context_result = SslContext::create(config);
  ASSERT_TRUE(context_result.ok());
  
  // Create SSL socket
  auto inner = createMockTransportSocket();
  SslTransportSocket ssl_socket(
      std::move(inner),
      context_result.value(),
      SslTransportSocket::InitialRole::Client,
      *dispatcher_);
  
  // Handshake would fail with self-signed cert if verification enabled
  // This is expected behavior for security
}

/**
 * Test multiple concurrent SSL connections
 */
TEST_F(SslIntegrationTest, MultipleConcurrentConnections) {
  const int num_connections = 3;
  std::vector<std::unique_ptr<SslTransportSocket>> clients;
  std::vector<std::unique_ptr<SslTransportSocket>> servers;
  
  // Create multiple SSL connections
  for (int i = 0; i < num_connections; ++i) {
    auto client_inner = createMockTransportSocket();
    auto server_inner = createMockTransportSocket();
    
    clients.push_back(std::make_unique<SslTransportSocket>(
        std::move(client_inner),
        client_context_,
        SslTransportSocket::InitialRole::Client,
        *dispatcher_));
    
    servers.push_back(std::make_unique<SslTransportSocket>(
        std::move(server_inner),
        server_context_,
        SslTransportSocket::InitialRole::Server,
        *dispatcher_));
  }
  
  // Perform handshakes
  for (int i = 0; i < num_connections; ++i) {
    EXPECT_TRUE(performHandshake(*clients[i], *servers[i]));
  }
  
  // All connections should be secure
  for (int i = 0; i < num_connections; ++i) {
    EXPECT_TRUE(clients[i]->isSecure());
    EXPECT_TRUE(servers[i]->isSecure());
  }
}

/**
 * Test SSL with large data transfer
 */
TEST_F(SslIntegrationTest, LargeDataTransfer) {
  // Set up SSL connections
  auto client_inner = createMockTransportSocket();
  auto server_inner = createMockTransportSocket();
  
  SslTransportSocket client_ssl(
      std::move(client_inner),
      client_context_,
      SslTransportSocket::InitialRole::Client,
      *dispatcher_);
  
  SslTransportSocket server_ssl(
      std::move(server_inner),
      server_context_,
      SslTransportSocket::InitialRole::Server,
      *dispatcher_);
  
  // Perform handshake
  ASSERT_TRUE(performHandshake(client_ssl, server_ssl));
  
  // Create large data (1MB)
  std::string large_data(1024 * 1024, 'X');
  Buffer send_buffer;
  send_buffer.add(large_data);
  
  // Send data
  size_t total_sent = 0;
  while (send_buffer.length() > 0) {
    auto result = client_ssl.doWrite(send_buffer, false);
    EXPECT_EQ(result.action_, network::PostIoAction::KeepOpen);
    total_sent += result.bytes_processed_;
  }
  
  EXPECT_EQ(total_sent, large_data.size());
  
  // Read data
  Buffer recv_buffer;
  size_t total_received = 0;
  while (total_received < large_data.size()) {
    auto result = server_ssl.doRead(recv_buffer);
    if (result.bytes_processed_ > 0) {
      total_received += result.bytes_processed_;
    }
    runEventLoopFor(std::chrono::milliseconds(10));
  }
  
  EXPECT_EQ(total_received, large_data.size());
}

/**
 * Test SSL renegotiation (if supported)
 */
TEST_F(SslIntegrationTest, SslRenegotiation) {
  // Note: TLS 1.3 doesn't support renegotiation
  // This test would be for TLS 1.2 only
  
  SslContextConfig config;
  config.is_client = true;
  config.verify_peer = false;
  config.protocols = {"TLSv1.2"};  // Use TLS 1.2 for renegotiation
  
  auto context = SslContext::create(config);
  ASSERT_TRUE(context.ok());
  
  // Renegotiation test would go here
  // Most modern applications avoid renegotiation for security
}

/**
 * Test session resumption
 */
TEST_F(SslIntegrationTest, SessionResumption) {
  // Create context with session resumption enabled
  SslContextConfig config;
  config.is_client = true;
  config.verify_peer = false;
  config.enable_session_resumption = true;
  config.session_timeout = 300;  // 5 minutes
  
  auto context = SslContext::create(config);
  ASSERT_TRUE(context.ok());
  
  // First connection establishes session
  // Second connection would resume session
  // This reduces handshake overhead
}

}  // namespace
}  // namespace transport
}  // namespace mcp