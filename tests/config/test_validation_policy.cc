/**
 * @file test_validation_policy.cc
 * @brief Unit tests for configuration validation policies
 */

#include <gtest/gtest.h>
#include "mcp/config/types_with_validation.h"
#include "mcp/config/validation_policy.h"
#include <sstream>

using namespace mcp::config;

class ValidationPolicyTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Reset to default state before each test
        getDefaultValidationContext().clearUnknownFields();
        getDefaultValidationContext().setUnknownFieldPolicy(UnknownFieldPolicy::WARN);
    }
    
    void TearDown() override {}
};

// Basic validation context tests
TEST_F(ValidationPolicyTest, ValidationContextDefaults) {
    ValidationContext ctx;
    EXPECT_EQ(ctx.getUnknownFieldPolicy(), UnknownFieldPolicy::WARN);
    EXPECT_FALSE(ctx.hasUnknownFields());
    EXPECT_TRUE(ctx.getUnknownFields().empty());
}

TEST_F(ValidationPolicyTest, ValidationContextPolicyChange) {
    ValidationContext ctx;
    
    ctx.setUnknownFieldPolicy(UnknownFieldPolicy::STRICT);
    EXPECT_EQ(ctx.getUnknownFieldPolicy(), UnknownFieldPolicy::STRICT);
    
    ctx.setUnknownFieldPolicy(UnknownFieldPolicy::PERMISSIVE);
    EXPECT_EQ(ctx.getUnknownFieldPolicy(), UnknownFieldPolicy::PERMISSIVE);
}

TEST_F(ValidationPolicyTest, ReportUnknownFieldStrict) {
    ValidationContext ctx(UnknownFieldPolicy::STRICT);
    
    EXPECT_THROW(ctx.reportUnknownField("test", "unknown_field"), std::runtime_error);
    
    // Even in strict mode, we track the field before throwing
    EXPECT_TRUE(ctx.hasUnknownFields());
    EXPECT_EQ(ctx.getUnknownFields().size(), 1);
    EXPECT_EQ(ctx.getUnknownFields()[0], "test.unknown_field");
}

TEST_F(ValidationPolicyTest, ReportUnknownFieldWarn) {
    ValidationContext ctx(UnknownFieldPolicy::WARN);
    
    // Capture warning output
    std::stringstream ss;
    ctx.setWarningHandler([&ss](const std::string& msg) {
        ss << msg;
    });
    
    ctx.reportUnknownField("config", "extra_field");
    
    EXPECT_TRUE(ctx.hasUnknownFields());
    EXPECT_EQ(ctx.getUnknownFields().size(), 1);
    EXPECT_EQ(ctx.getUnknownFields()[0], "config.extra_field");
    
    // Check warning was generated
    std::string warning = ss.str();
    EXPECT_NE(warning.find("Unknown configuration field: config.extra_field"), std::string::npos);
}

TEST_F(ValidationPolicyTest, ReportUnknownFieldPermissive) {
    ValidationContext ctx(UnknownFieldPolicy::PERMISSIVE);
    
    // Should not throw or warn
    EXPECT_NO_THROW(ctx.reportUnknownField("test", "unknown"));
    
    // But still tracks the field
    EXPECT_TRUE(ctx.hasUnknownFields());
    EXPECT_EQ(ctx.getUnknownFields().size(), 1);
}

// NodeConfig validation tests
TEST_F(ValidationPolicyTest, NodeConfigValidFields) {
    nlohmann::json j = {
        {"id", "test-node"},
        {"cluster", "production"},
        {"region", "us-west"},
        {"zone", "us-west-2a"},
        {"metadata", {{"key", "value"}}}
    };
    
    ValidationContext ctx(UnknownFieldPolicy::STRICT);
    EXPECT_NO_THROW(NodeConfigWithValidation::fromJson(j, ctx));
    EXPECT_FALSE(ctx.hasUnknownFields());
}

TEST_F(ValidationPolicyTest, NodeConfigUnknownFieldStrict) {
    nlohmann::json j = {
        {"id", "test-node"},
        {"cluster", "production"},
        {"unknown_field", "value"}
    };
    
    ValidationContext ctx(UnknownFieldPolicy::STRICT);
    EXPECT_THROW(NodeConfigWithValidation::fromJson(j, ctx), std::runtime_error);
}

