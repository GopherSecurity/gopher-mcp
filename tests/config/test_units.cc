/**
 * @file test_units.cc
 * @brief Unit tests for configuration unit parsing
 */

#include <gtest/gtest.h>
#include "mcp/config/units.h"
#include <nlohmann/json.hpp>

using namespace mcp::config;

class UnitParsingTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

// Duration parsing tests
TEST_F(UnitParsingTest, ParseDurationPlainNumber) {
    EXPECT_EQ(parseDuration("1000"), 1000);
    EXPECT_EQ(parseDuration("0"), 0);
    EXPECT_EQ(parseDuration("42"), 42);
    EXPECT_EQ(parseDuration("999999"), 999999);
}

TEST_F(UnitParsingTest, ParseDurationMilliseconds) {
    EXPECT_EQ(parseDuration("100ms"), 100);
    EXPECT_EQ(parseDuration("100MS"), 100);
    EXPECT_EQ(parseDuration("1500ms"), 1500);
    EXPECT_EQ(parseDuration("0ms"), 0);
    EXPECT_EQ(parseDuration("100 ms"), 100);  // With space
    EXPECT_EQ(parseDuration("100milliseconds"), 100);
}

TEST_F(UnitParsingTest, ParseDurationSeconds) {
    EXPECT_EQ(parseDuration("1s"), 1000);
    EXPECT_EQ(parseDuration("30s"), 30000);
    EXPECT_EQ(parseDuration("30S"), 30000);
    EXPECT_EQ(parseDuration("0.5s"), 500);
    EXPECT_EQ(parseDuration("1.5s"), 1500);
    EXPECT_EQ(parseDuration("10 s"), 10000);  // With space
    EXPECT_EQ(parseDuration("5seconds"), 5000);
    EXPECT_EQ(parseDuration("1second"), 1000);
}

TEST_F(UnitParsingTest, ParseDurationMinutes) {
    EXPECT_EQ(parseDuration("1m"), 60000);
    EXPECT_EQ(parseDuration("5m"), 300000);
    EXPECT_EQ(parseDuration("5M"), 300000);
    EXPECT_EQ(parseDuration("0.5m"), 30000);
    EXPECT_EQ(parseDuration("1.5m"), 90000);
    EXPECT_EQ(parseDuration("2 m"), 120000);  // With space
    EXPECT_EQ(parseDuration("3minutes"), 180000);
    EXPECT_EQ(parseDuration("1minute"), 60000);
}

TEST_F(UnitParsingTest, ParseDurationHours) {
    EXPECT_EQ(parseDuration("1h"), 3600000);
    EXPECT_EQ(parseDuration("2h"), 7200000);
    EXPECT_EQ(parseDuration("2H"), 7200000);
    EXPECT_EQ(parseDuration("0.5h"), 1800000);
    EXPECT_EQ(parseDuration("1.5h"), 5400000);
    EXPECT_EQ(parseDuration("24h"), 86400000);
    EXPECT_EQ(parseDuration("1hour"), 3600000);
    EXPECT_EQ(parseDuration("2hours"), 7200000);
}

TEST_F(UnitParsingTest, ParseDurationDays) {
    EXPECT_EQ(parseDuration("1d"), 86400000);
    EXPECT_EQ(parseDuration("7d"), 604800000);
    EXPECT_EQ(parseDuration("1D"), 86400000);
    EXPECT_EQ(parseDuration("0.5d"), 43200000);
    EXPECT_EQ(parseDuration("1day"), 86400000);
    EXPECT_EQ(parseDuration("2days"), 172800000);
}

TEST_F(UnitParsingTest, ParseDurationInvalid) {
    EXPECT_THROW(parseDuration(""), UnitParseError);
    EXPECT_THROW(parseDuration("abc"), UnitParseError);
    EXPECT_THROW(parseDuration("10x"), UnitParseError);
    EXPECT_THROW(parseDuration("s10"), UnitParseError);
    EXPECT_THROW(parseDuration("-5s"), UnitParseError);
}

// Size parsing tests
TEST_F(UnitParsingTest, ParseSizePlainNumber) {
    EXPECT_EQ(parseSize("1024"), 1024);
    EXPECT_EQ(parseSize("0"), 0);
    EXPECT_EQ(parseSize("42"), 42);
    EXPECT_EQ(parseSize("1000000"), 1000000);
}

