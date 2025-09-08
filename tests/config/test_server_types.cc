/**
 * @file test_server_types.cc
 * @brief Unit tests for server configuration types
 */

#include <gtest/gtest.h>

#include "mcp/config/types.h"

using namespace mcp::config;

class ConfigVersionTest : public ::testing::Test {
 protected:
  void SetUp() override {}
  void TearDown() override {}
};

TEST_F(ConfigVersionTest, DefaultConstructor) {
  ConfigVersion v;
  EXPECT_EQ(v.major(), 1);
  EXPECT_EQ(v.minor(), 0);
  EXPECT_EQ(v.patch(), 0);
  EXPECT_EQ(v.toString(), "1.0.0");
}

TEST_F(ConfigVersionTest, ParameterizedConstructor) {
  ConfigVersion v(2, 3, 4);
  EXPECT_EQ(v.major(), 2);
  EXPECT_EQ(v.minor(), 3);
  EXPECT_EQ(v.patch(), 4);
  EXPECT_EQ(v.toString(), "2.3.4");
}

TEST_F(ConfigVersionTest, ParseValidVersion) {
  auto v1 = ConfigVersion::parse("1.2.3");
  EXPECT_EQ(v1.toString(), "1.2.3");

  auto v2 = ConfigVersion::parse("v2.0.1");
  EXPECT_EQ(v2.toString(), "2.0.1");

  auto v3 = ConfigVersion::parse("10.20");
  EXPECT_EQ(v3.toString(), "10.20.0");
}

TEST_F(ConfigVersionTest, ParseInvalidVersion) {
  EXPECT_THROW(ConfigVersion::parse("invalid"), ConfigValidationError);
  EXPECT_THROW(ConfigVersion::parse("1-2-3"), ConfigValidationError);
  EXPECT_THROW(ConfigVersion::parse(""), ConfigValidationError);
}

TEST_F(ConfigVersionTest, ComparisonOperators) {
  ConfigVersion v1(1, 2, 3);
  ConfigVersion v2(1, 2, 3);
  ConfigVersion v3(2, 0, 0);
  ConfigVersion v4(1, 3, 0);
  ConfigVersion v5(1, 2, 4);

  // Equality
  EXPECT_EQ(v1, v2);
  EXPECT_NE(v1, v3);

  // Less than
  EXPECT_LT(v1, v3);  // Major version difference
  EXPECT_LT(v1, v4);  // Minor version difference
  EXPECT_LT(v1, v5);  // Patch version difference

  // Greater than
  EXPECT_GT(v3, v1);
  EXPECT_GT(v4, v1);
  EXPECT_GT(v5, v1);

  // Less than or equal
  EXPECT_LE(v1, v2);
  EXPECT_LE(v1, v3);

  // Greater than or equal
  EXPECT_GE(v1, v2);
  EXPECT_GE(v3, v1);
}

TEST_F(ConfigVersionTest, CompatibilityCheck) {
  ConfigVersion v1(1, 2, 3);
  ConfigVersion v2(1, 2, 0);
  ConfigVersion v3(1, 3, 0);
  ConfigVersion v4(2, 0, 0);

  EXPECT_TRUE(v1.isCompatibleWith(v2));   // Same major, newer minor
  EXPECT_TRUE(v3.isCompatibleWith(v2));   // Same major, newer minor
  EXPECT_FALSE(v4.isCompatibleWith(v2));  // Different major
  EXPECT_FALSE(v2.isCompatibleWith(v3));  // Older minor
}

class FilterConfigTest : public ::testing::Test {
 protected:
  void SetUp() override {}
  void TearDown() override {}
};

TEST_F(FilterConfigTest, DefaultValues) {
  FilterConfig fc;
  EXPECT_TRUE(fc.type.empty());
  EXPECT_TRUE(fc.name.empty());
  EXPECT_TRUE(fc.config.isObject());
  EXPECT_TRUE(fc.config.empty());
  EXPECT_TRUE(fc.enabled);
}

