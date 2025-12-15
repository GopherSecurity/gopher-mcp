/**
 * @file test_filter_integration.cc
 * @brief Integration tests for HTTP->SSE->JSON-RPC filter chain
 *
 * Tests that the complete filter chain (HTTP codec, SSE codec, JSON-RPC
 * dispatcher) works correctly when chained together using the new context-aware
 * factories.
 */

#include <memory>
#include <string>

#include <gtest/gtest.h>

#include "mcp/buffer.h"
#include "mcp/event/libevent_dispatcher.h"
#include "mcp/filter/filter_context.h"
#include "mcp/filter/filter_registry.h"
#include "mcp/filter/http_codec_filter.h"
#include "mcp/filter/json_rpc_protocol_filter.h"
#include "mcp/filter/sse_codec_filter.h"
#include "mcp/json/json_bridge.h"
#include "mcp/mcp_connection_manager.h"
#include "mcp/network/filter.h"

// Forward declare factory registration functions
extern void registerHttpCodecFilterFactory();
extern void registerSseCodecFilterFactory();
extern void registerJsonRpcDispatcherFilterFactory();

namespace mcp {
namespace filter {
namespace {

class MockProtocolCallbacks : public McpProtocolCallbacks {
 public:
  void onRequest(const jsonrpc::Request& request) override {
    requests_received_.push_back(request);
  }

  void onResponse(const jsonrpc::Response& response) override {
    responses_received_.push_back(response);
  }

  void onNotification(const jsonrpc::Notification& notification) override {
    notifications_received_.push_back(notification);
  }

  void onError(const Error& error) override {
    errors_received_.push_back(error);
  }

  // Test access methods
  const std::vector<jsonrpc::Request>& getRequests() const {
    return requests_received_;
  }
  const std::vector<jsonrpc::Response>& getResponses() const {
    return responses_received_;
  }
  const std::vector<jsonrpc::Notification>& getNotifications() const {
    return notifications_received_;
  }
  const std::vector<Error>& getErrors() const { return errors_received_; }

  void clearReceived() {
    requests_received_.clear();
    responses_received_.clear();
    notifications_received_.clear();
    errors_received_.clear();
  }

 private:
  std::vector<jsonrpc::Request> requests_received_;
  std::vector<jsonrpc::Response> responses_received_;
  std::vector<jsonrpc::Notification> notifications_received_;
  std::vector<Error> errors_received_;
};

class MockFilterCallbacks : public network::ReadFilterCallbacks,
                            public network::WriteFilterCallbacks {
 public:
  network::Connection& connection() override {
    // Return a reference - this is a simple mock
    static network::Connection* mock_conn = nullptr;
    return *mock_conn;  // This will crash if used, but it's for interface
                        // compatibility
  }

  void continueReading() override {}
  void upstreamHost(
      network::UpstreamHostDescriptionConstSharedPtr host) override {}
  const std::string& upstreamHost() const override {
    static std::string empty_host;
    return empty_host;
  }
  void setUpstreamHost(const std::string& host) override {}
  bool shouldContinueFilterChain() override { return true; }
  void injectWriteDataToFilterChain(Buffer& data, bool end_stream) override {}
  bool aboveWriteBufferHighWatermark() const override { return false; }
};

class FilterIntegrationTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto factory = event::createLibeventDispatcherFactory();
    dispatcher_ = factory->createDispatcher("test");
    callbacks_ = std::make_unique<MockProtocolCallbacks>();
    filter_callbacks_ = std::make_unique<MockFilterCallbacks>();

    // Clear registry and register factories
    FilterRegistry::instance().clearFactories();
    registerHttpCodecFilterFactory();
    registerSseCodecFilterFactory();
    registerJsonRpcDispatcherFilterFactory();
  }

  void TearDown() override { FilterRegistry::instance().clearFactories(); }

  // Helper to create filter chain
  void createFilterChain() {
    TransportMetadata transport("127.0.0.1", 8080);
    FilterCreationContext context(*dispatcher_, *callbacks_,
                                  ConnectionMode::Server, transport);

    json::JsonValue config = json::JsonValue::object();

    // Create filters in order: HTTP -> SSE -> JSON-RPC
    http_filter_ = std::dynamic_pointer_cast<HttpCodecFilter>(
        FilterRegistry::instance().createFilterWithContext("http.codec",
                                                           context, config));

    sse_filter_ = std::dynamic_pointer_cast<SseCodecFilter>(
        FilterRegistry::instance().createFilterWithContext("sse.codec", context,
                                                           config));

    json_rpc_filter_ = std::dynamic_pointer_cast<JsonRpcProtocolFilter>(
        FilterRegistry::instance().createFilterWithContext(
            "json_rpc.dispatcher", context, config));

    ASSERT_NE(http_filter_, nullptr);
    ASSERT_NE(sse_filter_, nullptr);
    ASSERT_NE(json_rpc_filter_, nullptr);

    // Initialize filter callbacks
    http_filter_->initializeReadFilterCallbacks(*filter_callbacks_);
    sse_filter_->initializeReadFilterCallbacks(*filter_callbacks_);
    json_rpc_filter_->initializeReadFilterCallbacks(*filter_callbacks_);
  }