TEST_F(UnitParsingTest, ParseSizeBytes) {
    EXPECT_EQ(parseSize("100B"), 100);
    EXPECT_EQ(parseSize("100b"), 100);
    EXPECT_EQ(parseSize("1500B"), 1500);
    EXPECT_EQ(parseSize("0B"), 0);
    EXPECT_EQ(parseSize("100 B"), 100);  // With space
    EXPECT_EQ(parseSize("100bytes"), 100);
    EXPECT_EQ(parseSize("1byte"), 1);
}

TEST_F(UnitParsingTest, ParseSizeKilobytes) {
    EXPECT_EQ(parseSize("1KB"), 1000);
    EXPECT_EQ(parseSize("1kb"), 1000);
    EXPECT_EQ(parseSize("1K"), 1000);
    EXPECT_EQ(parseSize("1k"), 1000);
    EXPECT_EQ(parseSize("10KB"), 10000);
    EXPECT_EQ(parseSize("1.5KB"), 1500);
    EXPECT_EQ(parseSize("0.5KB"), 500);
    
    // Binary kilobytes
    EXPECT_EQ(parseSize("1KiB"), 1024);
    EXPECT_EQ(parseSize("1kib"), 1024);
    EXPECT_EQ(parseSize("10KiB"), 10240);
}

TEST_F(UnitParsingTest, ParseSizeMegabytes) {
    EXPECT_EQ(parseSize("1MB"), 1000000);
    EXPECT_EQ(parseSize("1mb"), 1000000);
    EXPECT_EQ(parseSize("1M"), 1000000);
    EXPECT_EQ(parseSize("1m"), 1000000);
    EXPECT_EQ(parseSize("10MB"), 10000000);
    EXPECT_EQ(parseSize("1.5MB"), 1500000);
    EXPECT_EQ(parseSize("0.5MB"), 500000);
    
    // Binary megabytes
    EXPECT_EQ(parseSize("1MiB"), 1048576);
    EXPECT_EQ(parseSize("1mib"), 1048576);
    EXPECT_EQ(parseSize("10MiB"), 10485760);
}

TEST_F(UnitParsingTest, ParseSizeGigabytes) {
    EXPECT_EQ(parseSize("1GB"), 1000000000);
    EXPECT_EQ(parseSize("1gb"), 1000000000);
    EXPECT_EQ(parseSize("1G"), 1000000000);
    EXPECT_EQ(parseSize("1g"), 1000000000);
    EXPECT_EQ(parseSize("2GB"), 2000000000);
    EXPECT_EQ(parseSize("1.5GB"), 1500000000);
    
    // Binary gigabytes
    EXPECT_EQ(parseSize("1GiB"), 1073741824);
    EXPECT_EQ(parseSize("1gib"), 1073741824);
    EXPECT_EQ(parseSize("2GiB"), 2147483648);
}

TEST_F(UnitParsingTest, ParseSizeTerabytes) {
    EXPECT_EQ(parseSize("1TB"), 1000000000000);
    EXPECT_EQ(parseSize("1tb"), 1000000000000);
    EXPECT_EQ(parseSize("1T"), 1000000000000);
    EXPECT_EQ(parseSize("1t"), 1000000000000);
    
    // Binary terabytes
    EXPECT_EQ(parseSize("1TiB"), 1099511627776);
    EXPECT_EQ(parseSize("1tib"), 1099511627776);
}

TEST_F(UnitParsingTest, ParseSizeInvalid) {
    EXPECT_THROW(parseSize(""), UnitParseError);
    EXPECT_THROW(parseSize("abc"), UnitParseError);
    EXPECT_THROW(parseSize("10X"), UnitParseError);
    EXPECT_THROW(parseSize("MB10"), UnitParseError);
    EXPECT_THROW(parseSize("-5MB"), UnitParseError);
}

// JSON parsing tests
TEST_F(UnitParsingTest, ParseJsonDurationNumber) {
    nlohmann::json j = 5000;
    EXPECT_EQ(parseJsonDuration<uint32_t>(j, "timeout"), 5000);
}

TEST_F(UnitParsingTest, ParseJsonDurationString) {
    nlohmann::json j = "30s";
    EXPECT_EQ(parseJsonDuration<uint32_t>(j, "timeout"), 30000);
    
    j = "5m";
    EXPECT_EQ(parseJsonDuration<uint32_t>(j, "timeout"), 300000);
    
    j = "1.5h";
    EXPECT_EQ(parseJsonDuration<uint32_t>(j, "timeout"), 5400000);
}