TEST_F(FilterConfigTest, Validation) {
  FilterConfig fc;

  // Empty type
  EXPECT_THROW(fc.validate(), ConfigValidationError);

  // Empty name
  fc.type = "buffer";
  EXPECT_THROW(fc.validate(), ConfigValidationError);

  // Valid configuration
  fc.name = "request_buffer";
  EXPECT_NO_THROW(fc.validate());
}

TEST_F(FilterConfigTest, JsonSerialization) {
  FilterConfig fc;
  fc.type = "rate_limit";
  fc.name = "api_rate_limit";
  fc.config["requests_per_second"] = 100;
  fc.config["burst_size"] = 200;
  fc.enabled = false;

  auto json = fc.toJson();
  EXPECT_EQ(json["type"].getString(), "rate_limit");
  EXPECT_EQ(json["name"].getString(), "api_rate_limit");
  EXPECT_EQ(json["config"]["requests_per_second"].getInt(), 100);
  EXPECT_EQ(json["config"]["burst_size"].getInt(), 200);
  EXPECT_EQ(json["enabled"].getBool(), false);

  auto fc2 = FilterConfig::fromJson(json);
  EXPECT_EQ(fc, fc2);
}

TEST_F(FilterConfigTest, EqualityOperator) {
  FilterConfig fc1;
  fc1.type = "buffer";
  fc1.name = "test";
  fc1.config["size"] = 1024;

  FilterConfig fc2 = fc1;
  EXPECT_EQ(fc1, fc2);

  fc2.enabled = false;
  EXPECT_NE(fc1, fc2);
}

class FilterChainConfigTest : public ::testing::Test {
 protected:
  void SetUp() override {}
  void TearDown() override {}
};

TEST_F(FilterChainConfigTest, DefaultValues) {
  FilterChainConfig fcc;
  EXPECT_EQ(fcc.name, "default");
  EXPECT_EQ(fcc.transport_type, "tcp");
  EXPECT_TRUE(fcc.filters.empty());
}

TEST_F(FilterChainConfigTest, Validation) {
  FilterChainConfig fcc;

  // Valid with defaults
  EXPECT_NO_THROW(fcc.validate());

  // Empty name
  fcc.name = "";
  EXPECT_THROW(fcc.validate(), ConfigValidationError);
  fcc.name = "test";

  // Empty transport type
  fcc.transport_type = "";
  EXPECT_THROW(fcc.validate(), ConfigValidationError);
  fcc.transport_type = "http";

  // Invalid filter in chain
  FilterConfig fc;
  fcc.filters.push_back(fc);
  EXPECT_THROW(fcc.validate(), ConfigValidationError);

  // Duplicate filter names
  fcc.filters.clear();
  FilterConfig fc1;
  fc1.type = "buffer";
  fc1.name = "duplicate";
  FilterConfig fc2;
  fc2.type = "rate_limit";
  fc2.name = "duplicate";

  fcc.filters.push_back(fc1);
  fcc.filters.push_back(fc2);
  EXPECT_THROW(fcc.validate(), ConfigValidationError);
}

TEST_F(FilterChainConfigTest, JsonSerialization) {
  FilterChainConfig fcc;
  fcc.name = "http_chain";
  fcc.transport_type = "http";

  FilterConfig fc1;
  fc1.type = "http_codec";
  fc1.name = "codec";
  fc1.config["max_header_size"] = 8192;

  FilterConfig fc2;
  fc2.type = "sse_codec";
  fc2.name = "sse";

  fcc.filters.push_back(fc1);
  fcc.filters.push_back(fc2);

  auto json = fcc.toJson();
  EXPECT_EQ(json["name"].getString(), "http_chain");
  EXPECT_EQ(json["transport_type"].getString(), "http");
  EXPECT_EQ(json["filters"].size(), 2U);

  auto fcc2 = FilterChainConfig::fromJson(json);
  EXPECT_EQ(fcc, fcc2);
}

