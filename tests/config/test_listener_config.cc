/**
 * @file test_listener_config.cc
 * @brief Unit tests for listener configuration schema
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "mcp/config/listener_config.h"
#include "mcp/filter/filter_registry.h"
#include "mcp/filter/filter_context.h"
#include "mcp/event/libevent_dispatcher.h"
#include "mcp/mcp_connection_manager.h"
#include "mcp/json/json_bridge.h"

using namespace mcp::config;
using namespace mcp::filter;
using mcp::json::JsonValue;
using ::testing::Contains;
using ::testing::HasSubstr;

class ListenerConfigTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Setup code if needed
  }

  void TearDown() override {
    // Cleanup code if needed
  }

  // Helper to create a valid JSON configuration
  JsonValue createValidConfig() {
    const std::string valid_json = R"({
      "listeners": [
        {
          "name": "default_listener",
          "address": {
            "socket_address": {
              "address": "0.0.0.0",
              "port_value": 8080
            }
          },
          "filter_chains": [
            {
              "filters": [
                { "name": "http.codec" },
                { "name": "sse.codec" },
                { "name": "json_rpc.dispatcher" }
              ]
            }
          ]
        }
      ]
    })";
    return JsonValue::parse(valid_json);
  }

  // Helper to create JSON with no listeners
  JsonValue createNoListenersConfig() {
    const std::string json = R"({
      "listeners": []
    })";
    return JsonValue::parse(json);
  }

  // Helper to create JSON with invalid address
  JsonValue createInvalidAddressConfig() {
    const std::string json = R"({
      "listeners": [
        {
          "name": "invalid_listener",
          "address": {
            "socket_address": {
              "address": "invalid@address",
              "port_value": 8080
            }
          },
          "filter_chains": [
            {
              "filters": [
                { "name": "http.codec" }
              ]
            }
          ]
        }
      ]
    })";
    return JsonValue::parse(json);
  }
};

// ============================================================================
// SocketAddress Tests
// ============================================================================

TEST_F(ListenerConfigTest, SocketAddressDefaults) {
  SocketAddress addr;

  EXPECT_EQ(addr.address, "127.0.0.1");
  EXPECT_EQ(addr.port_value, 8080);
}

TEST_F(ListenerConfigTest, SocketAddressValidation_ValidConfig) {
  SocketAddress addr;
  addr.address = "0.0.0.0";
  addr.port_value = 9000;

  EXPECT_NO_THROW(addr.validate());
}

TEST_F(ListenerConfigTest, SocketAddressValidation_EmptyAddress) {
  SocketAddress addr;
  addr.address = "";

  EXPECT_THROW({
    try {
      addr.validate();
    } catch (const ConfigValidationError& e) {
      EXPECT_THAT(e.what(), HasSubstr("IP address cannot be empty"));
      throw;
    }
  }, ConfigValidationError);
}

TEST_F(ListenerConfigTest, SocketAddressValidation_ZeroPort) {
  SocketAddress addr;
  addr.port_value = 0;

  EXPECT_THROW({
    try {
      addr.validate();
    } catch (const ConfigValidationError& e) {
      EXPECT_THAT(e.what(), HasSubstr("Port cannot be 0"));
      throw;
    }
  }, ConfigValidationError);
}

TEST_F(ListenerConfigTest, SocketAddressValidation_InvalidAddress) {
  SocketAddress addr;
  addr.address = "invalid@address";

  EXPECT_THROW({
    try {
      addr.validate();
    } catch (const ConfigValidationError& e) {
      EXPECT_THAT(e.what(), HasSubstr("Invalid IP address format"));
      throw;
    }
  }, ConfigValidationError);
}

TEST_F(ListenerConfigTest, SocketAddressJsonRoundtrip) {
  SocketAddress addr;
  addr.address = "192.168.1.1";
  addr.port_value = 3000;

  // Convert to JSON and back
  auto json = addr.toJson();
  auto restored = SocketAddress::fromJson(json);

  EXPECT_EQ(addr, restored);
}

// ============================================================================
// FilterConfig Tests
// ============================================================================

TEST_F(ListenerConfigTest, FilterConfigDefaults) {
  FilterConfig filter;

  EXPECT_TRUE(filter.name.empty());
  EXPECT_TRUE(filter.config.isObject());
}

TEST_F(ListenerConfigTest, FilterConfigConstructors) {
  FilterConfig filter1("test.filter");
  EXPECT_EQ(filter1.name, "test.filter");

  JsonValue config = JsonValue::object();
  config["param"] = "value";
  FilterConfig filter2("test.filter", config);
  EXPECT_EQ(filter2.name, "test.filter");
  EXPECT_EQ(filter2.config["param"].getString(), "value");
}

TEST_F(ListenerConfigTest, FilterConfigValidation_ValidFilter) {
  FilterConfig filter("http.codec");

  EXPECT_NO_THROW(filter.validate());
}

TEST_F(ListenerConfigTest, FilterConfigValidation_EmptyName) {
  FilterConfig filter;
  filter.name = "";

  EXPECT_THROW({
    try {
      filter.validate();
    } catch (const ConfigValidationError& e) {
      EXPECT_THAT(e.what(), HasSubstr("Filter name cannot be empty"));
      throw;
    }
  }, ConfigValidationError);
}

TEST_F(ListenerConfigTest, FilterConfigValidation_InvalidName) {
  FilterConfig filter;
  filter.name = "invalid-name@with!special&chars";

  EXPECT_THROW({
    try {
      filter.validate();
    } catch (const ConfigValidationError& e) {
      EXPECT_THAT(e.what(), HasSubstr("Filter name contains invalid characters"));
      throw;
    }
  }, ConfigValidationError);
}

TEST_F(ListenerConfigTest, FilterConfigJsonRoundtrip) {
  FilterConfig filter("test.filter");
  filter.config["key"] = "value";

  // Convert to JSON and back
  auto json = filter.toJson();
  auto restored = FilterConfig::fromJson(json);

  EXPECT_EQ(filter, restored);
}

// ============================================================================
// FilterChainConfig Tests
// ============================================================================

TEST_F(ListenerConfigTest, FilterChainConfigValidation_ValidChain) {
  FilterChainConfig chain;
  chain.filters.push_back(FilterConfig("http.codec"));
  chain.filters.push_back(FilterConfig("sse.codec"));
  chain.filters.push_back(FilterConfig("json_rpc.dispatcher"));

  EXPECT_NO_THROW(chain.validate());
}

TEST_F(ListenerConfigTest, FilterChainConfigValidation_EmptyChain) {
  FilterChainConfig chain;

  EXPECT_THROW({
    try {
      chain.validate();
    } catch (const ConfigValidationError& e) {
      EXPECT_THAT(e.what(), HasSubstr("Filter chain cannot be empty"));
      throw;
    }
  }, ConfigValidationError);
}

TEST_F(ListenerConfigTest, FilterChainConfigValidation_DuplicateFilters) {
  FilterChainConfig chain;
  chain.filters.push_back(FilterConfig("http.codec"));
  chain.filters.push_back(FilterConfig("http.codec"));  // Duplicate

  EXPECT_THROW({
    try {
      chain.validate();
    } catch (const ConfigValidationError& e) {
      EXPECT_THAT(e.what(), HasSubstr("Duplicate filter name: http.codec"));
      throw;
    }
  }, ConfigValidationError);
}

TEST_F(ListenerConfigTest, FilterChainConfigJsonRoundtrip) {
  FilterChainConfig chain;
  chain.filters.push_back(FilterConfig("filter1"));
  chain.filters.push_back(FilterConfig("filter2"));

  // Convert to JSON and back
  auto json = chain.toJson();
  auto restored = FilterChainConfig::fromJson(json);

  EXPECT_EQ(chain, restored);
}

// ============================================================================
// ListenerConfig Tests
// ============================================================================

TEST_F(ListenerConfigTest, ListenerConfigDefaults) {
  ListenerConfig listener;

  EXPECT_EQ(listener.name, "default_listener");
  EXPECT_TRUE(listener.filter_chains.empty());
}

TEST_F(ListenerConfigTest, ListenerConfigValidation_ValidListener) {
  ListenerConfig listener;
  listener.name = "test_listener";
  listener.address.socket_address.address = "127.0.0.1";
  listener.address.socket_address.port_value = 8080;

  FilterChainConfig chain;
  chain.filters.push_back(FilterConfig("http.codec"));
  listener.filter_chains.push_back(chain);

  EXPECT_NO_THROW(listener.validate());
}

TEST_F(ListenerConfigTest, ListenerConfigValidation_EmptyName) {
  ListenerConfig listener;
  listener.name = "";

  FilterChainConfig chain;
  chain.filters.push_back(FilterConfig("http.codec"));
  listener.filter_chains.push_back(chain);

  EXPECT_THROW({
    try {
      listener.validate();
    } catch (const ConfigValidationError& e) {
      EXPECT_THAT(e.what(), HasSubstr("Listener name cannot be empty"));
      throw;
    }
  }, ConfigValidationError);
}

TEST_F(ListenerConfigTest, ListenerConfigValidation_InvalidName) {
  ListenerConfig listener;
  listener.name = "invalid@name";

  FilterChainConfig chain;
  chain.filters.push_back(FilterConfig("http.codec"));
  listener.filter_chains.push_back(chain);

  EXPECT_THROW({
    try {
      listener.validate();
    } catch (const ConfigValidationError& e) {
      EXPECT_THAT(e.what(), HasSubstr("Listener name contains invalid characters"));
      throw;
    }
  }, ConfigValidationError);
}

TEST_F(ListenerConfigTest, ListenerConfigValidation_NoFilterChains) {
  ListenerConfig listener;
  listener.name = "test_listener";

  EXPECT_THROW({
    try {
      listener.validate();
    } catch (const ConfigValidationError& e) {
      EXPECT_THAT(e.what(), HasSubstr("Listener must have at least one filter chain"));
      throw;
    }
  }, ConfigValidationError);
}

TEST_F(ListenerConfigTest, ListenerConfigJsonRoundtrip) {
  ListenerConfig listener;
  listener.name = "test_listener";
  listener.address.socket_address.address = "0.0.0.0";
  listener.address.socket_address.port_value = 9000;

  FilterChainConfig chain;
  chain.filters.push_back(FilterConfig("filter1"));
  listener.filter_chains.push_back(chain);

  // Convert to JSON and back
  auto json = listener.toJson();
  auto restored = ListenerConfig::fromJson(json);

  EXPECT_EQ(listener, restored);
}

// ============================================================================
// ServerConfig Tests
// ============================================================================

TEST_F(ListenerConfigTest, ServerConfigValidation_ValidConfig) {
  auto json = createValidConfig();
  auto config = ServerConfig::fromJson(json);

  EXPECT_TRUE(config.validate());
}

TEST_F(ListenerConfigTest, ServerConfigValidation_EmptyListeners) {
  auto json = createNoListenersConfig();
  auto config = ServerConfig::fromJson(json);

  EXPECT_FALSE(config.validate());

  auto errors = config.getValidationErrors();
  EXPECT_FALSE(errors.empty());
  EXPECT_THAT(errors[0], HasSubstr("Server must have at least one listener"));
}

TEST_F(ListenerConfigTest, ServerConfigValidation_InvalidAddress) {
  auto json = createInvalidAddressConfig();
  auto config = ServerConfig::fromJson(json);

  EXPECT_FALSE(config.validate());
}

TEST_F(ListenerConfigTest, ServerConfigValidation_DuplicateListenerNames) {
  const std::string json_str = R"({
    "listeners": [
      {
        "name": "duplicate_name",
        "address": {
          "socket_address": {
            "address": "127.0.0.1",
            "port_value": 8080
          }
        },
        "filter_chains": [
          {
            "filters": [
              { "name": "http.codec" }
            ]
          }
        ]
      },
      {
        "name": "duplicate_name",
        "address": {
          "socket_address": {
            "address": "127.0.0.1",
            "port_value": 8081
          }
        },
        "filter_chains": [
          {
            "filters": [
              { "name": "http.codec" }
            ]
          }
        ]
      }
    ]
  })";

  auto json = JsonValue::parse(json_str);
  auto config = ServerConfig::fromJson(json);

  EXPECT_FALSE(config.validate());

  auto errors = config.getValidationErrors();
  EXPECT_FALSE(errors.empty());
  EXPECT_THAT(errors[0], HasSubstr("Duplicate listener name: duplicate_name"));
}

TEST_F(ListenerConfigTest, ServerConfigValidation_PortConflict) {
  const std::string json_str = R"({
    "listeners": [
      {
        "name": "listener1",
        "address": {
          "socket_address": {
            "address": "127.0.0.1",
            "port_value": 8080
          }
        },
        "filter_chains": [
          {
            "filters": [
              { "name": "http.codec" }
            ]
          }
        ]
      },
      {
        "name": "listener2",
        "address": {
          "socket_address": {
            "address": "127.0.0.1",
            "port_value": 8080
          }
        },
        "filter_chains": [
          {
            "filters": [
              { "name": "http.codec" }
            ]
          }
        ]
      }
    ]
  })";

  auto json = JsonValue::parse(json_str);
  auto config = ServerConfig::fromJson(json);

  EXPECT_FALSE(config.validate());

  auto errors = config.getValidationErrors();
  EXPECT_FALSE(errors.empty());
  EXPECT_THAT(errors[0], HasSubstr("Port conflict"));
}

TEST_F(ListenerConfigTest, ServerConfigJsonRoundtrip) {
  auto original_json = createValidConfig();
  auto config = ServerConfig::fromJson(original_json);
  auto restored_json = config.toJson();
  auto restored_config = ServerConfig::fromJson(restored_json);

  EXPECT_EQ(config, restored_config);
}

// ============================================================================
// Context Factory Tests
// ============================================================================

class MockFilter : public mcp::network::Filter {
 public:
  mcp::network::FilterStatus onData(mcp::Buffer& data, bool end_stream) override {
    return mcp::network::FilterStatus::Continue;
  }

  mcp::network::FilterStatus onWrite(mcp::Buffer& data, bool end_stream) override {
    return mcp::network::FilterStatus::Continue;
  }

  mcp::network::FilterStatus onNewConnection() override {
    return mcp::network::FilterStatus::Continue;
  }

  void initializeReadFilterCallbacks(mcp::network::ReadFilterCallbacks& callbacks) override {}
  void initializeWriteFilterCallbacks(mcp::network::WriteFilterCallbacks& callbacks) override {}
};

class MockMcpProtocolCallbacks : public mcp::McpProtocolCallbacks {
 public:
  void onRequest(const mcp::jsonrpc::Request& request) override {}
  void onNotification(const mcp::jsonrpc::Notification& notification) override {}
  void onResponse(const mcp::jsonrpc::Response& response) override {}
  void onConnectionEvent(mcp::network::ConnectionEvent event) override {}
  void onError(const mcp::Error& error) override {}
};

TEST_F(ListenerConfigTest, ContextFactoryRegistration) {
  // Clear any existing registrations
  FilterRegistry::instance().clearFactories();

  // Create a mock factory
  auto factory = [](const FilterCreationContext&, const JsonValue&) -> mcp::network::FilterSharedPtr {
    return std::make_shared<MockFilter>();
  };

  BasicFilterMetadata metadata;
  metadata.name = "test_filter";
  metadata.version = "1.0.0";
  metadata.description = "Test filter for unit tests";

  EXPECT_TRUE(FilterRegistry::instance().registerContextFactory("test_filter", factory, metadata));
}

TEST_F(ListenerConfigTest, ContextFactoryCreation) {
  // Register a test factory
  auto factory = [](const FilterCreationContext&, const JsonValue&) -> mcp::network::FilterSharedPtr {
    return std::make_shared<MockFilter>();
  };

  BasicFilterMetadata metadata("test_filter", "Test filter");
  FilterRegistry::instance().registerContextFactory("test_filter", factory, metadata);

  // Create filter creation context
  mcp::event::LibeventDispatcher dispatcher("test");
  MockMcpProtocolCallbacks callbacks;
  TransportMetadata transport_metadata("127.0.0.1", 8080);
  FilterCreationContext context(dispatcher, callbacks, ConnectionMode::Server, transport_metadata);

  // Create filter using context
  JsonValue config = JsonValue::object();
  auto filter = FilterRegistry::instance().createFilterWithContext("test_filter", context, config);

  EXPECT_NE(filter, nullptr);
}

TEST_F(ListenerConfigTest, BasicFilterChainValidation) {
  // Register test filters
  auto factory = [](const FilterCreationContext&, const JsonValue&) -> mcp::network::FilterSharedPtr {
    return std::make_shared<MockFilter>();
  };

  BasicFilterMetadata metadata1("http.codec", "HTTP codec filter");
  BasicFilterMetadata metadata2("sse.codec", "SSE codec filter");
  BasicFilterMetadata metadata3("json_rpc.dispatcher", "JSON-RPC dispatcher filter");

  FilterRegistry::instance().registerContextFactory("http.codec", factory, metadata1);
  FilterRegistry::instance().registerContextFactory("sse.codec", factory, metadata2);
  FilterRegistry::instance().registerContextFactory("json_rpc.dispatcher", factory, metadata3);

  // Test valid filter chain
  std::vector<std::string> valid_chain = {
    "http.codec",
    "sse.codec",
    "json_rpc.dispatcher"
  };

  EXPECT_TRUE(FilterRegistry::instance().validateBasicFilterChain(valid_chain));

  // Test invalid filter chain (unknown filter)
  std::vector<std::string> invalid_chain = {
    "http.codec",
    "unknown.filter",
    "json_rpc.dispatcher"
  };

  EXPECT_FALSE(FilterRegistry::instance().validateBasicFilterChain(invalid_chain));

  // Test empty filter chain
  std::vector<std::string> empty_chain;
  EXPECT_FALSE(FilterRegistry::instance().validateBasicFilterChain(empty_chain));
}

// ============================================================================
// Integration Tests
// ============================================================================

TEST_F(ListenerConfigTest, FullIntegrationExample) {
  // This test demonstrates the complete workflow from JSON to filter creation

  // 1. Parse JSON configuration
  auto json = createValidConfig();
  auto server_config = ServerConfig::fromJson(json);

  // 2. Validate configuration
  EXPECT_TRUE(server_config.validate());

  // 3. Register necessary filters
  auto factory = [](const FilterCreationContext&, const JsonValue&) -> mcp::network::FilterSharedPtr {
    return std::make_shared<MockFilter>();
  };

  BasicFilterMetadata http_metadata("http.codec", "HTTP codec");
  BasicFilterMetadata sse_metadata("sse.codec", "SSE codec");
  BasicFilterMetadata rpc_metadata("json_rpc.dispatcher", "JSON-RPC dispatcher");

  FilterRegistry::instance().registerContextFactory("http.codec", factory, http_metadata);
  FilterRegistry::instance().registerContextFactory("sse.codec", factory, sse_metadata);
  FilterRegistry::instance().registerContextFactory("json_rpc.dispatcher", factory, rpc_metadata);

  // 4. Validate filter chain against registry
  const auto& listener = server_config.listeners[0];
  const auto& filter_chain = listener.filter_chains[0];

  std::vector<std::string> filter_names;
  for (const auto& filter_config : filter_chain.filters) {
    filter_names.push_back(filter_config.name);
  }

  EXPECT_TRUE(FilterRegistry::instance().validateBasicFilterChain(filter_names));

  // 5. Create filters with context
  mcp::event::LibeventDispatcher dispatcher("test");
  MockMcpProtocolCallbacks callbacks;
  TransportMetadata transport_metadata(
      listener.address.socket_address.address,
      listener.address.socket_address.port_value);
  FilterCreationContext context(dispatcher, callbacks, ConnectionMode::Server, transport_metadata);

  // Create each filter in the chain
  for (const auto& filter_config : filter_chain.filters) {
    auto filter = FilterRegistry::instance().createFilterWithContext(
        filter_config.name, context, filter_config.config);
    EXPECT_NE(filter, nullptr);
  }
}

// Test runner initialization
int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}