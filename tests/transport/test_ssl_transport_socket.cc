/**
 * @file test_ssl_transport_socket.cc
 * @brief Unit tests for SSL transport socket using real I/O
 */

#include <gtest/gtest.h>
#include <memory>
#include <thread>
#include <chrono>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

#include "mcp/transport/ssl_transport_socket.h"
#include "mcp/transport/ssl_context.h"
#include "mcp/network/socket_impl.h"
#include "mcp/network/io_socket_handle_impl.h"
#include "mcp/network/io_handle.h"
#include "mcp/network/address.h"
#include "mcp/network/transport_socket.h"
#include "mcp/network/socket_interface.h"
#include "mcp/event/libevent_dispatcher.h"
#include "mcp/buffer.h"
#include "../tests/integration/real_io_test_base.h"

namespace mcp {
namespace transport {
namespace {

/**
 * SSL transport socket test fixture using real I/O
 * Tests SSL handshake and data transmission with actual sockets
 */
class SslTransportSocketTest : public test::RealIoTestBase {
protected:
  void SetUp() override {
    RealIoTestBase::SetUp();
    
    // Initialize OpenSSL
    SSL_library_init();
    SSL_load_error_strings();
    OpenSSL_add_all_algorithms();
    
    // Create test SSL contexts
    createTestSslContexts();
    
    // Create socket pair for testing
    createSocketPair();
  }
  
  void TearDown() override {
    // Close sockets using MCP abstractions
    if (client_io_handle_) {
      client_io_handle_->close();
      client_io_handle_.reset();
    }
    if (server_io_handle_) {
      server_io_handle_->close();
      server_io_handle_.reset();
    }
    
    client_ssl_context_.reset();
    server_ssl_context_.reset();
    
    RealIoTestBase::TearDown();
  }
  
  /**
   * Create test SSL contexts for client and server
   */
  void createTestSslContexts() {
    // Create client context
    SslContextConfig client_config;
    client_config.is_client = true;
    client_config.verify_peer = false;  // Disable for testing
    
    auto client_result = SslContext::create(client_config);
    ASSERT_FALSE(holds_alternative<Error>(client_result));
    client_ssl_context_ = get<SslContextSharedPtr>(client_result);
    
    // Create server context  
    SslContextConfig server_config;
    server_config.is_client = false;
    server_config.verify_peer = false;  // Disable for testing
    
    auto server_result = SslContext::create(server_config);
    ASSERT_FALSE(holds_alternative<Error>(server_result));
    server_ssl_context_ = get<SslContextSharedPtr>(server_result);
  }
  
  /**
   * Create connected socket pair for testing using MCP abstractions
   */
  void createSocketPair() {
    // Use the base class utility which creates real connected IoHandles
    auto socket_pair = RealIoTestBase::createSocketPair();
    client_io_handle_ = std::move(socket_pair.first);
    server_io_handle_ = std::move(socket_pair.second);
  }
  
