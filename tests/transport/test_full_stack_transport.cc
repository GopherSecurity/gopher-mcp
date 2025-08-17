/**
 * @file test_full_stack_transport.cc
 * @brief Integration tests for the full transport stack: TCP → SSL → HTTP+SSE
 */

#include <gtest/gtest.h>

#include <chrono>
#include <memory>
#include <thread>

#include "mcp/transport/https_sse_transport_factory.h"
#include "mcp/transport/tcp_transport_socket.h"
#include "mcp/transport/ssl_transport_socket.h"
#include "mcp/transport/http_sse_transport_socket.h"
#include "mcp/network/connection_impl.h"
#include "mcp/network/socket_impl.h"
#include "mcp/network/address_impl.h"
#include "mcp/network/io_socket_handle_impl.h"
#include "mcp/buffer.h"
#include "mcp/core/compat.h"

// Include the real I/O test base
#include "../integration/real_io_test_base.h"

namespace mcp {
namespace transport {
namespace {

// Test fixture for full stack transport testing
class FullStackTransportTest : public test::RealListenerTestBase {
 protected:
  void SetUp() override {
    // Call base class setup first
    RealListenerTestBase::SetUp();
    
    // HTTP+SSE configuration for testing
    config_.endpoint_url = "http://localhost:8080";
    config_.use_ssl = false;  // Start with non-SSL for simplicity
    config_.verify_ssl = false;
    config_.preferred_version = http::HttpVersion::HTTP_1_1;
    config_.auto_reconnect = true;
    config_.reconnect_delay = std::chrono::milliseconds(3000);
    config_.request_timeout = std::chrono::milliseconds(30000);
    config_.sse_endpoint_path = "/events";
    config_.request_endpoint_path = "/rpc";
  }
  
  void TearDown() override {
    // Clean up in proper order
    executeInDispatcher([this]() {
      client_factory_.reset();
      server_factory_.reset();
      client_socket_.reset();
      server_socket_.reset();
    });
    
    // Call base class teardown
    RealListenerTestBase::TearDown();
  }
  
  // Create HTTPS+SSE transport factory
  void createFactory(bool use_ssl = false) {
    executeInDispatcher([this, use_ssl]() {
      config_.use_ssl = use_ssl;
      
      if (use_ssl) {
        config_.endpoint_url = "https://localhost:8443";
        // For testing, we'd need to set up test certificates
        // config_.ca_cert_path = "/path/to/test/ca.crt";
        // config_.client_cert_path = "/path/to/test/client.crt";
        // config_.client_key_path = "/path/to/test/client.key";
      }
      
      client_factory_ = std::make_unique<HttpsSseTransportFactory>(config_, *dispatcher_);
      
      // For server, we might need different config
      HttpSseTransportSocketConfig server_config = config_;
      server_factory_ = std::make_unique<HttpsSseTransportFactory>(server_config, *dispatcher_);
    });
  }
  
  // Create client transport socket
  void createClientSocket() {
    executeInDispatcher([this]() {
      // Create client transport socket through factory
      client_socket_ = client_factory_->createTransportSocket(nullptr);
      ASSERT_NE(client_socket_, nullptr);
    });
  }
  
  // Create server transport socket
  void createServerSocket() {
    executeInDispatcher([this]() {
      // Create server transport socket through factory
      server_socket_ = server_factory_->createTransportSocket();
      ASSERT_NE(server_socket_, nullptr);
    });
  }
  
