/**
 * @file test_parse_error.cc
 * @brief Unit tests for enhanced error diagnostics
 */

#include <sstream>

#include <gtest/gtest.h>

#include "mcp/config/enhanced_types.h"
#include "mcp/config/parse_error.h"
#include "mcp/json/json_bridge.h"
#include "test_json_helpers.h"

using namespace mcp::config;
using mcp::json::JsonValue;
using test::makeJsonObject;
using test::makeJsonArray;
using test::str;
using test::num;
using test::boolean;

class ParseErrorTest : public ::testing::Test {
 protected:
  void SetUp() override {}
  void TearDown() override {}
};

// Basic error creation tests
TEST_F(ParseErrorTest, ConfigParseErrorBasic) {
  ConfigParseError err("Something went wrong");
  std::string msg = err.what();
  EXPECT_NE(msg.find("Something went wrong"), std::string::npos);
}

TEST_F(ParseErrorTest, ConfigParseErrorWithField) {
  ConfigParseError err("Invalid value", "server.port");
  std::string msg = err.what();
  EXPECT_NE(msg.find("field 'server.port'"), std::string::npos);
  EXPECT_NE(msg.find("Invalid value"), std::string::npos);
}

TEST_F(ParseErrorTest, ConfigParseErrorWithFile) {
  ConfigParseError err("File not found", "", "/etc/config.json", 42);
  std::string msg = err.what();
  EXPECT_NE(msg.find("/etc/config.json:42"), std::string::npos);
}

// ParseContext tests
TEST_F(ParseErrorTest, ParseContextPathTracking) {
  ParseContext ctx;

  EXPECT_EQ(ctx.getCurrentPath(), "");

  ctx.pushField("server");
  EXPECT_EQ(ctx.getCurrentPath(), "server");

  ctx.pushField("capabilities");
  EXPECT_EQ(ctx.getCurrentPath(), "server.capabilities");

  ctx.pushField("features");
  EXPECT_EQ(ctx.getCurrentPath(), "server.capabilities.features");

  ctx.popField();
  EXPECT_EQ(ctx.getCurrentPath(), "server.capabilities");

  ctx.popField();
  ctx.popField();
  EXPECT_EQ(ctx.getCurrentPath(), "");
}

TEST_F(ParseErrorTest, ParseContextFieldScope) {
  ParseContext ctx;

  {
    ParseContext::FieldScope scope1(ctx, "level1");
    EXPECT_EQ(ctx.getCurrentPath(), "level1");

    {
      ParseContext::FieldScope scope2(ctx, "level2");
      EXPECT_EQ(ctx.getCurrentPath(), "level1.level2");
    }

    EXPECT_EQ(ctx.getCurrentPath(), "level1");
  }

  EXPECT_EQ(ctx.getCurrentPath(), "");
}

TEST_F(ParseErrorTest, ParseContextCreateError) {
  ParseContext ctx;
  ctx.pushField("server");
  ctx.pushField("port");
  ctx.setFile("config.yaml");

  auto err = ctx.createError("Invalid port number");

  std::string msg = err.what();
  EXPECT_NE(msg.find("server.port"), std::string::npos);
  EXPECT_NE(msg.find("config.yaml"), std::string::npos);
  EXPECT_NE(msg.find("Invalid port number"), std::string::npos);
}

// JSON field accessor tests
TEST_F(ParseErrorTest, GetJsonFieldSuccess) {
  ParseContext ctx;
  auto j = mcp::json::JsonValue::object();
  j["port"] = mcp::json::JsonValue(8080);

  int port = getJsonField<int>(j, "port", ctx);
  EXPECT_EQ(port, 8080);
}

TEST_F(ParseErrorTest, GetJsonFieldMissing) {
  ParseContext ctx;
  auto j = mcp::json::JsonValue::object();
  j["host"] = mcp::json::JsonValue("localhost");

  EXPECT_THROW({ getJsonField<int>(j, "port", ctx); }, ConfigParseError);

  try {
    getJsonField<int>(j, "port", ctx);
  } catch (const ConfigParseError& e) {
    std::string msg = e.what();
    EXPECT_NE(msg.find("Required field 'port' is missing"), std::string::npos);
  }
}