TEST_F(FilterChainConfigTest, MergeOperation) {
  FilterChainConfig base;
  base.name = "base";
  base.transport_type = "tcp";

  FilterConfig fc1;
  fc1.type = "buffer";
  fc1.name = "buf1";
  base.filters.push_back(fc1);

  FilterChainConfig overlay;
  overlay.name = "overlay";
  overlay.transport_type = "http";

  FilterConfig fc2;
  fc2.type = "rate_limit";
  fc2.name = "rate1";
  overlay.filters.push_back(fc2);

  base.merge(overlay);

  EXPECT_EQ(base.name, "overlay");
  EXPECT_EQ(base.transport_type, "http");
  EXPECT_EQ(base.filters.size(), 1);
  EXPECT_EQ(base.filters[0].type, "rate_limit");
}

class TLSConfigTest : public ::testing::Test {
 protected:
  void SetUp() override {}
  void TearDown() override {}
};

TEST_F(TLSConfigTest, DefaultValues) {
  TLSConfig tls;
  EXPECT_FALSE(tls.enabled);
  EXPECT_TRUE(tls.cert_file.empty());
  EXPECT_TRUE(tls.key_file.empty());
  EXPECT_TRUE(tls.ca_file.empty());
  EXPECT_FALSE(tls.verify_client);
  EXPECT_EQ(tls.min_version, "1.2");
  EXPECT_TRUE(tls.cipher_suites.empty());
}

TEST_F(TLSConfigTest, Validation) {
  TLSConfig tls;

  // Disabled TLS should validate
  EXPECT_NO_THROW(tls.validate());

  // Enabled without cert/key should fail
  tls.enabled = true;
  EXPECT_THROW(tls.validate(), ConfigValidationError);

  // With cert but no key
  tls.cert_file = "/path/to/cert";
  EXPECT_THROW(tls.validate(), ConfigValidationError);

  // With cert and key
  tls.key_file = "/path/to/key";
  EXPECT_NO_THROW(tls.validate());

  // Client verification without CA
  tls.verify_client = true;
  EXPECT_THROW(tls.validate(), ConfigValidationError);

  // With CA file
  tls.ca_file = "/path/to/ca";
  EXPECT_NO_THROW(tls.validate());

  // Invalid TLS version
  tls.min_version = "0.9";
  EXPECT_THROW(tls.validate(), ConfigValidationError);

  // Valid TLS versions
  tls.min_version = "1.0";
  EXPECT_NO_THROW(tls.validate());
  tls.min_version = "1.1";
  EXPECT_NO_THROW(tls.validate());
  tls.min_version = "1.2";
  EXPECT_NO_THROW(tls.validate());
  tls.min_version = "1.3";
  EXPECT_NO_THROW(tls.validate());
}

TEST_F(TLSConfigTest, JsonSerialization) {
  TLSConfig tls;
  tls.enabled = true;
  tls.cert_file = "/etc/ssl/server.crt";
  tls.key_file = "/etc/ssl/server.key";
  tls.ca_file = "/etc/ssl/ca.crt";
  tls.verify_client = true;
  tls.min_version = "1.3";
  tls.cipher_suites = {"TLS_AES_256_GCM_SHA384",
                       "TLS_CHACHA20_POLY1305_SHA256"};

  auto json = tls.toJson();
  auto tls2 = TLSConfig::fromJson(json);

  EXPECT_EQ(tls, tls2);
}

class TransportConfigTest : public ::testing::Test {
 protected:
  void SetUp() override {}
  void TearDown() override {}
};

TEST_F(TransportConfigTest, DefaultValues) {
  TransportConfig tc;
  EXPECT_EQ(tc.type, "tcp");
  EXPECT_EQ(tc.address, "127.0.0.1");
  EXPECT_EQ(tc.port, 3333);
  EXPECT_FALSE(tc.tls.enabled);
  EXPECT_EQ(tc.filter_chain, "default");
  EXPECT_TRUE(tc.enabled);
}