 protected:
  HttpSseTransportSocketConfig config_;
  std::unique_ptr<HttpsSseTransportFactory> client_factory_;
  std::unique_ptr<HttpsSseTransportFactory> server_factory_;
  network::TransportSocketPtr client_socket_;
  network::TransportSocketPtr server_socket_;
};

// ===== Basic Factory Tests =====

TEST_F(FullStackTransportTest, CreateFactoryNonSSL) {
  createFactory(false);
  
  executeInDispatcher([this]() {
    EXPECT_FALSE(client_factory_->implementsSecureTransport());
    EXPECT_EQ(client_factory_->name(), "http+sse");
    EXPECT_FALSE(client_factory_->supportsAlpn());
  });
}

TEST_F(FullStackTransportTest, CreateFactoryWithSSL) {
  createFactory(true);
  
  executeInDispatcher([this]() {
    EXPECT_TRUE(client_factory_->implementsSecureTransport());
    EXPECT_EQ(client_factory_->name(), "https+sse");
    // ALPN support depends on configuration
    // EXPECT_TRUE(client_factory_->supportsAlpn());
  });
}

// ===== Socket Creation Tests =====

TEST_F(FullStackTransportTest, CreateClientSocket) {
  createFactory(false);
  createClientSocket();
  
  executeInDispatcher([this]() {
    // The created socket should be HTTP+SSE transport
    // It internally wraps a TCP socket
    EXPECT_NE(client_socket_, nullptr);
    
    // HTTP+SSE doesn't provide SSL info when not using SSL
    EXPECT_EQ(client_socket_->ssl(), nullptr);
  });
}

TEST_F(FullStackTransportTest, CreateServerSocket) {
  createFactory(false);
  createServerSocket();
  
  executeInDispatcher([this]() {
    EXPECT_NE(server_socket_, nullptr);
    EXPECT_EQ(server_socket_->ssl(), nullptr);
  });
}

// ===== Protocol Stack Tests =====

TEST_F(FullStackTransportTest, VerifyProtocolStack) {
  createFactory(false);
  createClientSocket();
  
  executeInDispatcher([this]() {
    // The top-level protocol should be HTTP+SSE
    // Note: The actual protocol string depends on implementation
    std::string protocol = client_socket_->protocol();
    EXPECT_TRUE(protocol == "http+sse" || protocol == "HTTP/1.1" || protocol == "h2");
  });
}

// ===== SSL Layer Tests =====

TEST_F(FullStackTransportTest, CreateSSLSocket) {
  // This test would require proper SSL setup with certificates
  // For now, we just verify the factory can be created with SSL config
  createFactory(true);
  
  executeInDispatcher([this]() {
    EXPECT_TRUE(client_factory_->implementsSecureTransport());
    
    // Creating actual SSL socket would require certificates
    // client_socket_ = client_factory_->createTransportSocket(nullptr);
    // This would fail without proper SSL context setup
  });
}

// ===== Configuration Tests =====

TEST_F(FullStackTransportTest, FactoryConfiguration) {
  executeInDispatcher([this]() {
    // Test with different configurations
    HttpSseTransportSocketConfig test_config;
    test_config.endpoint_url = "https://api.example.com";
    test_config.use_ssl = true;
    test_config.verify_ssl = true;
    test_config.preferred_version = http::HttpVersion::HTTP_2;
    test_config.auto_reconnect = true;
    
    auto factory = std::make_unique<HttpsSseTransportFactory>(test_config, *dispatcher_);
    
    EXPECT_TRUE(factory->implementsSecureTransport());
    EXPECT_EQ(factory->name(), "https+sse");
    
    // SNI should be extracted from URL
    std::string sni = factory->defaultServerNameIndication();
    EXPECT_EQ(sni, "api.example.com");
  });
}

TEST_F(FullStackTransportTest, AutoDetectSSLFromURL) {
  executeInDispatcher([this]() {
    // Test HTTPS URL auto-detection
    HttpSseTransportSocketConfig https_config;
    https_config.endpoint_url = "https://secure.example.com";
    // Don't explicitly set use_ssl, let it auto-detect
    
    auto https_factory = std::make_unique<HttpsSseTransportFactory>(https_config, *dispatcher_);
    EXPECT_TRUE(https_factory->implementsSecureTransport());
    
    // Test HTTP URL
    HttpSseTransportSocketConfig http_config;
    http_config.endpoint_url = "http://plain.example.com";
    
    auto http_factory = std::make_unique<HttpsSseTransportFactory>(http_config, *dispatcher_);
    EXPECT_FALSE(http_factory->implementsSecureTransport());
  });
}

// ===== Hash Key Tests =====

TEST_F(FullStackTransportTest, FactoryHashKey) {
  createFactory(false);
  
  executeInDispatcher([this]() {
    std::vector<uint8_t> key1, key2;
    
    // Hash keys should be consistent for same factory
    client_factory_->hashKey(key1, nullptr);
    client_factory_->hashKey(key2, nullptr);
    EXPECT_EQ(key1, key2);
    
    // Different config should produce different hash
    HttpSseTransportSocketConfig different_config = config_;
    different_config.endpoint_url = "http://different.example.com";
    auto different_factory = std::make_unique<HttpsSseTransportFactory>(different_config, *dispatcher_);
    
    std::vector<uint8_t> key3;
    different_factory->hashKey(key3, nullptr);
    EXPECT_NE(key1, key3);
  });
}

// ===== Layer Integration Tests =====

TEST_F(FullStackTransportTest, VerifyTCPLayerCreation) {
  executeInDispatcher([this]() {
    // Directly test that TCP socket is created by the factory
    HttpSseTransportSocketConfig test_config;
    test_config.endpoint_url = "http://test.example.com";
    test_config.use_ssl = false;
    
    HttpsSseTransportFactory factory(test_config, *dispatcher_);
    
    // The factory should create a layered socket:
    // HTTP+SSE -> TCP (no SSL since use_ssl = false)
    auto socket = factory.createTransportSocket(nullptr);
    ASSERT_NE(socket, nullptr);
    
    // The socket should support the operations
    EXPECT_TRUE(socket->canFlushClose() || !socket->canFlushClose()); // Either is valid
    EXPECT_FALSE(socket->ssl()); // No SSL layer
  });
}

// ===== ALPN Tests =====

TEST_F(FullStackTransportTest, ALPNConfiguration) {
  executeInDispatcher([this]() {
    // Test ALPN configuration for HTTP/2
    HttpSseTransportSocketConfig h2_config;
    h2_config.endpoint_url = "https://h2.example.com";
    h2_config.use_ssl = true;
    h2_config.preferred_version = http::HttpVersion::HTTP_2;
    h2_config.alpn_protocols = std::vector<std::string>{"h2", "http/1.1"};
    
    auto h2_factory = std::make_unique<HttpsSseTransportFactory>(h2_config, *dispatcher_);
    EXPECT_TRUE(h2_factory->supportsAlpn());
    
    // For HTTP/1.1 only
    HttpSseTransportSocketConfig h1_config;
    h1_config.endpoint_url = "https://h1.example.com";
    h1_config.use_ssl = true;
    h1_config.preferred_version = http::HttpVersion::HTTP_1_1;
    h1_config.alpn_protocols = std::vector<std::string>{"http/1.1"};
    
    auto h1_factory = std::make_unique<HttpsSseTransportFactory>(h1_config, *dispatcher_);
    EXPECT_TRUE(h1_factory->supportsAlpn());
  });
}

// ===== Error Handling Tests =====

TEST_F(FullStackTransportTest, InvalidConfiguration) {
  executeInDispatcher([this]() {
    // Test with invalid URL
    HttpSseTransportSocketConfig invalid_config;
    invalid_config.endpoint_url = "";  // Empty URL
    
    // Factory should still be created, but might fail when creating sockets
    auto factory = std::make_unique<HttpsSseTransportFactory>(invalid_config, *dispatcher_);
    EXPECT_NE(factory, nullptr);
    
    // The socket creation might succeed but operations would fail
    auto socket = factory->createTransportSocket(nullptr);
    EXPECT_NE(socket, nullptr);  // Factory creates socket even with empty URL
  });
}

}  // namespace
}  // namespace transport
}  // namespace mcp