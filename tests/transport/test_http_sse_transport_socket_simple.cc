/**
 * @file test_http_sse_transport_socket_simple.cc
 * @brief Simple tests for HTTP+SSE transport socket
 */

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "mcp/event/libevent_dispatcher.h"
#include "mcp/network/address_impl.h"
#include "mcp/network/socket_impl.h"
#include "mcp/transport/http_sse_transport_socket.h"

namespace mcp {
namespace transport {
namespace {

using namespace std::chrono_literals;
using ::testing::_;

class HttpSseTransportSocketTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Create dispatcher
    auto factory = event::createLibeventDispatcherFactory();
    dispatcher_ = factory->createDispatcher("test");

    // Run once to initialize thread ID
    dispatcher_->run(event::RunType::NonBlock);

    // Default configuration
    config_.mode = HttpSseTransportSocketConfig::Mode::CLIENT;
    config_.underlying_transport =
        HttpSseTransportSocketConfig::UnderlyingTransport::TCP;
    config_.server_address = "127.0.0.1:8080";
    config_.connect_timeout = 5000ms;
    config_.idle_timeout = 30000ms;
  }

  std::unique_ptr<HttpSseTransportSocket> createTransport() {
    HttpSseTransportBuilder builder(*dispatcher_);
    return builder.withMode(config_.mode)
        .withServerAddress(config_.server_address)
        .withConnectTimeout(config_.connect_timeout)
        .withIdleTimeout(config_.idle_timeout)
        .build();
  }

  HttpSseTransportSocketConfig config_;
  std::unique_ptr<event::Dispatcher> dispatcher_;
};

// ===== Basic Tests =====

TEST_F(HttpSseTransportSocketTest, CreateAndDestroy) {
  auto transport = createTransport();
  ASSERT_NE(transport, nullptr);
  EXPECT_EQ(transport->protocol(), "http+sse");
  EXPECT_FALSE(transport->isConnected());
}

TEST_F(HttpSseTransportSocketTest, CanFlushClose) {
  auto transport = createTransport();
  EXPECT_TRUE(transport->canFlushClose());
}

TEST_F(HttpSseTransportSocketTest, ReadWhenNotConnected) {
  auto transport = createTransport();

  OwnedBuffer buffer;
  auto result = transport->doRead(buffer);

  EXPECT_TRUE(result.error_.has_value());
  EXPECT_EQ(result.action_, TransportIoResult::CLOSE);
}

TEST_F(HttpSseTransportSocketTest, WriteWhenNotConnected) {
  auto transport = createTransport();

  OwnedBuffer buffer;
  buffer.add("test data", 9);

  auto result = transport->doWrite(buffer, false);

  EXPECT_TRUE(result.error_.has_value());
  EXPECT_EQ(result.action_, TransportIoResult::CLOSE);
}

// ===== Builder Tests =====

TEST_F(HttpSseTransportSocketTest, BuilderConfiguration) {
  HttpSseTransportBuilder builder(*dispatcher_);

  auto transport = builder.withMode(HttpSseTransportSocketConfig::Mode::SERVER)
                       .withServerAddress("192.168.1.1:9090")
                       .withConnectTimeout(10s)
                       .withIdleTimeout(60s)
                       .withHttpFilter(true)
                       .withSseFilter(true)
                       .build();

  ASSERT_NE(transport, nullptr);
  EXPECT_EQ(transport->protocol(), "http+sse");
}

// ===== Factory Tests =====

TEST_F(HttpSseTransportSocketTest, FactoryCreateTransport) {
  HttpSseTransportBuilder builder(*dispatcher_);
  auto factory = builder.withMode(HttpSseTransportSocketConfig::Mode::CLIENT)
                     .buildFactory();

  ASSERT_NE(factory, nullptr);
  EXPECT_EQ(factory->name(), "http+sse-v2");
  EXPECT_FALSE(factory->implementsSecureTransport());

  auto transport = factory->createTransportSocket();
  ASSERT_NE(transport, nullptr);
}

// ===== Statistics Tests =====

TEST_F(HttpSseTransportSocketTest, TransportStatistics) {
  auto transport = createTransport();

  // Initial stats
  auto stats = transport->stats();
  EXPECT_EQ(stats.bytes_sent, 0);
  EXPECT_EQ(stats.bytes_received, 0);
  EXPECT_EQ(stats.connect_attempts, 0);
}

}  // namespace
}  // namespace transport
}  // namespace mcp