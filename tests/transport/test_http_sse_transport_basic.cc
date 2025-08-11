/**
 * Basic tests for HTTP+SSE transport socket
 * 
 * These tests verify that the transport compiles and basic operations work
 * without fully simulating the complex network interactions
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "mcp/transport/http_sse_transport_socket.h"
#include "mcp/http/llhttp_parser.h"
#include "mcp/event/libevent_dispatcher.h"
#include "mcp/buffer.h"

namespace mcp {
namespace transport {
namespace {

using ::testing::_;
using ::testing::Return;
using ::testing::NiceMock;

// Basic test fixture
class HttpSseTransportBasicTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Setup basic config
    config_.endpoint_url = "api.example.com";
    config_.request_endpoint_path = "/rpc";
    config_.sse_endpoint_path = "/events";
    config_.parser_factory = std::make_shared<http::LLHttpParserFactory>();
    
    // Create a real dispatcher for basic testing
    dispatcher_ = std::make_unique<event::LibeventDispatcher>("test");
  }
  
  void TearDown() override {
    transport_socket_.reset();
    dispatcher_.reset();
  }
  
  HttpSseTransportSocketConfig config_;
  std::unique_ptr<event::Dispatcher> dispatcher_;
  std::unique_ptr<HttpSseTransportSocket> transport_socket_;
};

TEST_F(HttpSseTransportBasicTest, CanCreateTransport) {
  // Simply verify we can create the transport
  EXPECT_NO_THROW({
    transport_socket_ = std::make_unique<HttpSseTransportSocket>(config_, *dispatcher_);
  });
  
  EXPECT_NE(nullptr, transport_socket_);
  EXPECT_EQ("http+sse", transport_socket_->protocol());
}

TEST_F(HttpSseTransportBasicTest, InitialStateIsCorrect) {
  transport_socket_ = std::make_unique<HttpSseTransportSocket>(config_, *dispatcher_);
  
  // Initial state checks
  EXPECT_EQ("http+sse", transport_socket_->protocol());
  EXPECT_TRUE(transport_socket_->failureReason().empty());
  EXPECT_FALSE(transport_socket_->canFlushClose());
}

TEST_F(HttpSseTransportBasicTest, FactoryCreatesTransport) {
  HttpSseTransportSocketFactory factory(config_, *dispatcher_);
  
  EXPECT_EQ(config_.verify_ssl, factory.implementsSecureTransport());
  
  auto transport = factory.createTransportSocket(nullptr);
  EXPECT_NE(nullptr, transport);
}

} // namespace
} // namespace transport
} // namespace mcp