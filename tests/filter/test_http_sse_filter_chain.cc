/**
 * Test HTTP+SSE filter chain following production architecture
 * Uses real I/O for integration testing
 */

#include <chrono>
#include <thread>

#include <gtest/gtest.h>

#include "mcp/filter/http_codec_filter.h"
#include "mcp/filter/mcp_http_filter_chain_factory.h"
#include "mcp/filter/sse_codec_filter.h"
#include "mcp/network/connection_impl.h"
#include "mcp/network/server_listener_impl.h"
#include "mcp/network/transport_socket.h"

#include "tests/test_utils/real_io_test_base.h"

namespace mcp {
namespace filter {
namespace {

using namespace std::chrono_literals;

/**
 * Test fixture for HTTP+SSE filter chain
 * Following production test patterns using real I/O
 */
class HttpSseFilterChainTest : public test::RealIoTestBase {
 protected:
  void SetUp() override {
    RealIoTestBase::SetUp();

    // Create server listener following production pattern
    setupServer();

    // Create client connection
    setupClient();
  }

  void TearDown() override {
    if (client_connection_) {
      client_connection_->close(network::ConnectionCloseType::NoFlush);
    }
    if (server_listener_) {
      server_listener_->disable();
    }
    RealIoTestBase::TearDown();
  }

  void setupServer() {
    // Following production pattern: Configure listener with filter chain
    auto address =
        network::Address::anyAddress(network::Address::IpVersion::v4, 0);

    network::TcpListenerConfig config;
    config.name = "test_http_sse_server";
    config.address = address;
    config.bind_to_port = true;

    // Following production: Transport socket is ONLY for I/O
    config.transport_socket_factory =
        std::make_shared<network::RawBufferTransportSocketFactory>();

    // Following production: Filters handle ALL protocol logic
    config.filter_chain_factory =
        std::make_shared<TestFilterChainFactory>(*dispatcher_, *this);

    // Create listener
    server_listener_ = std::make_unique<network::TcpActiveListener>(
        *dispatcher_, std::move(config), *this);
    server_listener_->enable();

    // Get actual listening port
    server_port_ = server_listener_->listener()->localAddress()->port();
  }

  void setupClient() {
    // Create client socket
    auto socket = createClientSocket();

    // Following production: Use RawBufferTransportSocket for pure I/O
    auto transport_socket =
        std::make_unique<network::RawBufferTransportSocket>();

    // Create client connection
    client_connection_ = std::make_unique<network::ClientConnectionImpl>(
        *dispatcher_, std::move(socket), std::move(transport_socket),
        stream_info_);

    // Add connection callbacks
    client_connection_->addConnectionCallbacks(*this);

    // Connect to server
    auto server_address =
        network::Address::parseInternetAddress("127.0.0.1", server_port_);
    client_connection_->connect(server_address);
  }

  // Test filter chain factory
  class TestFilterChainFactory : public network::FilterChainFactory {
   public:
    TestFilterChainFactory(event::Dispatcher& dispatcher,
                           HttpSseFilterChainTest& test)
        : dispatcher_(dispatcher), test_(test) {}

    bool createNetworkFilterChain(network::Connection& connection,
                                  const std::vector<network::FilterFactoryCb>&
                                      filter_factories) override {
      // Create protocol bridge
      auto bridge = std::make_unique<TestProtocolBridge>(test_);

      // Create HTTP codec filter
      auto http_filter =
          std::make_shared<HttpCodecFilter>(*bridge, dispatcher_, true);
      connection.addReadFilter(http_filter);
      connection.addWriteFilter(http_filter);

      // Create SSE codec filter
      auto sse_filter =
          std::make_shared<SseCodecFilter>(*bridge, true /* server mode */);
      connection.addReadFilter(sse_filter);
      connection.addWriteFilter(sse_filter);

      // Store bridge
      bridge->setFilters(http_filter.get(), sse_filter.get());
      test_.server_bridge_ = std::move(bridge);

      return true;
    }

    bool createUpstreamFilterChain(
        network::Connection& connection,
        const network::UpstreamFilterContext& context) override {
      return false;
    }

   private:
    event::Dispatcher& dispatcher_;
    HttpSseFilterChainTest& test_;
  };

  // Test protocol bridge
  class TestProtocolBridge : public HttpCodecFilter::MessageCallbacks,
                             public SseCodecFilter::EventCallbacks {
   public:
    TestProtocolBridge(HttpSseFilterChainTest& test) : test_(test) {}

    void setFilters(HttpCodecFilter* http, SseCodecFilter* sse) {
      http_filter_ = http;
      sse_filter_ = sse;
    }

    // HTTP callbacks
    void onHeaders(const std::map<std::string, std::string>& headers,
                   bool keep_alive) override {
      test_.server_headers_ = headers;
      test_.server_keep_alive_ = keep_alive;

      // Check if SSE request
      auto accept = headers.find("accept");
      if (accept != headers.end() &&
          accept->second.find("text/event-stream") != std::string::npos) {
        // Send SSE response headers
        std::map<std::string, std::string> response_headers = {
            {"content-type", "text/event-stream"},
            {"cache-control", "no-cache"},
            {"connection", keep_alive ? "keep-alive" : "close"}};
        http_filter_->responseEncoder().encodeHeaders(200, response_headers,
                                                      false);

        // Start SSE stream
        sse_filter_->startEventStream();
        is_sse_ = true;
      }
    }

    void onBody(const std::string& data, bool end_stream) override {
      test_.server_body_ = data;

      if (!is_sse_) {
        // Send HTTP response
        std::map<std::string, std::string> response_headers = {
            {"content-type", "application/json"},
            {"content-length", std::to_string(data.length())}};
        http_filter_->responseEncoder().encodeHeaders(200, response_headers,
                                                      false);

        Buffer response_data;
        response_data.add(data.c_str(), data.length());
        http_filter_->responseEncoder().encodeData(response_data, true);
      }
    }

    void onMessageComplete() override { test_.server_request_complete_ = true; }

    void onError(const std::string& error) override {
      test_.server_error_ = error;
    }

    // SSE callbacks
    void onEvent(const std::string& event,
                 const std::string& data,
                 const optional<std::string>& id) override {
      test_.sse_events_.push_back({event, data, id});

      // Echo event back
      if (sse_filter_) {
        sse_filter_->eventEncoder().encodeEvent(event, data, id);
      }
    }

    void onComment(const std::string& comment) override {
      test_.sse_comments_.push_back(comment);
    }

   private:
    HttpSseFilterChainTest& test_;
    HttpCodecFilter* http_filter_{nullptr};
    SseCodecFilter* sse_filter_{nullptr};
    bool is_sse_{false};
  };

