/**
 * @file test_context_aware_factories.cc
 * @brief Tests for context-aware filter factory functions
 *
 * Tests that all three core filters (HTTP, SSE, JSON-RPC) can be created
 * using FilterCreationContext and registered factory functions.
 */

#include <memory>

#include <gtest/gtest.h>

#include "mcp/event/libevent_dispatcher.h"
#include "mcp/filter/filter_context.h"
#include "mcp/filter/filter_registry.h"
#include "mcp/filter/http_codec_filter.h"
#include "mcp/filter/json_rpc_protocol_filter.h"
#include "mcp/filter/sse_codec_filter.h"
#include "mcp/json/json_bridge.h"
#include "mcp/mcp_connection_manager.h"

// Forward declare factory registration functions
extern void registerHttpCodecFilterFactory();
extern void registerSseCodecFilterFactory();
extern void registerJsonRpcDispatcherFilterFactory();

namespace mcp {
namespace filter {
namespace {

class MockProtocolCallbacks : public McpProtocolCallbacks {
 public:
  // Implement minimal interface for testing
  void onRequest(const jsonrpc::Request& request) override {}
  void onResponse(const jsonrpc::Response& response) override {}
  void onNotification(const jsonrpc::Notification& notification) override {}
  void onError(const Error& error) override {}
};

class ContextAwareFactoriesTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto factory = event::createLibeventDispatcherFactory();
    dispatcher_ = factory->createDispatcher("test");
    callbacks_ = std::make_unique<MockProtocolCallbacks>();

    // Clear registry to start fresh
    FilterRegistry::instance().clearFactories();

    // Register factories (should happen automatically via static initializers)
    registerHttpCodecFilterFactory();
    registerSseCodecFilterFactory();
    registerJsonRpcDispatcherFilterFactory();
  }

  void TearDown() override { FilterRegistry::instance().clearFactories(); }