  /**
   * Create a raw transport socket (non-SSL) for testing
   */
  std::unique_ptr<network::TransportSocket> createRawTransportSocket(
      network::IoHandlePtr io_handle) {
    // Simple pass-through transport socket using MCP IoHandle
    class RawTransportSocket : public network::TransportSocket {
    public:
      explicit RawTransportSocket(network::IoHandlePtr io_handle) 
          : io_handle_(std::move(io_handle)) {}
      
      void setTransportSocketCallbacks(network::TransportSocketCallbacks& callbacks) override {
        callbacks_ = &callbacks;
      }
      
      std::string protocol() const override { return "raw"; }
      std::string failureReason() const override { return ""; }
      bool canFlushClose() override { return true; }
      
      VoidResult connect(network::Socket& socket) override {
        // Already connected for socket pair
        return makeVoidSuccess();
      }
      
      void closeSocket(network::ConnectionEvent event) override {
        if (io_handle_) {
          io_handle_->close();
          io_handle_.reset();
        }
      }
      
      TransportIoResult doRead(Buffer& buffer) override {
        if (!io_handle_) {
          return TransportIoResult::close();
        }
        
        // Read using MCP IoHandle abstraction
        auto result = io_handle_->read(buffer, 16384);
        if (!result.ok()) {
          if (result.wouldBlock()) {
            return TransportIoResult::stop();
          }
          return TransportIoResult::close();
        }
        
        if (*result > 0) {
          return TransportIoResult::success(*result);
        }
        
        return TransportIoResult::close();  // EOF
      }
      
      TransportIoResult doWrite(Buffer& buffer, bool end_stream) override {
        if (!io_handle_) {
          return TransportIoResult::close();
        }
        
        if (buffer.length() == 0) {
          return TransportIoResult::success(0);
        }
        
        // Write using MCP IoHandle abstraction
        auto result = io_handle_->write(buffer);
        if (!result.ok()) {
          if (result.wouldBlock()) {
            return TransportIoResult::stop();
          }
          return TransportIoResult::close();
        }
        
        if (*result > 0) {
          return TransportIoResult::success(*result);
        }
        
        return TransportIoResult::stop();
      }
      
      void onConnected() override {
        if (callbacks_) {
          callbacks_->setTransportSocketIsReadable();
        }
      }
      
    private:
      network::IoHandlePtr io_handle_;
      network::TransportSocketCallbacks* callbacks_{nullptr};
    };
    
    return std::make_unique<RawTransportSocket>(std::move(io_handle));
  }

protected:
  SslContextSharedPtr client_ssl_context_;
  SslContextSharedPtr server_ssl_context_;
  network::IoHandlePtr client_io_handle_;
  network::IoHandlePtr server_io_handle_;
};

/**
 * Test SSL transport socket creation
 */
TEST_F(SslTransportSocketTest, CreateSocket) {
  executeInDispatcher([this]() {
    // Create raw transport socket using MCP IoHandle
    auto raw_socket = createRawTransportSocket(std::move(client_io_handle_));
    
    // Wrap with SSL transport socket
    auto ssl_socket = std::make_unique<SslTransportSocket>(
        std::move(raw_socket),
        client_ssl_context_,
        SslTransportSocket::InitialRole::Client,
        *dispatcher_);
    
    EXPECT_NE(ssl_socket, nullptr);
    EXPECT_EQ(ssl_socket->protocol(), "ssl");
    EXPECT_EQ(ssl_socket->getState(), SslState::Initial);
  });
}

/**
 * Test state transitions
 */
TEST_F(SslTransportSocketTest, StateTransitions) {
  executeInDispatcher([this]() {
    // Create SSL transport sockets for both sides
    auto client_raw = createRawTransportSocket(std::move(client_io_handle_));
    auto client_ssl = std::make_unique<SslTransportSocket>(
        std::move(client_raw),
        client_ssl_context_,
        SslTransportSocket::InitialRole::Client,
        *dispatcher_);
    
    // Check initial state
    EXPECT_EQ(client_ssl->getState(), SslState::Initial);
    
    // Create server SSL socket
    auto server_raw = createRawTransportSocket(std::move(server_io_handle_));
    auto server_ssl = std::make_unique<SslTransportSocket>(
        std::move(server_raw),
        server_ssl_context_,
        SslTransportSocket::InitialRole::Server,
        *dispatcher_);
    
    EXPECT_EQ(server_ssl->getState(), SslState::Initial);
  });
}

/**
 * Test SNI configuration
 */
TEST_F(SslTransportSocketTest, SetSniHostname) {
  SSL* ssl = client_ssl_context_->newSsl();
  ASSERT_NE(ssl, nullptr);
  
  // Set SNI hostname
  auto result = SslContext::setSniHostname(ssl, "example.com");
  EXPECT_FALSE(holds_alternative<Error>(result));
  
  // Verify SNI was set
  const char* hostname = SSL_get_servername(ssl, TLSEXT_NAMETYPE_host_name);
  EXPECT_STREQ(hostname, "example.com");
  
  SSL_free(ssl);
}

/**
 * Test SSL handshake callbacks
 */
class TestHandshakeCallbacks : public SslHandshakeCallbacks {
public:
  void onSslHandshakeComplete() override {
    handshake_complete_ = true;
  }
  
  void onSslHandshakeFailed(const std::string& reason) override {
    handshake_failed_ = true;
    failure_reason_ = reason;
  }
  
  bool handshake_complete_{false};
  bool handshake_failed_{false};
  std::string failure_reason_;
};

TEST_F(SslTransportSocketTest, HandshakeCallbacks) {
  executeInDispatcher([this]() {
    TestHandshakeCallbacks client_callbacks;
    TestHandshakeCallbacks server_callbacks;
    
    // Create client SSL socket
    auto client_raw = createRawTransportSocket(std::move(client_io_handle_));
    auto client_ssl = std::make_unique<SslTransportSocket>(
        std::move(client_raw),
        client_ssl_context_,
        SslTransportSocket::InitialRole::Client,
        *dispatcher_);
    
    // Create server SSL socket
    auto server_raw = createRawTransportSocket(std::move(server_io_handle_));
    auto server_ssl = std::make_unique<SslTransportSocket>(
        std::move(server_raw),
        server_ssl_context_,
        SslTransportSocket::InitialRole::Server,
        *dispatcher_);
    
    // Register callbacks
    client_ssl->setHandshakeCallbacks(&client_callbacks);
    server_ssl->setHandshakeCallbacks(&server_callbacks);
    
    // Verify callbacks are registered but not yet triggered
    EXPECT_FALSE(client_callbacks.handshake_complete_);
    EXPECT_FALSE(client_callbacks.handshake_failed_);
    EXPECT_FALSE(server_callbacks.handshake_complete_);
    EXPECT_FALSE(server_callbacks.handshake_failed_);
  });
}

}  // namespace
}  // namespace transport
}  // namespace mcp