TEST_F(ParseErrorTest, GetJsonFieldWrongType) {
  ParseContext ctx;
  JsonValue j = makeJsonObject({{"port", str("not-a-number")}});

  EXPECT_THROW({ getJsonField<int>(j, "port", ctx); }, ConfigParseError);

  try {
    getJsonField<int>(j, "port", ctx);
  } catch (const ConfigParseError& e) {
    std::string msg = e.what();
    EXPECT_NE(msg.find("Failed to parse field 'port'"), std::string::npos);
  }
}

TEST_F(ParseErrorTest, GetOptionalJsonField) {
  ParseContext ctx;
  JsonValue j = makeJsonObject({{"name", str("test")}});

  std::string name;
  bool found = getOptionalJsonField(j, "name", name, ctx);
  EXPECT_TRUE(found);
  EXPECT_EQ(name, "test");

  int port;
  found = getOptionalJsonField(j, "port", port, ctx);
  EXPECT_FALSE(found);
}

// Enhanced type parsing tests
TEST_F(ParseErrorTest, NodeConfigEnhancedMissingRequired) {
  ParseContext ctx;
  JsonValue j = makeJsonObject({{"cluster", str("prod")}});  // Missing 'id'

  EXPECT_THROW({ NodeConfigEnhanced::fromJson(j, ctx); }, ConfigParseError);

  try {
    NodeConfigEnhanced::fromJson(j, ctx);
  } catch (const ConfigParseError& e) {
    std::string msg = e.what();
    EXPECT_NE(msg.find("Required field 'id' is missing"), std::string::npos);
  }
}

TEST_F(ParseErrorTest, NodeConfigEnhancedInvalidType) {
  ParseContext ctx;
  JsonValue j = makeJsonObject({{"id", num(123)}, {"cluster", str("prod")}});  // Should be string

  EXPECT_THROW({ NodeConfigEnhanced::fromJson(j, ctx); }, ConfigParseError);
}

TEST_F(ParseErrorTest, NodeConfigEnhancedValidationError) {
  ParseContext ctx;
  JsonValue j = makeJsonObject({{"id", str("")}, {"cluster", str("prod")}});  // Empty ID will fail validation

  try {
    NodeConfigEnhanced::fromJson(j, ctx);
  } catch (const ConfigParseError& e) {
    std::string msg = e.what();
    EXPECT_NE(msg.find("Node ID cannot be empty"), std::string::npos);
  }
}

TEST_F(ParseErrorTest, CapabilitiesConfigEnhancedUnitParsing) {
  ParseContext ctx;

  // Valid units
  JsonValue valid = makeJsonObject({
    {"max_request_size", str("10MB")},
    {"max_response_size", str("5MB")},
    {"request_timeout", str("30s")}
  });

  EXPECT_NO_THROW({
    auto config = CapabilitiesConfigEnhanced::fromJson(valid, ctx);
    EXPECT_EQ(config.max_request_size, 10 * 1024 * 1024);
    EXPECT_EQ(config.request_timeout_ms, 30000);
  });

  // Invalid unit
  JsonValue invalid = makeJsonObject({
    {"max_request_size", str("10 MEGABYTES")}  // Invalid unit format
  });

  try {
    CapabilitiesConfigEnhanced::fromJson(invalid, ctx);
  } catch (const ConfigParseError& e) {
    std::string msg = e.what();
    EXPECT_NE(msg.find("Invalid size format"), std::string::npos);
  }
}

TEST_F(ParseErrorTest, FilterConfigEnhancedWithConfig) {
  ParseContext ctx;

  // Buffer filter with size unit
  JsonValue buffer = makeJsonObject({
    {"type", str("buffer")},
    {"name", str("request_buffer")},
    {"config", makeJsonObject({{"max_size", str("2MB")}})}
  });

  auto config = FilterConfigEnhanced::fromJson(buffer, ctx);
  EXPECT_EQ(config.type, "buffer");
  EXPECT_EQ(config.config["max_size"].getInt(), 2 * 1024 * 1024);

  // Rate limit with duration
  JsonValue rate_limit = makeJsonObject({
    {"type", str("rate_limit")},
    {"name", str("api_limiter")},
    {"config", makeJsonObject({{"window_duration", str("1m")}})}
  });

  config = FilterConfigEnhanced::fromJson(rate_limit, ctx);
  EXPECT_EQ(config.config["window_duration"].getInt(), 60000);
}