  std::unique_ptr<event::Dispatcher> dispatcher_;
  std::unique_ptr<MockProtocolCallbacks> callbacks_;
};

TEST_F(ContextAwareFactoriesTest, HttpCodecFilterFactoryRegistered) {
  // Test that HTTP codec filter factory is registered
  EXPECT_TRUE(FilterRegistry::instance().hasContextFactory("http.codec"));

  // Test metadata is available
  const auto* metadata =
      FilterRegistry::instance().getBasicMetadata("http.codec");
  ASSERT_NE(metadata, nullptr);
  EXPECT_EQ(metadata->name, "http.codec");
  EXPECT_EQ(metadata->version, "1.0.0");
  EXPECT_FALSE(metadata->description.empty());
}

TEST_F(ContextAwareFactoriesTest, SseCodecFilterFactoryRegistered) {
  // Test that SSE codec filter factory is registered
  EXPECT_TRUE(FilterRegistry::instance().hasContextFactory("sse.codec"));

  // Test metadata is available
  const auto* metadata =
      FilterRegistry::instance().getBasicMetadata("sse.codec");
  ASSERT_NE(metadata, nullptr);
  EXPECT_EQ(metadata->name, "sse.codec");
  EXPECT_EQ(metadata->version, "1.0.0");
  EXPECT_FALSE(metadata->description.empty());
}

TEST_F(ContextAwareFactoriesTest, JsonRpcDispatcherFilterFactoryRegistered) {
  // Test that JSON-RPC dispatcher filter factory is registered
  EXPECT_TRUE(
      FilterRegistry::instance().hasContextFactory("json_rpc.dispatcher"));

  // Test metadata is available
  const auto* metadata =
      FilterRegistry::instance().getBasicMetadata("json_rpc.dispatcher");
  ASSERT_NE(metadata, nullptr);
  EXPECT_EQ(metadata->name, "json_rpc.dispatcher");
  EXPECT_EQ(metadata->version, "1.0.0");
  EXPECT_FALSE(metadata->description.empty());
}

TEST_F(ContextAwareFactoriesTest, CreateHttpCodecFilterWithContext) {
  // Create filter creation context
  TransportMetadata transport("127.0.0.1", 8080);
  FilterCreationContext context(*dispatcher_, *callbacks_,
                                ConnectionMode::Server, transport);

  // Create filter using context
  json::JsonValue config = json::JsonValue::object();
  auto filter = FilterRegistry::instance().createFilterWithContext(
      "http.codec", context, config);

  ASSERT_NE(filter, nullptr);

  // Verify it's actually an HttpCodecFilter
  auto http_filter = std::dynamic_pointer_cast<HttpCodecFilter>(filter);
  EXPECT_NE(http_filter, nullptr);
}

TEST_F(ContextAwareFactoriesTest, CreateSseCodecFilterWithContext) {
  // Create filter creation context
  TransportMetadata transport("127.0.0.1", 8080);
  FilterCreationContext context(*dispatcher_, *callbacks_,
                                ConnectionMode::Server, transport);

  // Create filter using context
  json::JsonValue config = json::JsonValue::object();
  auto filter = FilterRegistry::instance().createFilterWithContext(
      "sse.codec", context, config);

  ASSERT_NE(filter, nullptr);

  // Verify it's actually an SseCodecFilter
  auto sse_filter = std::dynamic_pointer_cast<SseCodecFilter>(filter);
  EXPECT_NE(sse_filter, nullptr);
}

TEST_F(ContextAwareFactoriesTest, CreateJsonRpcDispatcherFilterWithContext) {
  // Create filter creation context
  TransportMetadata transport("127.0.0.1", 8080);
  FilterCreationContext context(*dispatcher_, *callbacks_,
                                ConnectionMode::Server, transport);

  // Create filter using context
  json::JsonValue config = json::JsonValue::object();
  auto filter = FilterRegistry::instance().createFilterWithContext(
      "json_rpc.dispatcher", context, config);

  ASSERT_NE(filter, nullptr);

  // Verify it's actually a JsonRpcProtocolFilter
  auto json_rpc_filter =
      std::dynamic_pointer_cast<JsonRpcProtocolFilter>(filter);
  EXPECT_NE(json_rpc_filter, nullptr);
}

TEST_F(ContextAwareFactoriesTest, FilterChainValidation) {
  // Test that a basic filter chain validates correctly
  std::vector<std::string> filter_names = {"http.codec", "sse.codec",
                                           "json_rpc.dispatcher"};

  EXPECT_TRUE(
      FilterRegistry::instance().validateBasicFilterChain(filter_names));
}

TEST_F(ContextAwareFactoriesTest, InvalidFilterChainValidation) {
  // Test that an invalid filter chain fails validation
  std::vector<std::string> invalid_filter_names = {
      "http.codec",
      "invalid.filter",  // This filter doesn't exist
      "json_rpc.dispatcher"};

  EXPECT_FALSE(FilterRegistry::instance().validateBasicFilterChain(
      invalid_filter_names));
}

TEST_F(ContextAwareFactoriesTest, EmptyFilterChainValidation) {
  // Test that an empty filter chain fails validation
  std::vector<std::string> empty_filter_names;

  EXPECT_FALSE(
      FilterRegistry::instance().validateBasicFilterChain(empty_filter_names));
}

TEST_F(ContextAwareFactoriesTest, ClientModeFilters) {
  // Test creating filters in client mode
  TransportMetadata transport("127.0.0.1", 8080);
  FilterCreationContext context(*dispatcher_, *callbacks_,
                                ConnectionMode::Client, transport);

  json::JsonValue config = json::JsonValue::object();

  // Create all three filters in client mode
  auto http_filter = FilterRegistry::instance().createFilterWithContext(
      "http.codec", context, config);
  auto sse_filter = FilterRegistry::instance().createFilterWithContext(
      "sse.codec", context, config);
  auto json_rpc_filter = FilterRegistry::instance().createFilterWithContext(
      "json_rpc.dispatcher", context, config);

  EXPECT_NE(http_filter, nullptr);
  EXPECT_NE(sse_filter, nullptr);
  EXPECT_NE(json_rpc_filter, nullptr);
}

}  // namespace
}  // namespace filter
}  // namespace mcp