TEST_F(TransportConfigTest, Validation) {
  TransportConfig tc;

  // Valid defaults
  EXPECT_NO_THROW(tc.validate());

  // Empty type
  tc.type = "";
  EXPECT_THROW(tc.validate(), ConfigValidationError);
  tc.type = "tcp";

  // Network transport without address
  tc.address = "";
  EXPECT_THROW(tc.validate(), ConfigValidationError);
  tc.address = "0.0.0.0";

  // Network transport with invalid port
  tc.port = 0;
  EXPECT_THROW(tc.validate(), ConfigValidationError);
  tc.port = 8080;

  // HTTPS requires TLS
  tc.type = "https";
  EXPECT_THROW(tc.validate(), ConfigValidationError);
  tc.tls.enabled = true;
  tc.tls.cert_file = "/cert";
  tc.tls.key_file = "/key";
  EXPECT_NO_THROW(tc.validate());

  // Empty filter chain
  tc.filter_chain = "";
  EXPECT_THROW(tc.validate(), ConfigValidationError);
}

TEST_F(TransportConfigTest, StdioTransport) {
  TransportConfig tc;
  tc.type = "stdio";

  // Stdio doesn't require address/port
  tc.address = "";
  tc.port = 0;
  EXPECT_NO_THROW(tc.validate());
}

TEST_F(TransportConfigTest, JsonSerialization) {
  TransportConfig tc;
  tc.type = "https";
  tc.address = "0.0.0.0";
  tc.port = 8443;
  tc.tls.enabled = true;
  tc.tls.cert_file = "/cert";
  tc.tls.key_file = "/key";
  tc.filter_chain = "secure";
  tc.enabled = false;

  auto json = tc.toJson();
  auto tc2 = TransportConfig::fromJson(json);

  EXPECT_EQ(tc, tc2);
}

class CapabilitiesConfigTest : public ::testing::Test {
 protected:
  void SetUp() override {}
  void TearDown() override {}
};

TEST_F(CapabilitiesConfigTest, DefaultValues) {
  CapabilitiesConfig cc;
  EXPECT_EQ(cc.features.size(), 3);
  EXPECT_EQ(cc.features[0], "tools");
  EXPECT_EQ(cc.features[1], "prompts");
  EXPECT_EQ(cc.features[2], "resources");
  EXPECT_EQ(cc.max_request_size, 10 * 1024 * 1024);
  EXPECT_EQ(cc.max_response_size, 10 * 1024 * 1024);
  EXPECT_EQ(cc.request_timeout_ms, 30000);
}

TEST_F(CapabilitiesConfigTest, JsonSerialization) {
  CapabilitiesConfig cc;
  cc.features = {"custom1", "custom2"};
  cc.max_request_size = 5 * 1024 * 1024;
  cc.max_response_size = 20 * 1024 * 1024;
  cc.request_timeout_ms = 60000;

  auto json = cc.toJson();
  auto cc2 = CapabilitiesConfig::fromJson(json);

  EXPECT_EQ(cc, cc2);
}

class ServerConfigTest : public ::testing::Test {
 protected:
  void SetUp() override {}
  void TearDown() override {}
};

TEST_F(ServerConfigTest, DefaultValues) {
  ServerConfig sc;
  EXPECT_EQ(sc.name, "gopher-mcp-server");
  EXPECT_EQ(sc.version, ConfigVersion(1, 0, 0));
  EXPECT_EQ(sc.max_sessions, 1000);
  EXPECT_EQ(sc.session_timeout_ms, 300000);
  EXPECT_EQ(sc.worker_threads, 0);
  EXPECT_EQ(sc.event_threads, 1);
  EXPECT_TRUE(sc.transports.empty());
  EXPECT_TRUE(sc.filter_chains.empty());
}

