#pragma once

#include <chrono>
#include <regex>
#include <string>
#include <utility>

#include "mcp/json/json_bridge.h"

namespace mcp {
namespace config {

// Duration parsing and validation
// Supports formats: 10ms, 5s, 2m, 1h
class Duration {
 public:
  // Parse from string (e.g., "10ms", "5s", "2m", "1h")
  static std::pair<bool, std::chrono::milliseconds> parse(const std::string& str);
  
  // Parse from JsonValue (string or number)
  static std::pair<bool, std::chrono::milliseconds> parse(const json::JsonValue& value);
  
  // Convert duration to string representation
  static std::string toString(std::chrono::milliseconds duration);
  
  // Get regex pattern for schema validation
  static std::string getSchemaPattern() {
    return "^[0-9]+(ms|s|m|h)$";
  }
  
  // Validate without parsing (for quick checks)
  static bool isValid(const std::string& str);
  
  // Parse with detailed error message
  static std::pair<bool, std::chrono::milliseconds> parseWithError(
      const std::string& str, std::string& error_message);
};

// Size/memory parsing and validation
// Supports formats: 1024B, 10KB, 5MB, 2GB
class Size {
 public:
  // Parse from string (e.g., "1024B", "10KB", "5MB", "2GB")
  static std::pair<bool, size_t> parse(const std::string& str);
  
  // Parse from JsonValue (string or number)
  static std::pair<bool, size_t> parse(const json::JsonValue& value);
  
  // Convert size to human-readable string
  static std::string toString(size_t bytes);
  
  // Get regex pattern for schema validation
  static std::string getSchemaPattern() {
    return "^[0-9]+(B|KB|MB|GB)$";
  }
  
  // Validate without parsing (for quick checks)
  static bool isValid(const std::string& str);
  
  // Parse with detailed error message
  static std::pair<bool, size_t> parseWithError(
      const std::string& str, std::string& error_message);
};

// Unit validation utilities
class UnitValidator {
 public:
  // Check if value looks like it should have units but doesn't
  static bool isSuspiciousValue(const json::JsonValue& value, const std::string& field_name);
  
  // Get YAML quoting guidance
  static std::string getYamlQuotingGuidance() {
    return R"(
YAML Quoting Guidance for Units:
---------------------------------
To avoid YAML implicit typing issues, always quote unit values:

CORRECT:
  timeout: "30s"       # Quoted string
  memory: "512MB"      # Quoted string
  
INCORRECT:
  timeout: 30s         # May be parsed as plain scalar
  memory: 512MB        # May cause parsing issues

For numeric values without units:
  timeout_ms: 30000    # Milliseconds as number
  memory_bytes: 536870912  # Bytes as number
)";
  }
  
  // Create friendly error message for invalid unit
  static std::string formatUnitError(const std::string& value, 
                                     const std::string& field_path,
                                     const std::string& expected_format);
};

// Exception for unit parse errors
class UnitParseError : public std::runtime_error {
 public:
  explicit UnitParseError(const std::string& message) : std::runtime_error(message) {}
};

// Helper functions for JSON parsing
template<typename T>
T parseJsonDuration(const json::JsonValue& value, const std::string& field_name) {
  auto result = Duration::parse(value);
  if (!result.first) {
    throw UnitParseError("Invalid duration format for field '" + field_name + "'");
  }
  return static_cast<T>(result.second.count());
}

template<typename T>
T parseJsonSize(const json::JsonValue& value, const std::string& field_name) {
  auto result = Size::parse(value);
  if (!result.first) {
    throw UnitParseError("Invalid size format for field '" + field_name + "'");
  }
  return static_cast<T>(result.second);
}


// Common unit conversion utilities
class UnitConversion {
 public:
  // Duration conversions
  static int64_t toMilliseconds(const std::chrono::seconds& sec) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(sec).count();
  }
  
  static int64_t toSeconds(const std::chrono::milliseconds& ms) {
    return std::chrono::duration_cast<std::chrono::seconds>(ms).count();
  }
  
  // Size conversions
  static constexpr size_t BYTE = 1;
  // Use binary multiples (KiB, MiB, GiB) while keeping legacy names for compatibility
  static constexpr size_t KILOBYTE = 1024;
  static constexpr size_t MEGABYTE = KILOBYTE * 1024;
  static constexpr size_t GIGABYTE = MEGABYTE * 1024;
  
  static double toKilobytes(size_t bytes) {
    return static_cast<double>(bytes) / KILOBYTE;
  }
  
  static double toMegabytes(size_t bytes) {
    return static_cast<double>(bytes) / MEGABYTE;
  }
  
  static double toGigabytes(size_t bytes) {
    return static_cast<double>(bytes) / GIGABYTE;
  }
};

}  // namespace config
}  // namespace mcp