TEST_F(ValidationPolicyTest, NodeConfigUnknownFieldWarn) {
    nlohmann::json j = {
        {"id", "test-node"},
        {"cluster", "production"},
        {"extra_field", "value"}
    };
    
    ValidationContext ctx(UnknownFieldPolicy::WARN);
    std::stringstream ss;
    ctx.setWarningHandler([&ss](const std::string& msg) {
        ss << msg << "\n";
    });
    
    auto config = NodeConfigWithValidation::fromJson(j, ctx);
    
    EXPECT_TRUE(ctx.hasUnknownFields());
    EXPECT_EQ(ctx.getUnknownFields().size(), 1);
    EXPECT_EQ(ctx.getUnknownFields()[0], "node.extra_field");
    
    // Config should still be valid and usable
    EXPECT_EQ(config.id, "test-node");
    EXPECT_EQ(config.cluster, "production");
}

TEST_F(ValidationPolicyTest, NodeConfigUnknownFieldPermissive) {
    nlohmann::json j = {
        {"id", "test-node"},
        {"cluster", "production"},
        {"random_field", "ignored"},
        {"another_unknown", 123}
    };
    
    ValidationContext ctx(UnknownFieldPolicy::PERMISSIVE);
    
    auto config = NodeConfigWithValidation::fromJson(j, ctx);
    
    // Fields are tracked but no error/warning
    EXPECT_TRUE(ctx.hasUnknownFields());
    EXPECT_EQ(ctx.getUnknownFields().size(), 2);
    
    // Config is valid
    EXPECT_EQ(config.id, "test-node");
    EXPECT_EQ(config.cluster, "production");
}

// AdminConfig validation tests
TEST_F(ValidationPolicyTest, AdminConfigValidFields) {
    nlohmann::json j = {
        {"address", "0.0.0.0"},
        {"port", 9901},
        {"allowed_ips", {"192.168.1.0/24"}},
        {"enabled", true},
        {"path_prefix", "/admin"},
        {"enable_cors", true},
        {"cors_origins", {"*"}}
    };
    
    ValidationContext ctx(UnknownFieldPolicy::STRICT);
    EXPECT_NO_THROW(AdminConfigWithValidation::fromJson(j, ctx));
    EXPECT_FALSE(ctx.hasUnknownFields());
}

TEST_F(ValidationPolicyTest, AdminConfigUnknownFields) {
    nlohmann::json j = {
        {"address", "127.0.0.1"},
        {"port", 9901},
        {"auth_enabled", true},  // Unknown field
        {"ssl_cert", "/path/to/cert"}  // Unknown field
    };
    
    ValidationContext ctx(UnknownFieldPolicy::WARN);
    auto config = AdminConfigWithValidation::fromJson(j, ctx);
    
    EXPECT_TRUE(ctx.hasUnknownFields());
    EXPECT_EQ(ctx.getUnknownFields().size(), 2);
    EXPECT_EQ(ctx.getUnknownFields()[0], "admin.auth_enabled");
    EXPECT_EQ(ctx.getUnknownFields()[1], "admin.ssl_cert");
}

// FilterConfig validation tests
TEST_F(ValidationPolicyTest, FilterConfigValidFields) {
    nlohmann::json j = {
        {"type", "buffer"},
        {"name", "request_buffer"},
        {"config", {{"max_size", 1048576}}},
        {"enabled", true}
    };
    
    ValidationContext ctx(UnknownFieldPolicy::STRICT);
    EXPECT_NO_THROW(FilterConfigWithValidation::fromJson(j, ctx));
    EXPECT_FALSE(ctx.hasUnknownFields());
}

TEST_F(ValidationPolicyTest, FilterConfigCustomConfigFields) {
    // Filter-specific config fields should be allowed
    nlohmann::json j = {
        {"type", "rate_limit"},
        {"name", "api_limiter"},
        {"config", {
            {"requests_per_second", 100},
            {"burst_size", 200},
            {"custom_field", "allowed"}  // Should be allowed in config
        }},
        {"enabled", true}
    };
    
    ValidationContext ctx(UnknownFieldPolicy::STRICT);
    EXPECT_NO_THROW(FilterConfigWithValidation::fromJson(j, ctx));
    EXPECT_FALSE(ctx.hasUnknownFields());
}

TEST_F(ValidationPolicyTest, FilterConfigUnknownTopLevel) {
    nlohmann::json j = {
        {"type", "buffer"},
        {"name", "test"},
        {"priority", 10}  // Unknown top-level field
    };
    
    ValidationContext ctx(UnknownFieldPolicy::STRICT);
    EXPECT_THROW(FilterConfigWithValidation::fromJson(j, ctx), std::runtime_error);
}

