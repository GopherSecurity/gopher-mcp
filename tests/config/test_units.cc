#include <gtest/gtest.h>

#include "mcp/config/units.h"
#include "mcp/json/json_bridge.h"
#include "mcp/logging/logger_registry.h"
#include "mcp/logging/log_sink.h"

#include <memory>
#include <vector>

// Test logging sink to verify log output
namespace mcp {
namespace logging {

class TestLogSink : public LogSink {
 public:
  void log(const LogMessage& msg) override {
    messages_.push_back(msg);
  }

  void flush() override {}

  bool hasMessage(LogLevel level, const std::string& substr) {
    for (const auto& msg : messages_) {
      if (msg.level == level && msg.message.find(substr) != std::string::npos) {
        return true;
      }
    }
    return false;
  }

  void clear() { messages_.clear(); }

 private:
  std::vector<LogMessage> messages_;
};

}  // namespace logging
}  // namespace mcp

namespace mcp {
namespace config {
namespace test {

using json::JsonValue;

class UnitsTest : public ::testing::Test {
 protected:
  void SetUp() override {
    test_sink_ = std::make_shared<logging::TestLogSink>();
    auto& registry = logging::LoggerRegistry::getInstance();
    auto logger = registry.getLogger("config.units");
    logger->setSink(test_sink_);
    logger->setLevel(logging::LogLevel::Debug);
  }