TEST_F(ServerConfigTest, Validation) {
  ServerConfig sc;

  // Add matching transport and filter chain
  TransportConfig tc;
  tc.filter_chain = "test_chain";
  sc.transports.push_back(tc);

  FilterChainConfig fcc;
  fcc.name = "test_chain";
  sc.filter_chains.push_back(fcc);

  EXPECT_NO_THROW(sc.validate());

  // Empty name
  sc.name = "";
  EXPECT_THROW(sc.validate(), ConfigValidationError);
  sc.name = "test";

  // Zero max sessions
  sc.max_sessions = 0;
  EXPECT_THROW(sc.validate(), ConfigValidationError);
  sc.max_sessions = 100;

  // Zero event threads
  sc.event_threads = 0;
  EXPECT_THROW(sc.validate(), ConfigValidationError);
  sc.event_threads = 2;

  // Transport references non-existent chain
  sc.transports[0].filter_chain = "non_existent";
  EXPECT_THROW(sc.validate(), ConfigValidationError);
}

TEST_F(ServerConfigTest, JsonSerialization) {
  ServerConfig sc;
  sc.name = "test_server";
  sc.version = ConfigVersion(2, 1, 3);
  sc.max_sessions = 500;
  sc.session_timeout_ms = 60000;
  sc.worker_threads = 4;
  sc.event_threads = 2;

  TransportConfig tc;
  tc.type = "http";
  tc.port = 8080;
  tc.filter_chain = "http";
  sc.transports.push_back(tc);

  FilterChainConfig fcc;
  fcc.name = "http";
  fcc.transport_type = "http";
  sc.filter_chains.push_back(fcc);

  auto json = sc.toJson();
  auto sc2 = ServerConfig::fromJson(json);

  EXPECT_EQ(sc, sc2);
}

TEST_F(ServerConfigTest, MergeOperation) {
  ServerConfig base;
  base.name = "base_server";
  base.max_sessions = 100;

  ServerConfig overlay;
  overlay.name = "overlay_server";
  overlay.max_sessions = 200;
  overlay.worker_threads = 8;

  TransportConfig tc;
  tc.port = 9090;
  overlay.transports.push_back(tc);

  base.merge(overlay);

  EXPECT_EQ(base.name, "overlay_server");
  EXPECT_EQ(base.max_sessions, 200);
  EXPECT_EQ(base.worker_threads, 8);
  EXPECT_EQ(base.transports.size(), 1);
  EXPECT_EQ(base.transports[0].port, 9090);
}

class ConfigFactoryTest : public ::testing::Test {
 protected:
  void SetUp() override {}
  void TearDown() override {}
};

TEST_F(ConfigFactoryTest, CreateDefaultServerConfig) {
  auto config = ConfigFactory::createDefaultServerConfig();

  EXPECT_EQ(config.name, "gopher-mcp-server");
  EXPECT_EQ(config.transports.size(), 1);
  EXPECT_EQ(config.transports[0].type, "tcp");
  EXPECT_EQ(config.transports[0].port, 3333);
  EXPECT_EQ(config.filter_chains.size(), 1);
  EXPECT_EQ(config.filter_chains[0].name, "default");
  EXPECT_EQ(config.filter_chains[0].filters.size(), 1);
  EXPECT_EQ(config.filter_chains[0].filters[0].type, "buffer");

  EXPECT_NO_THROW(config.validate());
}

TEST_F(ConfigFactoryTest, CreateHttpServerConfig) {
  auto config = ConfigFactory::createHttpServerConfig();

  EXPECT_EQ(config.name, "gopher-mcp-http-server");
  EXPECT_EQ(config.transports.size(), 1);
  EXPECT_EQ(config.transports[0].type, "http");
  EXPECT_EQ(config.transports[0].port, 8080);
  EXPECT_EQ(config.transports[0].address, "0.0.0.0");

  // Should have both default and http chains
  EXPECT_EQ(config.filter_chains.size(), 2);

  // Find the http chain
  bool found_http_chain = false;
  for (const auto& chain : config.filter_chains) {
    if (chain.name == "http") {
      found_http_chain = true;
      EXPECT_EQ(chain.filters.size(), 2);
      EXPECT_EQ(chain.filters[0].type, "http_codec");
      EXPECT_EQ(chain.filters[1].type, "sse_codec");
    }
  }
  EXPECT_TRUE(found_http_chain);

  EXPECT_NO_THROW(config.validate());
}