  // Server state
  std::unique_ptr<network::TcpActiveListener> server_listener_;
  uint32_t server_port_{0};
  std::unique_ptr<TestProtocolBridge> server_bridge_;

  // Client state
  std::unique_ptr<network::ClientConnection> client_connection_;

  // Request/response data
  std::map<std::string, std::string> server_headers_;
  bool server_keep_alive_{true};
  std::string server_body_;
  bool server_request_complete_{false};
  std::string server_error_;

  // SSE data
  struct SseEvent {
    std::string event;
    std::string data;
    optional<std::string> id;
  };
  std::vector<SseEvent> sse_events_;
  std::vector<std::string> sse_comments_;
};

/**
 * Test HTTP request/response through filter chain
 * Verifies proper separation of transport and protocol layers
 */
TEST_F(HttpSseFilterChainTest, HttpRequestResponse) {
  // Wait for connection
  ASSERT_TRUE(waitForCondition([this]() {
    return client_connection_->state() == network::Connection::State::Open;
  }));

  // Send HTTP request
  std::string request =
      "POST /rpc HTTP/1.1\r\n"
      "Host: localhost\r\n"
      "Content-Type: application/json\r\n"
      "Content-Length: 27\r\n"
      "\r\n"
      "{\"method\":\"test\",\"id\":1}";

  Buffer request_buffer;
  request_buffer.add(request.c_str(), request.length());
  client_connection_->write(request_buffer, false);

  // Wait for response
  ASSERT_TRUE(waitForCondition([this]() { return server_request_complete_; }));

  // Verify server received correct data
  EXPECT_EQ(server_headers_["host"], "localhost");
  EXPECT_EQ(server_headers_["content-type"], "application/json");
  EXPECT_EQ(server_body_, "{\"method\":\"test\",\"id\":1}");

  // Verify transport socket didn't process protocol
  // (This is the key architectural test)
  auto transport_socket = client_connection_->transportSocket();
  EXPECT_EQ(transport_socket->protocol(), "");  // RawBuffer has no protocol
}

/**
 * Test SSE event streaming through filter chain
 * Verifies SSE codec filter works correctly
 */
TEST_F(HttpSseFilterChainTest, SseEventStream) {
  // Wait for connection
  ASSERT_TRUE(waitForCondition([this]() {
    return client_connection_->state() == network::Connection::State::Open;
  }));

  // Send SSE request
  std::string request =
      "GET /events HTTP/1.1\r\n"
      "Host: localhost\r\n"
      "Accept: text/event-stream\r\n"
      "\r\n";

  Buffer request_buffer;
  request_buffer.add(request.c_str(), request.length());
  client_connection_->write(request_buffer, false);

  // Wait for SSE headers
  std::this_thread::sleep_for(100ms);

  // Send SSE event from client
  std::string event_data =
      "event: test\n"
      "data: {\"message\":\"hello\"}\n"
      "id: 1\n"
      "\n";

  Buffer event_buffer;
  event_buffer.add(event_data.c_str(), event_data.length());
  client_connection_->write(event_buffer, false);

  // Wait for event to be processed
  ASSERT_TRUE(waitForCondition([this]() { return !sse_events_.empty(); }));

  // Verify SSE event was parsed correctly
  ASSERT_EQ(sse_events_.size(), 1);
  EXPECT_EQ(sse_events_[0].event, "test");
  EXPECT_EQ(sse_events_[0].data, "{\"message\":\"hello\"}");
  EXPECT_EQ(sse_events_[0].id.value_or(""), "1");
}

/**
 * Test that transport socket is pure I/O only
 * This is the critical architectural test
 */
TEST_F(HttpSseFilterChainTest, TransportSocketPureIo) {
  // Create a raw buffer transport socket
  auto transport_socket = std::make_unique<network::RawBufferTransportSocket>();

  // Verify it has no protocol knowledge
  EXPECT_EQ(transport_socket->protocol(), "");
  EXPECT_FALSE(transport_socket->implementsSecureTransport());

  // Verify it only does I/O operations
  Buffer test_buffer;
  test_buffer.add("raw data", 8);

  // Transport socket should pass data through unchanged
  // It should NOT parse or modify protocol data
  TransportSocketCallbacksImpl callbacks;
  transport_socket->setTransportSocketCallbacks(callbacks);

  auto result = transport_socket->doWrite(test_buffer, false);
  EXPECT_EQ(result.action_, TransportIoResult::CONTINUE);

  // Verify buffer wasn't parsed or modified
  EXPECT_EQ(test_buffer.toString(), "raw data");
}

}  // namespace
}  // namespace filter
}  // namespace mcp