// ServerConfig validation tests
TEST_F(ValidationPolicyTest, ServerConfigComplexValidation) {
    nlohmann::json j = {
        {"name", "test-server"},
        {"version", "1.0.0"},
        {"max_sessions", 100},
        {"capabilities", {
            {"features", {"tools"}},
            {"max_request_size", "10MB"}
        }},
        {"transports", {
            {
                {"type", "tcp"},
                {"port", 3333}
            }
        }},
        {"filter_chains", {
            {
                {"name", "default"},
                {"filters", {}}
            }
        }}
    };
    
    ValidationContext ctx(UnknownFieldPolicy::STRICT);
    EXPECT_NO_THROW(ServerConfigWithValidation::fromJson(j, ctx));
    EXPECT_FALSE(ctx.hasUnknownFields());
}

TEST_F(ValidationPolicyTest, ServerConfigNestedUnknownFields) {
    nlohmann::json j = {
        {"name", "test-server"},
        {"version", "1.0.0"},
        {"custom_field", "unknown"},  // Unknown at server level
        {"capabilities", {
            {"features", {"tools"}},
            {"unknown_cap", true}  // Unknown in capabilities
        }},
        {"transports", {
            {
                {"type", "tcp"},
                {"port", 3333},
                {"unknown_transport", "field"}  // Unknown in transport
            }
        }}
    };
    
    ValidationContext ctx(UnknownFieldPolicy::WARN);
    auto config = ServerConfigWithValidation::fromJson(j, ctx);
    
    EXPECT_TRUE(ctx.hasUnknownFields());
    EXPECT_GE(ctx.getUnknownFields().size(), 3);
    
    // Check that unknown fields were detected at different levels
    auto& fields = ctx.getUnknownFields();
    bool found_server = false;
    bool found_capabilities = false;
    bool found_transport = false;
    
    for (const auto& field : fields) {
        if (field == "server.custom_field") found_server = true;
        if (field == "capabilities.unknown_cap") found_capabilities = true;
        if (field.find("transports") != std::string::npos && 
            field.find("unknown_transport") != std::string::npos) {
            found_transport = true;
        }
    }
    
    EXPECT_TRUE(found_server);
    EXPECT_TRUE(found_capabilities);
    EXPECT_TRUE(found_transport);
}

// Integration tests
TEST_F(ValidationPolicyTest, LoadConfigWithValidation) {
    nlohmann::json j = {
        {"name", "test"},
        {"version", "1.0.0"},
        {"unknown_field", "test"}
    };
    
    // Test with different policies
    EXPECT_THROW(
        loadConfigWithValidation<ServerConfigWithValidation>(j, UnknownFieldPolicy::STRICT),
        std::runtime_error);
    
    EXPECT_NO_THROW(
        loadConfigWithValidation<ServerConfigWithValidation>(j, UnknownFieldPolicy::WARN));
    
    EXPECT_NO_THROW(
        loadConfigWithValidation<ServerConfigWithValidation>(j, UnknownFieldPolicy::PERMISSIVE));
}

TEST_F(ValidationPolicyTest, GlobalPolicyChange) {
    setGlobalUnknownFieldPolicy(UnknownFieldPolicy::STRICT);
    
    nlohmann::json j = {
        {"id", "test"},
        {"cluster", "default"},
        {"unknown", "field"}
    };
    
    // Should use global strict policy
    EXPECT_THROW(NodeConfigWithValidation::fromJson(j), std::runtime_error);
    
    // Change global policy
    setGlobalUnknownFieldPolicy(UnknownFieldPolicy::PERMISSIVE);
    
    // Now should work
    EXPECT_NO_THROW(NodeConfigWithValidation::fromJson(j));
}

TEST_F(ValidationPolicyTest, TrackMultipleUnknownFields) {
    ValidationContext ctx(UnknownFieldPolicy::PERMISSIVE);
    
    nlohmann::json j = {
        {"id", "test"},
        {"cluster", "default"},
        {"field1", "unknown"},
        {"field2", "unknown"},
        {"field3", "unknown"}
    };
    
    NodeConfigWithValidation::fromJson(j, ctx);
    
    EXPECT_TRUE(ctx.hasUnknownFields());
    EXPECT_EQ(ctx.getUnknownFields().size(), 3);
    
    // Clear and verify
    ctx.clearUnknownFields();
    EXPECT_FALSE(ctx.hasUnknownFields());
    EXPECT_TRUE(ctx.getUnknownFields().empty());
}