TEST_F(ConfigFactoryTest, CreateHttpsServerConfig) {
  auto config = ConfigFactory::createHttpsServerConfig();

  EXPECT_EQ(config.name, "gopher-mcp-https-server");
  EXPECT_EQ(config.transports.size(), 1);
  EXPECT_EQ(config.transports[0].type, "https");
  EXPECT_EQ(config.transports[0].port, 8443);
  EXPECT_TRUE(config.transports[0].tls.enabled);
  EXPECT_EQ(config.transports[0].tls.cert_file, "/etc/mcp/server.crt");
  EXPECT_EQ(config.transports[0].tls.key_file, "/etc/mcp/server.key");
  EXPECT_EQ(config.transports[0].tls.min_version, "1.2");

  EXPECT_NO_THROW(config.validate());
}

TEST_F(ConfigFactoryTest, RealWorldScenario) {
  // Create a complex configuration mixing various features
  ServerConfig config;
  config.name = "production-mcp-server";
  config.version = ConfigVersion(2, 0, 0);
  config.max_sessions = 5000;
  config.session_timeout_ms = 600000;  // 10 minutes
  config.worker_threads = 16;
  config.event_threads = 4;

  // Add multiple transports
  TransportConfig tcp;
  tcp.type = "tcp";
  tcp.address = "0.0.0.0";
  tcp.port = 3333;
  tcp.filter_chain = "tcp_chain";
  config.transports.push_back(tcp);

  TransportConfig https;
  https.type = "https";
  https.address = "0.0.0.0";
  https.port = 8443;
  https.tls.enabled = true;
  https.tls.cert_file = "/etc/ssl/server.crt";
  https.tls.key_file = "/etc/ssl/server.key";
  https.tls.min_version = "1.2";
  https.filter_chain = "https_chain";
  config.transports.push_back(https);

  // Add corresponding filter chains
  FilterChainConfig tcp_chain;
  tcp_chain.name = "tcp_chain";
  tcp_chain.transport_type = "tcp";

  FilterConfig buffer;
  buffer.type = "buffer";
  buffer.name = "tcp_buffer";
  buffer.config["max_size"] = 2 * 1024 * 1024;
  tcp_chain.filters.push_back(buffer);

  FilterConfig rate_limit;
  rate_limit.type = "rate_limit";
  rate_limit.name = "tcp_rate_limit";
  rate_limit.config["requests_per_second"] = 1000;
  tcp_chain.filters.push_back(rate_limit);

  config.filter_chains.push_back(tcp_chain);

  FilterChainConfig https_chain;
  https_chain.name = "https_chain";
  https_chain.transport_type = "https";

  FilterConfig http_codec;
  http_codec.type = "http_codec";
  http_codec.name = "http_codec";
  http_codec.config["max_header_size"] = 16384;
  https_chain.filters.push_back(http_codec);

  FilterConfig sse_codec;
  sse_codec.type = "sse_codec";
  sse_codec.name = "sse_codec";
  https_chain.filters.push_back(sse_codec);

  FilterConfig auth;
  auth.type = "auth";
  auth.name = "jwt_auth";
  auth.config["secret"] = "test_secret";
  auth.config["algorithm"] = "HS256";
  https_chain.filters.push_back(auth);

  config.filter_chains.push_back(https_chain);

  // Validate the entire configuration
  EXPECT_NO_THROW(config.validate());

  // Test JSON round-trip
  auto json = config.toJson();
  auto config2 = ServerConfig::fromJson(json);
  EXPECT_EQ(config, config2);

  // Ensure the loaded config is also valid
  EXPECT_NO_THROW(config2.validate());
}