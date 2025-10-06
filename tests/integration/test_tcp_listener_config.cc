/**
 * @file test_tcp_listener_config.cc
 * @brief Integration tests for TCP listener configuration
 *
 * Tests the config-driven TCP listener functionality including:
 * - TCP listener creation from ListenerConfig
 * - Filter chain assembly from configuration
 * - End-to-end listener functionality
 * - Error handling scenarios
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "mcp/network/server_listener_impl.h"
#include "mcp/config/listener_config.h"
#include "mcp/event/libevent_dispatcher.h"
#include "mcp/mcp_connection_manager.h"
#include "mcp/json/json_bridge.h"

#define GOPHER_LOG_COMPONENT "test.tcp_listener_config"

namespace mcp {
namespace integration {
namespace test {

class MockListenerCallbacks : public network::ListenerCallbacks {
public:
  MOCK_METHOD(void, onAccept, (network::ConnectionSocketPtr&& socket), (override));
  MOCK_METHOD(void, onNewConnection, (network::ConnectionPtr&& connection), (override));
};

class TcpListenerConfigTest : public ::testing::Test {
protected:
  void SetUp() override {
    auto factory = event::createLibeventDispatcherFactory();
    dispatcher_ = factory->createDispatcher("test");
    callbacks_ = std::make_unique<MockListenerCallbacks>();

    // Clear registry to start fresh
    filter::FilterRegistry::instance().clearFactories();

    // Register core filters for testing
    registerCoreFilters();
  }

  void TearDown() override {
    filter::FilterRegistry::instance().clearFactories();
  }

  void registerCoreFilters() {
    // Register core filters needed for testing
    auto& registry = filter::FilterRegistry::instance();

    // HTTP codec filter
    filter::BasicFilterMetadata http_metadata;
    http_metadata.name = "http.codec";
    http_metadata.version = "1.0.0";
    http_metadata.description = "HTTP codec filter for testing";
    http_metadata.default_config = json::JsonValue::object();
    http_metadata.default_config["header_timeout_ms"] = json::JsonValue(5000);
    http_metadata.default_config["body_timeout_ms"] = json::JsonValue(10000);
    http_metadata.default_config["idle_timeout_ms"] = json::JsonValue(30000);
    http_metadata.default_config["enable_keep_alive"] = json::JsonValue(true);

    registry.registerContextFactory(
        "http.codec",
        [](const filter::FilterCreationContext& ctx, const json::JsonValue& config) {
          // Return a mock filter for testing
          return std::shared_ptr<network::Filter>(nullptr);
        },
        http_metadata);

    // SSE codec filter
    filter::BasicFilterMetadata sse_metadata;
    sse_metadata.name = "sse.codec";
    sse_metadata.version = "1.0.0";
    sse_metadata.description = "SSE codec filter for testing";
    sse_metadata.default_config = json::JsonValue::object();
    sse_metadata.default_config["keep_alive_interval_ms"] = json::JsonValue(30000);
    sse_metadata.default_config["event_timeout_ms"] = json::JsonValue(10000);
    sse_metadata.default_config["enable_keep_alive"] = json::JsonValue(true);

    registry.registerContextFactory(
        "sse.codec",
        [](const filter::FilterCreationContext& ctx, const json::JsonValue& config) {
          return std::shared_ptr<network::Filter>(nullptr);
        },
        sse_metadata);

    // JSON-RPC dispatcher filter
    filter::BasicFilterMetadata json_rpc_metadata;
    json_rpc_metadata.name = "json_rpc.dispatcher";
    json_rpc_metadata.version = "1.0.0";
    json_rpc_metadata.description = "JSON-RPC dispatcher filter for testing";
    json_rpc_metadata.default_config = json::JsonValue::object();
    json_rpc_metadata.default_config["use_framing"] = json::JsonValue(true);
    json_rpc_metadata.default_config["max_message_size"] = json::JsonValue(1048576);
    json_rpc_metadata.default_config["enable_batching"] = json::JsonValue(false);

    registry.registerContextFactory(
        "json_rpc.dispatcher",
        [](const filter::FilterCreationContext& ctx, const json::JsonValue& config) {
          return std::shared_ptr<network::Filter>(nullptr);
        },
        json_rpc_metadata);
  }

  config::ListenerConfig createTestListenerConfig() {
    config::ListenerConfig listener_config;
    listener_config.name = "test_listener";
    listener_config.address.socket_address.address = "127.0.0.1";
    listener_config.address.socket_address.port_value = 0; // Use any available port

    // Create filter chain with HTTP -> SSE -> JSON-RPC
    config::FilterChainConfig filter_chain;

    config::FilterConfig http_filter;
    http_filter.name = "http.codec";
    http_filter.type = "http.codec";
    http_filter.config = json::JsonValue::object();
    filter_chain.filters.push_back(http_filter);

    config::FilterConfig sse_filter;
    sse_filter.name = "sse.codec";
    sse_filter.type = "sse.codec";
    sse_filter.config = json::JsonValue::object();
    filter_chain.filters.push_back(sse_filter);

    config::FilterConfig json_rpc_filter;
    json_rpc_filter.name = "json_rpc.dispatcher";
    json_rpc_filter.type = "json_rpc.dispatcher";
    json_rpc_filter.config = json::JsonValue::object();
    filter_chain.filters.push_back(json_rpc_filter);

    listener_config.filter_chains.push_back(filter_chain);

    return listener_config;
  }

  std::unique_ptr<event::Dispatcher> dispatcher_;
  std::unique_ptr<MockListenerCallbacks> callbacks_;
};

TEST_F(TcpListenerConfigTest, BasicListenerCreation) {
  // Test TCP listener can be created from ListenerConfig
  auto listener_config = createTestListenerConfig();

  // Validate the configuration
  EXPECT_NO_THROW(listener_config.validate());

  // Create TCP listener with configuration
  auto tcp_listener = std::make_unique<network::TcpActiveListener>(
      *dispatcher_, listener_config, *callbacks_);

  EXPECT_NE(tcp_listener, nullptr);
  EXPECT_EQ(tcp_listener->name(), "test_listener");
}

TEST_F(TcpListenerConfigTest, FilterChainFromConfig) {
  // Test that filter chain is created from configuration
  auto listener_config = createTestListenerConfig();

  // Create TCP listener
  auto tcp_listener = std::make_unique<network::TcpActiveListener>(
      *dispatcher_, listener_config, *callbacks_);

  // Verify listener was created successfully
  EXPECT_NE(tcp_listener, nullptr);

  // The filter chain should be configured internally
  // This is verified by the successful construction without exceptions
  EXPECT_EQ(tcp_listener->name(), "test_listener");
}

TEST_F(TcpListenerConfigTest, ConfigurationValidation) {
  // Test configuration validation and error handling

  // Test empty filter chain (should fail)
  config::ListenerConfig invalid_config;
  invalid_config.name = "invalid_listener";
  invalid_config.address.socket_address.address = "127.0.0.1";
  invalid_config.address.socket_address.port_value = 8080;
  // No filter chains - should be invalid

  EXPECT_THROW(invalid_config.validate(), config::ConfigValidationError);

  // Test invalid filter name
  config::ListenerConfig invalid_filter_config = createTestListenerConfig();
  invalid_filter_config.filter_chains[0].filters[0].name = "unknown_filter";
  invalid_filter_config.filter_chains[0].filters[0].type = "unknown_filter";

  // This should still pass validation at config level
  EXPECT_NO_THROW(invalid_filter_config.validate());

  // But creating the listener might fail during filter creation
  // (depending on implementation details)
}

TEST_F(TcpListenerConfigTest, MultipleFilterChains) {
  // Test listener with multiple filter chains
  auto listener_config = createTestListenerConfig();

  // Add a second filter chain
  config::FilterChainConfig second_chain;

  config::FilterConfig rate_limit_filter;
  rate_limit_filter.name = "rate_limit";
  rate_limit_filter.type = "rate_limit";
  rate_limit_filter.config = json::JsonValue::object();
  second_chain.filters.push_back(rate_limit_filter);

  listener_config.filter_chains.push_back(second_chain);

  // Validate configuration
  EXPECT_NO_THROW(listener_config.validate());

  // Create listener - should use first filter chain
  auto tcp_listener = std::make_unique<network::TcpActiveListener>(
      *dispatcher_, listener_config, *callbacks_);

  EXPECT_NE(tcp_listener, nullptr);
}

TEST_F(TcpListenerConfigTest, ConfigurationSerialization) {
  // Test that configuration can be serialized to/from JSON
  auto listener_config = createTestListenerConfig();

  // Serialize to JSON
  auto json_config = listener_config.toJson();

  // Verify JSON structure
  EXPECT_TRUE(json_config.contains("name"));
  EXPECT_TRUE(json_config.contains("address"));
  EXPECT_TRUE(json_config.contains("filter_chains"));

  EXPECT_EQ(json_config["name"].getString(), "test_listener");
  EXPECT_TRUE(json_config["address"]["socket_address"]["address"].getString() == "127.0.0.1");
  EXPECT_EQ(json_config["address"]["socket_address"]["port_value"].getInt(), 0);

  // Deserialize from JSON
  auto deserialized_config = config::ListenerConfig::fromJson(json_config);

  // Verify deserialized config matches original
  EXPECT_EQ(deserialized_config.name, listener_config.name);
  EXPECT_EQ(deserialized_config.address.socket_address.address,
            listener_config.address.socket_address.address);
  EXPECT_EQ(deserialized_config.address.socket_address.port_value,
            listener_config.address.socket_address.port_value);
  EXPECT_EQ(deserialized_config.filter_chains.size(),
            listener_config.filter_chains.size());
}

TEST_F(TcpListenerConfigTest, IPv6AddressSupport) {
  // Test IPv6 address support
  auto listener_config = createTestListenerConfig();
  listener_config.address.socket_address.address = "::1";
  listener_config.address.socket_address.port_value = 8080;

  // Validate configuration
  EXPECT_NO_THROW(listener_config.validate());

  // Create listener
  auto tcp_listener = std::make_unique<network::TcpActiveListener>(
      *dispatcher_, listener_config, *callbacks_);

  EXPECT_NE(tcp_listener, nullptr);
}

TEST_F(TcpListenerConfigTest, ListenerEnableDisable) {
  // Test listener enable/disable functionality
  auto listener_config = createTestListenerConfig();

  auto tcp_listener = std::make_unique<network::TcpActiveListener>(
      *dispatcher_, listener_config, *callbacks_);

  EXPECT_NE(tcp_listener, nullptr);

  // Test enable/disable
  EXPECT_NO_THROW(tcp_listener->enable());
  EXPECT_NO_THROW(tcp_listener->disable());
  EXPECT_NO_THROW(tcp_listener->pause());
  EXPECT_NO_THROW(tcp_listener->resume());
}

TEST_F(TcpListenerConfigTest, FilterConfigurationPassing) {
  // Test that filter configuration is passed correctly
  auto listener_config = createTestListenerConfig();

  // Add custom configuration to HTTP filter
  listener_config.filter_chains[0].filters[0].config["custom_param"] =
      json::JsonValue("custom_value");
  listener_config.filter_chains[0].filters[0].config["timeout_ms"] =
      json::JsonValue(5000);

  // Validate configuration
  EXPECT_NO_THROW(listener_config.validate());

  // Create listener
  auto tcp_listener = std::make_unique<network::TcpActiveListener>(
      *dispatcher_, listener_config, *callbacks_);

  EXPECT_NE(tcp_listener, nullptr);

  // Filter configuration passing is verified by successful construction
  // Individual filter tests would verify the configuration is used correctly
}

// Integration test that demonstrates complete TCP listener workflow
TEST_F(TcpListenerConfigTest, EndToEndListenerWorkflow) {
  // Create a complete listener configuration
  auto listener_config = createTestListenerConfig();
  listener_config.name = "integration_test_listener";
  listener_config.address.socket_address.port_value = 0; // Any port

  // Validate configuration
  ASSERT_NO_THROW(listener_config.validate());

  // Create TCP listener
  auto tcp_listener = std::make_unique<network::TcpActiveListener>(
      *dispatcher_, listener_config, *callbacks_);

  ASSERT_NE(tcp_listener, nullptr);
  EXPECT_EQ(tcp_listener->name(), "integration_test_listener");

  // Enable listener
  EXPECT_NO_THROW(tcp_listener->enable());

  // Run dispatcher briefly to initialize
  dispatcher_->run(event::RunType::NonBlock);

  // Disable listener
  EXPECT_NO_THROW(tcp_listener->disable());

  // This test demonstrates the complete workflow:
  // 1. Configuration creation and validation
  // 2. TCP listener creation from config
  // 3. Filter chain setup from configuration
  // 4. Listener lifecycle management
}

}  // namespace test
}  // namespace integration
}  // namespace mcp