  std::unique_ptr<event::Dispatcher> dispatcher_;
  std::unique_ptr<MockProtocolCallbacks> callbacks_;
  std::unique_ptr<MockFilterCallbacks> filter_callbacks_;

  std::shared_ptr<HttpCodecFilter> http_filter_;
  std::shared_ptr<SseCodecFilter> sse_filter_;
  std::shared_ptr<JsonRpcProtocolFilter> json_rpc_filter_;
};

TEST_F(FilterIntegrationTest, CreateCompleteFilterChain) {
  createFilterChain();

  // Verify all filters were created successfully
  EXPECT_NE(http_filter_, nullptr);
  EXPECT_NE(sse_filter_, nullptr);
  EXPECT_NE(json_rpc_filter_, nullptr);
}

TEST_F(FilterIntegrationTest, FilterChainInitialization) {
  createFilterChain();

  // Test that all filters can handle onNewConnection
  EXPECT_EQ(http_filter_->onNewConnection(), network::FilterStatus::Continue);
  EXPECT_EQ(sse_filter_->onNewConnection(), network::FilterStatus::Continue);
  EXPECT_EQ(json_rpc_filter_->onNewConnection(),
            network::FilterStatus::Continue);
}

TEST_F(FilterIntegrationTest, BasicHttpDataFlow) {
  createFilterChain();

  // Create simple HTTP request data
  std::string http_request =
      "POST /mcp HTTP/1.1\r\n"
      "Host: localhost:8080\r\n"
      "Content-Type: application/json\r\n"
      "Content-Length: 27\r\n"
      "\r\n"
      "{\"method\":\"test\",\"id\":1}";

  auto buffer = std::make_unique<OwnedBuffer>();
  buffer->add(http_request);

  // Process through HTTP filter
  auto status = http_filter_->onData(*buffer, false);

  // For now, just verify it doesn't crash and returns a valid status
  EXPECT_TRUE(status == network::FilterStatus::Continue ||
              status == network::FilterStatus::StopIteration);
}

TEST_F(FilterIntegrationTest, FilterRegistryListsAllFactories) {
  createFilterChain();

  auto factories = FilterRegistry::instance().listContextFactories();

  // Should contain all three factories
  EXPECT_GE(factories.size(), 3);

  bool has_http = std::find(factories.begin(), factories.end(), "http.codec") !=
                  factories.end();
  bool has_sse = std::find(factories.begin(), factories.end(), "sse.codec") !=
                 factories.end();
  bool has_json_rpc = std::find(factories.begin(), factories.end(),
                                "json_rpc.dispatcher") != factories.end();

  EXPECT_TRUE(has_http);
  EXPECT_TRUE(has_sse);
  EXPECT_TRUE(has_json_rpc);
}

TEST_F(FilterIntegrationTest, DefaultConfigurationIsValid) {
  createFilterChain();

  // Verify that default configurations are valid JSON objects
  const auto* http_metadata =
      FilterRegistry::instance().getBasicMetadata("http.codec");
  const auto* sse_metadata =
      FilterRegistry::instance().getBasicMetadata("sse.codec");
  const auto* json_rpc_metadata =
      FilterRegistry::instance().getBasicMetadata("json_rpc.dispatcher");

  ASSERT_NE(http_metadata, nullptr);
  ASSERT_NE(sse_metadata, nullptr);
  ASSERT_NE(json_rpc_metadata, nullptr);

  EXPECT_TRUE(http_metadata->default_config.isObject());
  EXPECT_TRUE(sse_metadata->default_config.isObject());
  EXPECT_TRUE(json_rpc_metadata->default_config.isObject());
}

TEST_F(FilterIntegrationTest, MetadataValidation) {
  createFilterChain();

  // Test metadata validation
  const auto* http_metadata =
      FilterRegistry::instance().getBasicMetadata("http.codec");
  ASSERT_NE(http_metadata, nullptr);

  // This should not throw
  EXPECT_NO_THROW(http_metadata->validate());

  // Test that metadata contains expected fields
  EXPECT_FALSE(http_metadata->name.empty());
  EXPECT_FALSE(http_metadata->version.empty());
  EXPECT_FALSE(http_metadata->description.empty());
}

// NOTE: More comprehensive integration tests would require:
// 1. Mock network connections
// 2. Complete HTTP message parsing simulation
// 3. SSE event stream simulation
// 4. JSON-RPC message flow testing
//
// These are deferred to future iterations when the filter communication
// interface is fully refactored to use standard network filter patterns.

}  // namespace
}  // namespace filter
}  // namespace mcp