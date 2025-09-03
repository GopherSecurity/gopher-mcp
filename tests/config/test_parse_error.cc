/**
 * @file test_parse_error.cc
 * @brief Unit tests for enhanced error diagnostics
 */

#include <gtest/gtest.h>
#include "mcp/config/enhanced_types.h"
#include "mcp/config/parse_error.h"
#include <sstream>

using namespace mcp::config;

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
    nlohmann::json j = {{"port", 8080}};
    
    int port = getJsonField<int>(j, "port", ctx);
    EXPECT_EQ(port, 8080);
}

TEST_F(ParseErrorTest, GetJsonFieldMissing) {
    ParseContext ctx;
    nlohmann::json j = {{"host", "localhost"}};
    
    EXPECT_THROW({
        getJsonField<int>(j, "port", ctx);
    }, ConfigParseError);
    
    try {
        getJsonField<int>(j, "port", ctx);
    } catch (const ConfigParseError& e) {
        std::string msg = e.what();
        EXPECT_NE(msg.find("Required field 'port' is missing"), std::string::npos);
    }
}

TEST_F(ParseErrorTest, GetJsonFieldWrongType) {
    ParseContext ctx;
    nlohmann::json j = {{"port", "not-a-number"}};
    
    EXPECT_THROW({
        getJsonField<int>(j, "port", ctx);
    }, ConfigParseError);
    
    try {
        getJsonField<int>(j, "port", ctx);
    } catch (const ConfigParseError& e) {
        std::string msg = e.what();
        EXPECT_NE(msg.find("Failed to parse field 'port'"), std::string::npos);
    }
}

TEST_F(ParseErrorTest, GetOptionalJsonField) {
    ParseContext ctx;
    nlohmann::json j = {{"name", "test"}};
    
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
    nlohmann::json j = {{"cluster", "prod"}};  // Missing 'id'
    
    EXPECT_THROW({
        NodeConfigEnhanced::fromJson(j, ctx);
    }, ConfigParseError);
    
    try {
        NodeConfigEnhanced::fromJson(j, ctx);
    } catch (const ConfigParseError& e) {
        std::string msg = e.what();
        EXPECT_NE(msg.find("Required field 'id' is missing"), std::string::npos);
    }
}

TEST_F(ParseErrorTest, NodeConfigEnhancedInvalidType) {
    ParseContext ctx;
    nlohmann::json j = {
        {"id", 123},  // Should be string
        {"cluster", "prod"}
    };
    
    EXPECT_THROW({
        NodeConfigEnhanced::fromJson(j, ctx);
    }, ConfigParseError);
}

TEST_F(ParseErrorTest, NodeConfigEnhancedValidationError) {
    ParseContext ctx;
    nlohmann::json j = {
        {"id", ""},  // Empty ID will fail validation
        {"cluster", "prod"}
    };
    
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
    nlohmann::json valid = {
        {"max_request_size", "10MB"},
        {"max_response_size", "5MB"},
        {"request_timeout", "30s"}
    };
    
    EXPECT_NO_THROW({
        auto config = CapabilitiesConfigEnhanced::fromJson(valid, ctx);
        EXPECT_EQ(config.max_request_size, 10000000);
        EXPECT_EQ(config.request_timeout_ms, 30000);
    });
    
    // Invalid unit
    nlohmann::json invalid = {
        {"max_request_size", "10 MEGABYTES"}  // Invalid unit format
    };
    
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
    nlohmann::json buffer = {
        {"type", "buffer"},
        {"name", "request_buffer"},
        {"config", {{"max_size", "2MB"}}}
    };
    
    auto config = FilterConfigEnhanced::fromJson(buffer, ctx);
    EXPECT_EQ(config.type, "buffer");
    EXPECT_EQ(config.config["max_size"], 2000000);
    
    // Rate limit with duration
    nlohmann::json rate_limit = {
        {"type", "rate_limit"},
        {"name", "api_limiter"},
        {"config", {{"window_duration", "1m"}}}
    };
    
    config = FilterConfigEnhanced::fromJson(rate_limit, ctx);
    EXPECT_EQ(config.config["window_duration"], 60000);
}

TEST_F(ParseErrorTest, ServerConfigEnhancedNestedErrors) {
    ParseContext ctx;
    nlohmann::json j = {
        {"name", "test-server"},
        {"version", "invalid-version"},  // Will fail version parsing
        {"capabilities", {
            {"max_request_size", "invalid"}  // Will fail unit parsing
        }}
    };
    
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
    nlohmann::json j = {
        {"name", "test-server"},
        {"filter_chains", {
            {
                {"name", "chain1"},
                {"filters", {
                    {
                        {"type", "buffer"},
                        // Missing required 'name' field
                    }
                }}
            }
        }}
    };
    
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
    nlohmann::json j = {
        {"key1", "value1"},
        {"key2", 123},
        {"key3", true}
    };
    
    std::string excerpt = getJsonExcerpt(j, 50);
    EXPECT_LE(excerpt.length(), 50);
    EXPECT_NE(excerpt.find("key1"), std::string::npos);
}

// Validation type tests
TEST_F(ParseErrorTest, ValidateJsonType) {
    ParseContext ctx;
    
    nlohmann::json obj = {{"key", "value"}};
    EXPECT_NO_THROW(validateJsonType(obj, nlohmann::json::value_t::object, "test", ctx));
    
    nlohmann::json arr = {1, 2, 3};
    EXPECT_NO_THROW(validateJsonType(arr, nlohmann::json::value_t::array, "test", ctx));
    
    nlohmann::json str = "string";
    EXPECT_THROW(
        validateJsonType(str, nlohmann::json::value_t::object, "test", ctx),
        ConfigParseError);
}

// TryParseConfig tests
TEST_F(ParseErrorTest, TryParseConfigSuccess) {
    nlohmann::json j = {
        {"name", "test-server"},
        {"version", "1.0.0"}
    };
    
    ServerConfigEnhanced config;
    std::string error;
    
    bool success = tryParseConfig(j, config, error);
    EXPECT_TRUE(success);
    EXPECT_TRUE(error.empty());
    EXPECT_EQ(config.name, "test-server");
}

TEST_F(ParseErrorTest, TryParseConfigFailure) {
    nlohmann::json j = {
        {"name", 123},  // Wrong type
        {"version", "1.0.0"}
    };
    
    ServerConfigEnhanced config;
    std::string error;
    
    bool success = tryParseConfig(j, config, error);
    EXPECT_FALSE(success);
    EXPECT_FALSE(error.empty());
    EXPECT_NE(error.find("parse"), std::string::npos);
}

// File loading with diagnostics
TEST_F(ParseErrorTest, LoadEnhancedConfigFileNotFound) {
    EXPECT_THROW({
        loadEnhancedConfig<ServerConfigEnhanced>("/non/existent/file.json");
    }, ConfigParseError);
    
    try {
        loadEnhancedConfig<ServerConfigEnhanced>("/non/existent/file.json");
    } catch (const ConfigParseError& e) {
        std::string msg = e.what();
        EXPECT_NE(msg.find("Cannot open"), std::string::npos);
        EXPECT_NE(msg.find("/non/existent/file.json"), std::string::npos);
    }
}