TEST_F(ParseErrorTest, ServerConfigEnhancedNestedErrors) {
  ParseContext ctx;
  JsonValue j = makeJsonObject({
    {"name", str("test-server")},
    {"version", str("invalid-version")},  // Will fail version parsing
    {"capabilities", makeJsonObject({
      {"max_request_size", str("invalid")}  // Will fail unit parsing
    })}
  });

  try {
    ServerConfigEnhanced::fromJson(j, ctx);
  } catch (const ConfigParseError& e) {
    std::string msg = e.what();
    // Should indicate which field failed
    EXPECT_TRUE(msg.find("version") != std::string::npos ||
                msg.find("capabilities") != std::string::npos);
  }
}

TEST_F(ParseErrorTest, ServerConfigEnhancedArrayErrors) {
  ParseContext ctx;
  JsonValue j = makeJsonObject({
    {"name", str("test-server")},
    {"filter_chains", makeJsonArray({
      makeJsonObject({
        {"name", str("chain1")},
        {"filters", makeJsonArray({
          makeJsonObject({
            {"type", str("buffer")}
            // Missing required 'name' field
          })
        })}
      })
    })}
  });

  try {
    ServerConfigEnhanced::fromJson(j, ctx);
  } catch (const ConfigParseError& e) {
    std::string msg = e.what();
    // Should indicate array index
    EXPECT_NE(msg.find("[0]"), std::string::npos);
  }
}

// JsonPath tests
TEST_F(ParseErrorTest, JsonPathConstruction) {
  JsonPath path;
  path = path / "server" / "capabilities" / "features" / 0;

  EXPECT_EQ(path.toString(), "server.capabilities.features[0]");

  JsonPath path2;
  std::string str = path2 / "array" / 1 / "nested" / 2;
  EXPECT_EQ(str, "array[1].nested[2]");
}

// Error excerpt tests
TEST_F(ParseErrorTest, GetJsonExcerpt) {
  JsonValue j = makeJsonObject({{"key1", str("value1")}, {"key2", num(123)}, {"key3", boolean(true)}});

  std::string excerpt = getJsonExcerpt(j, 50);
  EXPECT_LE(excerpt.length(), 50);
  EXPECT_NE(excerpt.find("key1"), std::string::npos);
}

// Validation type tests
TEST_F(ParseErrorTest, ValidateJsonType) {
  ParseContext ctx;

  JsonValue obj = makeJsonObject({{"key", str("value")}});
  EXPECT_NO_THROW(
      validateJsonType(obj, mcp::json::JsonType::Object, "test", ctx));

  JsonValue arr = makeJsonArray({num(1), num(2), num(3)});
  EXPECT_NO_THROW(
      validateJsonType(arr, mcp::json::JsonType::Array, "test", ctx));

  JsonValue str_val = str("string");
  EXPECT_THROW(
      validateJsonType(str_val, mcp::json::JsonType::Object, "test", ctx),
      ConfigParseError);
}

// TryParseConfig tests
TEST_F(ParseErrorTest, TryParseConfigSuccess) {
  JsonValue j = makeJsonObject({{"name", str("test-server")}, {"version", str("1.0.0")}});

  ServerConfigEnhanced config;
  std::string error;

  bool success = tryParseConfig(j, config, error);
  EXPECT_TRUE(success);
  EXPECT_TRUE(error.empty());
  EXPECT_EQ(config.name, "test-server");
}

TEST_F(ParseErrorTest, TryParseConfigFailure) {
  JsonValue j = makeJsonObject({
    {"name", num(123)},  // Wrong type
    {"version", str("1.0.0")}
  });

  ServerConfigEnhanced config;
  std::string error;

  bool success = tryParseConfig(j, config, error);
  EXPECT_FALSE(success);
  EXPECT_FALSE(error.empty());
  EXPECT_NE(error.find("parse"), std::string::npos);
}

// File loading with diagnostics
TEST_F(ParseErrorTest, LoadEnhancedConfigFileNotFound) {
  EXPECT_THROW(
      { loadEnhancedConfig<ServerConfigEnhanced>("/non/existent/file.json"); },
      ConfigParseError);

  try {
    loadEnhancedConfig<ServerConfigEnhanced>("/non/existent/file.json");
  } catch (const ConfigParseError& e) {
    std::string msg = e.what();
    EXPECT_NE(msg.find("Cannot open"), std::string::npos);
    EXPECT_NE(msg.find("/non/existent/file.json"), std::string::npos);
  }
}