TEST_F(UnitParsingTest, ParseJsonSizeNumber) {
    nlohmann::json j = 1048576;
    EXPECT_EQ(parseJsonSize<size_t>(j, "max_size"), 1048576);
}

TEST_F(UnitParsingTest, ParseJsonSizeString) {
    nlohmann::json j = "10MB";
    EXPECT_EQ(parseJsonSize<size_t>(j, "max_size"), 10000000);
    
    j = "1GB";
    EXPECT_EQ(parseJsonSize<size_t>(j, "max_size"), 1000000000);
    
    j = "512KB";
    EXPECT_EQ(parseJsonSize<size_t>(j, "max_size"), 512000);
    
    j = "1.5MiB";
    EXPECT_EQ(parseJsonSize<size_t>(j, "max_size"), 1572864);
}

TEST_F(UnitParsingTest, ParseJsonInvalidType) {
    nlohmann::json j = true;  // Boolean
    EXPECT_THROW(parseJsonDuration<uint32_t>(j, "timeout"), UnitParseError);
    EXPECT_THROW(parseJsonSize<size_t>(j, "max_size"), UnitParseError);
    
    j = nlohmann::json::array();  // Array
    EXPECT_THROW(parseJsonDuration<uint32_t>(j, "timeout"), UnitParseError);
    EXPECT_THROW(parseJsonSize<size_t>(j, "max_size"), UnitParseError);
}

// Formatting tests
TEST_F(UnitParsingTest, FormatDuration) {
    EXPECT_EQ(formatDuration(0), "0ms");
    EXPECT_EQ(formatDuration(500), "500ms");
    EXPECT_EQ(formatDuration(1000), "1s");
    EXPECT_EQ(formatDuration(1500), "1.5s");
    EXPECT_EQ(formatDuration(60000), "1m");
    EXPECT_EQ(formatDuration(90000), "1.5m");
    EXPECT_EQ(formatDuration(3600000), "1h");
    EXPECT_EQ(formatDuration(5400000), "1.5h");
    EXPECT_EQ(formatDuration(86400000), "1d");
}

TEST_F(UnitParsingTest, FormatSize) {
    EXPECT_EQ(formatSize(0), "0B");
    EXPECT_EQ(formatSize(500), "500B");
    EXPECT_EQ(formatSize(1000), "1KB");
    EXPECT_EQ(formatSize(1500), "1.5KB");
    EXPECT_EQ(formatSize(1000000), "1MB");
    EXPECT_EQ(formatSize(1500000), "1.5MB");
    EXPECT_EQ(formatSize(1000000000), "1GB");
    EXPECT_EQ(formatSize(1500000000), "1.5GB");
}

// Integration test with config types
TEST_F(UnitParsingTest, ConfigIntegration) {
    nlohmann::json config = {
        {"capabilities", {
            {"max_request_size", "10MB"},
            {"max_response_size", "5MB"},
            {"request_timeout", "30s"}
        }},
        {"session_timeout", "5m"}
    };
    
    // These values should parse correctly
    auto max_req = parseJsonSize<size_t>(config["capabilities"]["max_request_size"], "max_request_size");
    EXPECT_EQ(max_req, 10000000);
    
    auto max_resp = parseJsonSize<size_t>(config["capabilities"]["max_response_size"], "max_response_size");
    EXPECT_EQ(max_resp, 5000000);
    
    auto req_timeout = parseJsonDuration<uint32_t>(config["capabilities"]["request_timeout"], "request_timeout");
    EXPECT_EQ(req_timeout, 30000);
    
    auto session_timeout = parseJsonDuration<uint32_t>(config["session_timeout"], "session_timeout");
    EXPECT_EQ(session_timeout, 300000);
}

TEST_F(UnitParsingTest, MixedFormats) {
    // Test that we can handle both numeric and string formats in the same config
    nlohmann::json config = {
        {"timeout1", 5000},      // Plain number
        {"timeout2", "5s"},      // String with unit
        {"size1", 1048576},      // Plain number  
        {"size2", "1MB"}         // String with unit
    };
    
    EXPECT_EQ(parseJsonDuration<uint32_t>(config["timeout1"], "timeout1"), 5000);
    EXPECT_EQ(parseJsonDuration<uint32_t>(config["timeout2"], "timeout2"), 5000);
    EXPECT_EQ(parseJsonSize<size_t>(config["size1"], "size1"), 1048576);
    EXPECT_EQ(parseJsonSize<size_t>(config["size2"], "size2"), 1000000);
}