  std::shared_ptr<logging::TestLogSink> test_sink_;
};

// ============================================================================
// Duration Tests
// ============================================================================

TEST_F(UnitsTest, DurationValidCases) {
  // Table-driven test for valid durations
  struct TestCase {
    std::string input;
    int64_t expected_ms;
    std::string description;
  };

  std::vector<TestCase> cases = {
    {"0ms", 0, "zero milliseconds"},
    {"1ms", 1, "single millisecond"},
    {"100ms", 100, "hundred milliseconds"},
    {"1s", 1000, "one second"},
    {"30s", 30000, "thirty seconds"},
    {"1m", 60000, "one minute"},
    {"5m", 300000, "five minutes"},
    {"1h", 3600000, "one hour"},
    {"24h", 86400000, "twenty-four hours"},
    {"999ms", 999, "max three-digit milliseconds"},
    {"59s", 59000, "fifty-nine seconds"},
    {"59m", 3540000, "fifty-nine minutes"},
    {"168h", 604800000, "one week in hours"},
  };

  for (const auto& tc : cases) {
    test_sink_->clear();
    auto [success, duration] = Duration::parse(tc.input);
    EXPECT_TRUE(success) << "Failed to parse: " << tc.input << " (" << tc.description << ")";
    EXPECT_EQ(duration.count(), tc.expected_ms) 
        << "Wrong value for: " << tc.input << " (" << tc.description << ")";
    
    // Verify debug logging
    EXPECT_TRUE(test_sink_->hasMessage(logging::LogLevel::Debug, "Successfully parsed duration"));
  }
}

TEST_F(UnitsTest, DurationInvalidCases) {
  // Table-driven test for invalid durations
  std::vector<std::string> invalid_cases = {
    "",           // Empty string
    "10",         // Missing unit
    "ms",         // Missing number
    "10 ms",      // Space between number and unit
    "-5s",        // Negative value
    "1.5s",       // Decimal not supported
    "10sec",      // Wrong unit format
    "10S",        // Wrong case
    "10MS",       // Wrong case
    "1d",         // Days not supported
    "abc",        // Non-numeric
    "10sm",       // Invalid unit order
    "10m30s",     // Combined units not supported
    "10x",        // Invalid unit
  };

  for (const auto& input : invalid_cases) {
    test_sink_->clear();
    std::string error_message;
    auto [success, duration] = Duration::parseWithError(input, error_message);
    EXPECT_FALSE(success) << "Should have failed to parse: " << input;
    EXPECT_FALSE(error_message.empty()) << "Should have error message for: " << input;
    
    // Verify error logging
    EXPECT_TRUE(test_sink_->hasMessage(logging::LogLevel::Error, ""));
  }
}

TEST_F(UnitsTest, DurationFromJsonValue) {
  // Test parsing from JsonValue (string)
  {
    JsonValue value("30s");
    auto [success, duration] = Duration::parse(value);
    EXPECT_TRUE(success);
    EXPECT_EQ(duration.count(), 30000);
  }

  // Test parsing from JsonValue (number - assumed milliseconds)
  {
    JsonValue value(5000);
    auto [success, duration] = Duration::parse(value);
    EXPECT_TRUE(success);
    EXPECT_EQ(duration.count(), 5000);
  }

  // Test parsing from JsonValue (double - assumed milliseconds)
  {
    JsonValue value(1500.0);
    auto [success, duration] = Duration::parse(value);
    EXPECT_TRUE(success);
    EXPECT_EQ(duration.count(), 1500);
  }

  // Test invalid type
  {
    JsonValue value = JsonValue::array();
    auto [success, duration] = Duration::parse(value);
    EXPECT_FALSE(success);
  }
}

TEST_F(UnitsTest, DurationToString) {
  EXPECT_EQ(Duration::toString(std::chrono::milliseconds(0)), "0ms");
  EXPECT_EQ(Duration::toString(std::chrono::milliseconds(500)), "500ms");
  EXPECT_EQ(Duration::toString(std::chrono::milliseconds(1000)), "1s");
  EXPECT_EQ(Duration::toString(std::chrono::milliseconds(60000)), "1m");
  EXPECT_EQ(Duration::toString(std::chrono::milliseconds(3600000)), "1h");
  EXPECT_EQ(Duration::toString(std::chrono::milliseconds(7200000)), "2h");
  
  // Non-round values
  EXPECT_EQ(Duration::toString(std::chrono::milliseconds(1500)), "1500ms");
  EXPECT_EQ(Duration::toString(std::chrono::milliseconds(61000)), "61s");
}

TEST_F(UnitsTest, DurationValidation) {
  // Valid formats
  EXPECT_TRUE(Duration::isValid("10ms"));
  EXPECT_TRUE(Duration::isValid("5s"));
  EXPECT_TRUE(Duration::isValid("2m"));
  EXPECT_TRUE(Duration::isValid("1h"));
  
  // Invalid formats
  EXPECT_FALSE(Duration::isValid("10"));
  EXPECT_FALSE(Duration::isValid("10 ms"));
  EXPECT_FALSE(Duration::isValid(""));
  EXPECT_FALSE(Duration::isValid("ms"));
}

// ============================================================================
// Size Tests
// ============================================================================

TEST_F(UnitsTest, SizeValidCases) {
  // Table-driven test for valid sizes
  struct TestCase {
    std::string input;
    size_t expected_bytes;
    std::string description;
  };

  std::vector<TestCase> cases = {
    {"0B", 0, "zero bytes"},
    {"1B", 1, "single byte"},
    {"1024B", 1024, "1024 bytes"},
    {"1KB", 1024, "one kilobyte"},
    {"10KB", 10240, "ten kilobytes"},
    {"1MB", 1048576, "one megabyte"},
    {"100MB", 104857600, "hundred megabytes"},
    {"1GB", 1073741824, "one gigabyte"},
    {"2GB", 2147483648, "two gigabytes"},
    {"512KB", 524288, "512 kilobytes"},
    {"256MB", 268435456, "256 megabytes"},
  };

  for (const auto& tc : cases) {
    test_sink_->clear();
    auto [success, size] = Size::parse(tc.input);
    EXPECT_TRUE(success) << "Failed to parse: " << tc.input << " (" << tc.description << ")";
    EXPECT_EQ(size, tc.expected_bytes) 
        << "Wrong value for: " << tc.input << " (" << tc.description << ")";
    
    // Verify debug logging
    EXPECT_TRUE(test_sink_->hasMessage(logging::LogLevel::Debug, "Successfully parsed size"));
  }
}

TEST_F(UnitsTest, SizeInvalidCases) {
  // Table-driven test for invalid sizes
  std::vector<std::string> invalid_cases = {
    "",           // Empty string
    "10",         // Missing unit
    "KB",         // Missing number
    "10 KB",      // Space between number and unit
    "-5MB",       // Negative value
    "1.5GB",      // Decimal not supported
    "10mb",       // Wrong case
    "10Mb",       // Wrong case
    "10kb",       // Wrong case
    "1TB",        // Terabytes not supported
    "abc",        // Non-numeric
    "10BK",       // Invalid unit order
    "10X",        // Invalid unit
  };

  for (const auto& input : invalid_cases) {
    test_sink_->clear();
    std::string error_message;
    auto [success, size] = Size::parseWithError(input, error_message);
    EXPECT_FALSE(success) << "Should have failed to parse: " << input;
    EXPECT_FALSE(error_message.empty()) << "Should have error message for: " << input;
    
    // Verify error logging
    EXPECT_TRUE(test_sink_->hasMessage(logging::LogLevel::Error, ""));
  }
}

TEST_F(UnitsTest, SizeFromJsonValue) {
  // Test parsing from JsonValue (string)
  {
    JsonValue value("10MB");
    auto [success, size] = Size::parse(value);
    EXPECT_TRUE(success);
    EXPECT_EQ(size, 10485760);
  }

  // Test parsing from JsonValue (number - assumed bytes)
  {
    JsonValue value(1024);
    auto [success, size] = Size::parse(value);
    EXPECT_TRUE(success);
    EXPECT_EQ(size, 1024);
  }

  // Test invalid type
  {
    JsonValue value = JsonValue::object();
    auto [success, size] = Size::parse(value);
    EXPECT_FALSE(success);
  }
}

TEST_F(UnitsTest, SizeToString) {
  EXPECT_EQ(Size::toString(0), "0B");
  EXPECT_EQ(Size::toString(512), "512B");
  EXPECT_EQ(Size::toString(1024), "1KB");
  EXPECT_EQ(Size::toString(10240), "10KB");
  EXPECT_EQ(Size::toString(1048576), "1MB");
  EXPECT_EQ(Size::toString(1073741824), "1GB");
  
  // Non-round values
  EXPECT_EQ(Size::toString(1025), "1025B");
  EXPECT_EQ(Size::toString(1048577), "1048577B");
}

TEST_F(UnitsTest, SizeValidation) {
  // Valid formats
  EXPECT_TRUE(Size::isValid("10B"));
  EXPECT_TRUE(Size::isValid("5KB"));
  EXPECT_TRUE(Size::isValid("2MB"));
  EXPECT_TRUE(Size::isValid("1GB"));
  
  // Invalid formats
  EXPECT_FALSE(Size::isValid("10"));
  EXPECT_FALSE(Size::isValid("10 KB"));
  EXPECT_FALSE(Size::isValid(""));
  EXPECT_FALSE(Size::isValid("KB"));
}

// ============================================================================
// UnitValidator Tests
// ============================================================================

TEST_F(UnitsTest, SuspiciousValueDetection) {
  test_sink_->clear();
  
  // Suspicious duration values
  {
    JsonValue value(5000);  // Large number, likely milliseconds
    EXPECT_TRUE(UnitValidator::isSuspiciousValue(value, "connection_timeout"));
    EXPECT_TRUE(test_sink_->hasMessage(logging::LogLevel::Warning, "Suspicious duration value"));
  }
  
  test_sink_->clear();
  
  // Suspicious size values
  {
    JsonValue value(10485760);  // 10MB in bytes
    EXPECT_TRUE(UnitValidator::isSuspiciousValue(value, "buffer_size"));
    EXPECT_TRUE(test_sink_->hasMessage(logging::LogLevel::Warning, "Suspicious size value"));
  }
  
  test_sink_->clear();
  
  // Values with spaces (incorrect format)
  {
    JsonValue value("10 ms");
    EXPECT_TRUE(UnitValidator::isSuspiciousValue(value, "timeout"));
    EXPECT_TRUE(test_sink_->hasMessage(logging::LogLevel::Warning, "contains spaces"));
  }
  
  // Non-suspicious values
  {
    JsonValue value(100);  // Small number
    EXPECT_FALSE(UnitValidator::isSuspiciousValue(value, "retry_count"));
    
    JsonValue value2("30s");  // Proper format
    EXPECT_FALSE(UnitValidator::isSuspiciousValue(value2, "timeout"));
  }
}

TEST_F(UnitsTest, FriendlyErrorMessages) {
  // Test error message formatting
  std::string error;
  
  // Missing unit
  error = UnitValidator::formatUnitError("1000", "server.timeout", 
                                         "^[0-9]+(ms|s|m|h)$");
  EXPECT_TRUE(error.find("Plain numbers should include units") != std::string::npos);
  
  // Space in value
  error = UnitValidator::formatUnitError("10 ms", "server.timeout", 
                                         "^[0-9]+(ms|s|m|h)$");
  EXPECT_TRUE(error.find("no spaces") != std::string::npos);
  
  // Wrong case
  error = UnitValidator::formatUnitError("10mb", "server.buffer_size", 
                                         "^[0-9]+(B|KB|MB|GB)$");
  EXPECT_TRUE(error.find("must be uppercase") != std::string::npos);
}

TEST_F(UnitsTest, SchemaPatterns) {
  // Verify schema patterns are correct
  std::string duration_pattern = Duration::getSchemaPattern();
  EXPECT_EQ(duration_pattern, "^[0-9]+(ms|s|m|h)$");
  
  std::string size_pattern = Size::getSchemaPattern();
  EXPECT_EQ(size_pattern, "^[0-9]+(B|KB|MB|GB)$");
}

TEST_F(UnitsTest, UnitConversions) {
  // Test duration conversions
  EXPECT_EQ(UnitConversion::toMilliseconds(std::chrono::seconds(5)), 5000);
  EXPECT_EQ(UnitConversion::toSeconds(std::chrono::milliseconds(3000)), 3);
  
  // Test size conversions
  EXPECT_EQ(UnitConversion::KILOBYTE, 1024);
  EXPECT_EQ(UnitConversion::MEGABYTE, 1048576);
  EXPECT_EQ(UnitConversion::GIGABYTE, 1073741824);
  
  EXPECT_DOUBLE_EQ(UnitConversion::toKilobytes(2048), 2.0);
  EXPECT_DOUBLE_EQ(UnitConversion::toMegabytes(2097152), 2.0);
  EXPECT_DOUBLE_EQ(UnitConversion::toGigabytes(2147483648), 2.0);
}

TEST_F(UnitsTest, YamlQuotingGuidance) {
  std::string guidance = UnitValidator::getYamlQuotingGuidance();
  EXPECT_TRUE(guidance.find("YAML Quoting Guidance") != std::string::npos);
  EXPECT_TRUE(guidance.find("timeout: \"30s\"") != std::string::npos);
  EXPECT_TRUE(guidance.find("CORRECT:") != std::string::npos);
  EXPECT_TRUE(guidance.find("INCORRECT:") != std::string::npos);
}

// ============================================================================
// Edge Cases and Error Handling
// ============================================================================

TEST_F(UnitsTest, OverflowHandling) {
  // Test overflow detection for durations
  {
    std::string error;
    auto [success, duration] = Duration::parseWithError("999999999999h", error);
    EXPECT_FALSE(success);
    EXPECT_TRUE(error.find("overflow") != std::string::npos || 
                error.find("too large") != std::string::npos);
  }
  
  // Test overflow detection for sizes  
  {
    std::string error;
    auto [success, size] = Size::parseWithError("999999999999GB", error);
    EXPECT_FALSE(success);
    EXPECT_TRUE(error.find("overflow") != std::string::npos || 
                error.find("too large") != std::string::npos);
  }
}

}  // namespace test
}  // namespace config
}  // namespace mcp