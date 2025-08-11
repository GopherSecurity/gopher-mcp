/**
 * Fixed tests for HTTP+SSE transport socket
 * 
 * These tests verify basic functionality without complex mocking
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "mcp/transport/http_sse_transport_socket.h"
#include "mcp/http/llhttp_parser.h"
#include "mcp/json/json_bridge.h"
#include "mcp/buffer.h"
#include "mcp/network/connection.h"
#include "mcp/event/libevent_dispatcher.h"

namespace mcp {
namespace transport {
namespace {

using ::testing::_;
using ::testing::Return;
using ::testing::NiceMock;

// Simple mock callbacks
class SimpleMockCallbacks : public network::TransportSocketCallbacks {
 public:
  MOCK_METHOD(network::IoHandle&, ioHandle, (), (override));
  MOCK_METHOD(const network::IoHandle&, ioHandle, (), (const, override));
  MOCK_METHOD(network::Connection&, connection, (), (override));
  MOCK_METHOD(bool, shouldDrainReadBuffer, (), (override));
  MOCK_METHOD(void, setTransportSocketIsReadable, (), (override));
  MOCK_METHOD(void, raiseEvent, (network::ConnectionEvent), (override));
  MOCK_METHOD(void, flushWriteBuffer, (), (override));
};

class HttpSseTransportFixedTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Setup basic config
    config_.endpoint_url = "api.example.com";
    config_.request_endpoint_path = "/rpc";
    config_.sse_endpoint_path = "/events";
    config_.parser_factory = std::make_shared<http::LLHttpParserFactory>();
    
    // Create real dispatcher
    dispatcher_ = std::make_unique<event::LibeventDispatcher>("test");
    callbacks_ = std::make_unique<NiceMock<SimpleMockCallbacks>>();
  }
  
  void TearDown() override {
    transport_socket_.reset();
    callbacks_.reset();
    dispatcher_.reset();
  }
  
  HttpSseTransportSocketConfig config_;
  std::unique_ptr<event::Dispatcher> dispatcher_;
  std::unique_ptr<NiceMock<SimpleMockCallbacks>> callbacks_;
  std::unique_ptr<HttpSseTransportSocket> transport_socket_;
};

// Test basic creation and protocol
TEST_F(HttpSseTransportFixedTest, BasicCreation) {
  transport_socket_ = std::make_unique<HttpSseTransportSocket>(config_, *dispatcher_);
  EXPECT_EQ("http+sse", transport_socket_->protocol());
  EXPECT_TRUE(transport_socket_->failureReason().empty());
}

// Test setting callbacks
TEST_F(HttpSseTransportFixedTest, SetCallbacks) {
  transport_socket_ = std::make_unique<HttpSseTransportSocket>(config_, *dispatcher_);
  transport_socket_->setTransportSocketCallbacks(*callbacks_);
  EXPECT_EQ("http+sse", transport_socket_->protocol());
}

// Test with auto-reconnect
TEST_F(HttpSseTransportFixedTest, AutoReconnectConfig) {
  config_.auto_reconnect = true;
  transport_socket_ = std::make_unique<HttpSseTransportSocket>(config_, *dispatcher_);
  transport_socket_->setTransportSocketCallbacks(*callbacks_);
  EXPECT_EQ("http+sse", transport_socket_->protocol());
}

// Test factory creation
TEST_F(HttpSseTransportFixedTest, FactoryCreation) {
  HttpSseTransportSocketFactory factory(config_, *dispatcher_);
  EXPECT_EQ(config_.verify_ssl, factory.implementsSecureTransport());
  
  auto transport = factory.createTransportSocket(nullptr);
  EXPECT_NE(nullptr, transport);
  EXPECT_EQ("http+sse", transport->protocol());
}

// Test with SSL verification enabled
TEST_F(HttpSseTransportFixedTest, SslVerification) {
  config_.verify_ssl = true;
  HttpSseTransportSocketFactory factory(config_, *dispatcher_);
  EXPECT_TRUE(factory.implementsSecureTransport());
}

// Test with custom headers
TEST_F(HttpSseTransportFixedTest, CustomHeaders) {
  config_.headers["Authorization"] = "Bearer token";
  config_.headers["X-Custom"] = "value";
  transport_socket_ = std::make_unique<HttpSseTransportSocket>(config_, *dispatcher_);
  EXPECT_EQ("http+sse", transport_socket_->protocol());
}

// Test with custom timeouts
TEST_F(HttpSseTransportFixedTest, CustomTimeouts) {
  config_.connect_timeout = std::chrono::milliseconds(5000);
  config_.request_timeout = std::chrono::milliseconds(10000);
  config_.keepalive_interval = std::chrono::milliseconds(60000);
  transport_socket_ = std::make_unique<HttpSseTransportSocket>(config_, *dispatcher_);
  EXPECT_EQ("http+sse", transport_socket_->protocol());
}

// Test multiple instances
TEST_F(HttpSseTransportFixedTest, MultipleInstances) {
  auto t1 = std::make_unique<HttpSseTransportSocket>(config_, *dispatcher_);
  auto t2 = std::make_unique<HttpSseTransportSocket>(config_, *dispatcher_);
  
  EXPECT_EQ(t1->protocol(), t2->protocol());
  EXPECT_NE(t1.get(), t2.get());
}

// Test with keepalive disabled
TEST_F(HttpSseTransportFixedTest, KeepaliveDisabled) {
  config_.enable_keepalive = false;
  transport_socket_ = std::make_unique<HttpSseTransportSocket>(config_, *dispatcher_);
  EXPECT_EQ("http+sse", transport_socket_->protocol());
}

// Test with different HTTP versions
TEST_F(HttpSseTransportFixedTest, HttpVersions) {
  config_.preferred_version = http::HttpVersion::Http11;
  auto t1 = std::make_unique<HttpSseTransportSocket>(config_, *dispatcher_);
  EXPECT_EQ("http+sse", t1->protocol());
  
  config_.preferred_version = http::HttpVersion::Http2;
  auto t2 = std::make_unique<HttpSseTransportSocket>(config_, *dispatcher_);
  EXPECT_EQ("http+sse", t2->protocol());
}

// Test with max reconnect attempts
TEST_F(HttpSseTransportFixedTest, MaxReconnectAttempts) {
  config_.auto_reconnect = true;
  config_.max_reconnect_attempts = 5;
  transport_socket_ = std::make_unique<HttpSseTransportSocket>(config_, *dispatcher_);
  EXPECT_EQ("http+sse", transport_socket_->protocol());
}

// Test close behavior
TEST_F(HttpSseTransportFixedTest, CloseSocket) {
  transport_socket_ = std::make_unique<HttpSseTransportSocket>(config_, *dispatcher_);
  transport_socket_->setTransportSocketCallbacks(*callbacks_);
  
  // Close should not crash
  transport_socket_->closeSocket(network::ConnectionEvent::LocalClose);
  EXPECT_EQ("http+sse", transport_socket_->protocol());
}

// Test factory with options
TEST_F(HttpSseTransportFixedTest, FactoryWithOptions) {
  HttpSseTransportSocketFactory factory(config_, *dispatcher_);
  
  auto options = std::make_shared<network::TransportSocketOptions>();
  auto transport = factory.createTransportSocket(options);
  EXPECT_NE(nullptr, transport);
}

}  // namespace
}  // namespace transport
}  // namespace mcp