/**
 * @file test_basic_filter_registry.cc
 * @brief Tests for basic filter registry functionality
 *
 * Tests that core filters (HTTP, SSE, JSON-RPC) are properly registered
 * with basic metadata and can be created using FilterCreationContext.
 */

#include <memory>

#include <gtest/gtest.h>

#include "mcp/event/libevent_dispatcher.h"
#include "mcp/filter/core_filter_factories.h"
#include "mcp/filter/filter_context.h"
#include "mcp/filter/filter_registry.h"
#include "mcp/filter/http_codec_filter.h"
#include "mcp/filter/json_rpc_protocol_filter.h"
#include "mcp/filter/sse_codec_filter.h"
#include "mcp/json/json_bridge.h"
#include "mcp/mcp_connection_manager.h"

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
  void onConnectionEvent(network::ConnectionEvent event) override {}
};

class BasicFilterRegistryTest : public ::testing::Test {
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

TEST_F(BasicFilterRegistryTest, CoreFiltersRegistered) {
  // Test that core filters are automatically registered
  auto& registry = FilterRegistry::instance();

  EXPECT_TRUE(registry.hasContextFactory("http.codec"));
  EXPECT_TRUE(registry.hasContextFactory("sse.codec"));
  EXPECT_TRUE(registry.hasContextFactory("json_rpc.dispatcher"));

  // Test that all three are listed in registered factories
  auto factories = registry.listContextFactories();

  bool has_http = std::find(factories.begin(), factories.end(), "http.codec") !=
                  factories.end();
  bool has_sse = std::find(factories.begin(), factories.end(), "sse.codec") !=
                 factories.end();
  bool has_json_rpc = std::find(factories.begin(), factories.end(),
                                "json_rpc.dispatcher") != factories.end();

  EXPECT_TRUE(has_http);
  EXPECT_TRUE(has_sse);
  EXPECT_TRUE(has_json_rpc);

  // Should have at least the 3 core filters
  EXPECT_GE(factories.size(), 3);
}

TEST_F(BasicFilterRegistryTest, BasicMetadataRetrieval) {
  auto& registry = FilterRegistry::instance();

  // Test HTTP codec metadata
  const auto* http_metadata = registry.getBasicMetadata("http.codec");
  ASSERT_NE(http_metadata, nullptr);
  EXPECT_EQ(http_metadata->name, "http.codec");
  EXPECT_EQ(http_metadata->version, "1.0.0");
  EXPECT_FALSE(http_metadata->description.empty());
  EXPECT_TRUE(http_metadata->default_config.isObject());

  // Test that metadata validation passes
  EXPECT_NO_THROW(http_metadata->validate());

  // Test SSE codec metadata
  const auto* sse_metadata = registry.getBasicMetadata("sse.codec");
  ASSERT_NE(sse_metadata, nullptr);
  EXPECT_EQ(sse_metadata->name, "sse.codec");
  EXPECT_EQ(sse_metadata->version, "1.0.0");
  EXPECT_FALSE(sse_metadata->description.empty());
  EXPECT_TRUE(sse_metadata->default_config.isObject());

  // Test that metadata validation passes
  EXPECT_NO_THROW(sse_metadata->validate());

  // Test JSON-RPC dispatcher metadata
  const auto* json_rpc_metadata =
      registry.getBasicMetadata("json_rpc.dispatcher");
  ASSERT_NE(json_rpc_metadata, nullptr);
  EXPECT_EQ(json_rpc_metadata->name, "json_rpc.dispatcher");
  EXPECT_EQ(json_rpc_metadata->version, "1.0.0");
  EXPECT_FALSE(json_rpc_metadata->description.empty());
  EXPECT_TRUE(json_rpc_metadata->default_config.isObject());

  // Test that metadata validation passes
  EXPECT_NO_THROW(json_rpc_metadata->validate());

  // Test metadata for non-existent filter
  const auto* invalid_metadata = registry.getBasicMetadata("non.existent");
  EXPECT_EQ(invalid_metadata, nullptr);
}

TEST_F(BasicFilterRegistryTest, FilterCreationWithContext) {
  auto& registry = FilterRegistry::instance();

  // Note: This test verifies that filter factories can be called, but doesn't
  // create actual filter instances to avoid complex dispatcher dependencies.
  // The factory registration and metadata validation are the key aspects.

  // Test that factories are registered and can be called
  EXPECT_TRUE(registry.hasContextFactory("http.codec"));
  EXPECT_TRUE(registry.hasContextFactory("sse.codec"));
  EXPECT_TRUE(registry.hasContextFactory("json_rpc.dispatcher"));

  // Test creation with invalid filter name should throw
  TransportMetadata transport("127.0.0.1", 8080);
  FilterCreationContext context(*dispatcher_, *callbacks_,
                                ConnectionMode::Server, transport);
  json::JsonValue config = json::JsonValue::object();

  EXPECT_THROW(
      registry.createFilterWithContext("invalid.filter", context, config),
      std::runtime_error);

  // Note: Actual filter creation is tested in integration tests where the
  // full filter chain environment is properly set up.
}

TEST_F(BasicFilterRegistryTest, BasicFilterChainValidation) {
  auto& registry = FilterRegistry::instance();

  // Test valid filter chain
  std::vector<std::string> valid_chain = {"http.codec", "sse.codec",
                                          "json_rpc.dispatcher"};
  EXPECT_TRUE(registry.validateBasicFilterChain(valid_chain));

  // Test valid partial chain
  std::vector<std::string> valid_partial_chain = {"http.codec", "sse.codec"};
  EXPECT_TRUE(registry.validateBasicFilterChain(valid_partial_chain));

  // Test single filter chain
  std::vector<std::string> single_filter_chain = {"http.codec"};
  EXPECT_TRUE(registry.validateBasicFilterChain(single_filter_chain));

  // Test invalid filter chain with unknown filter
  std::vector<std::string> invalid_chain = {"http.codec", "unknown.filter",
                                            "json_rpc.dispatcher"};
  EXPECT_FALSE(registry.validateBasicFilterChain(invalid_chain));

  // Test empty filter chain
  std::vector<std::string> empty_chain;
  EXPECT_FALSE(registry.validateBasicFilterChain(empty_chain));

  // Test chain with duplicate filters (should still be valid)
  std::vector<std::string> duplicate_chain = {"http.codec", "http.codec",
                                              "sse.codec"};
  EXPECT_TRUE(registry.validateBasicFilterChain(duplicate_chain));
}

TEST_F(BasicFilterRegistryTest, ClientModeFilters) {
  auto& registry = FilterRegistry::instance();

  // Test that filters can be registered for both server and client modes
  // The actual creation is tested in integration tests to avoid dispatcher
  // complexity

  // Test context creation for client mode
  TransportMetadata transport("127.0.0.1", 8080);
  FilterCreationContext context(*dispatcher_, *callbacks_,
                                ConnectionMode::Client, transport);

  // Verify context properties
  EXPECT_FALSE(context.isServer());
  EXPECT_TRUE(context.isClient());
  EXPECT_EQ(context.getModeString(), "client");

  // Test that all core filters are available for client mode too
  EXPECT_TRUE(registry.hasContextFactory("http.codec"));
  EXPECT_TRUE(registry.hasContextFactory("sse.codec"));
  EXPECT_TRUE(registry.hasContextFactory("json_rpc.dispatcher"));
}

TEST_F(BasicFilterRegistryTest, DefaultConfigurationValues) {
  auto& registry = FilterRegistry::instance();

  // Test HTTP codec default config
  const auto* http_metadata = registry.getBasicMetadata("http.codec");
  ASSERT_NE(http_metadata, nullptr);

  const auto& http_config = http_metadata->default_config;
  EXPECT_TRUE(http_config.contains("header_timeout_ms"));
  EXPECT_TRUE(http_config.contains("body_timeout_ms"));
  EXPECT_TRUE(http_config.contains("idle_timeout_ms"));
  EXPECT_TRUE(http_config.contains("enable_keep_alive"));

  // Test SSE codec default config
  const auto* sse_metadata = registry.getBasicMetadata("sse.codec");
  ASSERT_NE(sse_metadata, nullptr);

  const auto& sse_config = sse_metadata->default_config;
  EXPECT_TRUE(sse_config.contains("keep_alive_interval_ms"));
  EXPECT_TRUE(sse_config.contains("event_timeout_ms"));
  EXPECT_TRUE(sse_config.contains("enable_keep_alive"));

  // Test JSON-RPC dispatcher default config
  const auto* json_rpc_metadata =
      registry.getBasicMetadata("json_rpc.dispatcher");
  ASSERT_NE(json_rpc_metadata, nullptr);

  const auto& json_rpc_config = json_rpc_metadata->default_config;
  EXPECT_TRUE(json_rpc_config.contains("use_framing"));
  EXPECT_TRUE(json_rpc_config.contains("max_message_size"));
  EXPECT_TRUE(json_rpc_config.contains("enable_batching"));
}

TEST_F(BasicFilterRegistryTest, TransportMetadataInContext) {
  auto& registry = FilterRegistry::instance();

  // Test different transport configurations
  TransportMetadata tcp_transport("0.0.0.0", 8080, "192.168.1.100", 45678);
  tcp_transport.sni = "example.com";
  tcp_transport.alpn = {"http/1.1"};

  FilterCreationContext tcp_context(*dispatcher_, *callbacks_,
                                    ConnectionMode::Server, tcp_transport);

  // Verify context properties
  EXPECT_TRUE(tcp_context.isServer());
  EXPECT_FALSE(tcp_context.isClient());
  EXPECT_EQ(tcp_context.getModeString(), "server");
  EXPECT_TRUE(tcp_context.transport.isTcp());
  EXPECT_EQ(tcp_context.transport.getTransportType(), "tcp");

  // Verify transport metadata details
  EXPECT_EQ(tcp_context.transport.local_address, "0.0.0.0");
  EXPECT_EQ(tcp_context.transport.local_port, 8080);
  EXPECT_EQ(tcp_context.transport.remote_address, "192.168.1.100");
  EXPECT_EQ(tcp_context.transport.remote_port, 45678);
  EXPECT_EQ(tcp_context.transport.sni, "example.com");
  EXPECT_EQ(tcp_context.transport.alpn, std::vector<std::string>{"http/1.1"});
}

TEST_F(BasicFilterRegistryTest, MetadataEquality) {
  auto& registry = FilterRegistry::instance();

  // Get metadata for HTTP codec
  const auto* metadata1 = registry.getBasicMetadata("http.codec");
  const auto* metadata2 = registry.getBasicMetadata("http.codec");

  ASSERT_NE(metadata1, nullptr);
  ASSERT_NE(metadata2, nullptr);

  // Should be the same object
  EXPECT_EQ(metadata1, metadata2);

  // Test metadata inequality with different filter
  BasicFilterMetadata different_metadata("different.filter",
                                         "Different filter");
  EXPECT_NE(different_metadata, *metadata1);

  // Test that metadata contains expected fields (instead of exact equality)
  EXPECT_EQ(metadata1->name, "http.codec");
  EXPECT_EQ(metadata1->version, "1.0.0");
  EXPECT_FALSE(metadata1->description.empty());
  EXPECT_TRUE(metadata1->default_config.isObject());

  // Test that default_config has expected fields
  const auto& config = metadata1->default_config;
  EXPECT_TRUE(config.contains("header_timeout_ms"));
  EXPECT_TRUE(config.contains("body_timeout_ms"));
  EXPECT_TRUE(config.contains("idle_timeout_ms"));
  EXPECT_TRUE(config.contains("enable_keep_alive"));
}

}  // namespace
}  // namespace filter
}  // namespace mcp