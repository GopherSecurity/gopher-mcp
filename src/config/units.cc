#define GOPHER_LOG_COMPONENT "config.units"

#include "mcp/config/units.h"

#include <algorithm>
#include <cctype>
#include <limits>
#include <sstream>

#include "mcp/logging/log_macros.h"

namespace mcp {
namespace config {

// Duration implementation
std::pair<bool, std::chrono::milliseconds> Duration::parse(
    const std::string& str) {
  std::string error;
  return parseWithError(str, error);
}

std::pair<bool, std::chrono::milliseconds> Duration::parse(
    const json::JsonValue& value) {
  if (value.isString()) {
    return parse(value.getString());
  }

  if (value.isInteger() || value.isFloat()) {
    // Assume milliseconds if no unit specified
    int64_t ms = value.isInteger() ? value.getInt64()
                                   : static_cast<int64_t>(value.getFloat());
    if (ms < 0) {
      GOPHER_LOG(Error, "Duration values must be non-negative: {}", ms);
      return {false, std::chrono::milliseconds(0)};
    }

    GOPHER_LOG(Debug, "Parsing numeric value as milliseconds: {}", ms);
    return {true, std::chrono::milliseconds(ms)};
  }

  GOPHER_LOG(Error, "Invalid duration value type: expected string or number");
  return {false, std::chrono::milliseconds(0)};
}

std::string Duration::toString(std::chrono::milliseconds duration) {
  auto ms = duration.count();

  if (ms == 0)
    return "0ms";
  if (ms % (60 * 60 * 1000) == 0)
    return std::to_string(ms / (60 * 60 * 1000)) + "h";
  if (ms % (60 * 1000) == 0)
    return std::to_string(ms / (60 * 1000)) + "m";
  if (ms % 1000 == 0)
    return std::to_string(ms / 1000) + "s";
  return std::to_string(ms) + "ms";
}

bool Duration::isValid(const std::string& str) {
  static const std::regex pattern("^[0-9]+(ms|s|m|h)$");
  return std::regex_match(str, pattern);
}

std::pair<bool, std::chrono::milliseconds> Duration::parseWithError(
    const std::string& str, std::string& error_message) {
  static const std::regex pattern("^([0-9]+)(ms|s|m|h)$");
  std::smatch match;

  if (!std::regex_match(str, match, pattern)) {
    error_message = "Invalid duration format '" + str +
                    "'. Expected format: <number><unit> where unit is ms, s, "
                    "m, or h (e.g., '30s', '5m', '1h')";
    GOPHER_LOG(Error, "{}", error_message);
    return {false, std::chrono::milliseconds(0)};
  }

  try {
    int64_t value = std::stoll(match[1].str());
    std::string unit = match[2].str();

    if (value < 0) {
      error_message = "Duration value cannot be negative: " + str;
      GOPHER_LOG(Error, "{}", error_message);
      return {false, std::chrono::milliseconds(0)};
    }

    const int64_t ms_per_second = 1000;
    const int64_t ms_per_minute = 60 * ms_per_second;
    const int64_t ms_per_hour = 60 * ms_per_minute;
    const int64_t max_count = std::numeric_limits<int64_t>::max();
    const int64_t ns_per_ms = 1'000'000;

    int64_t multiplier = 1;
    if (unit == "ms") {
      multiplier = 1;
    } else if (unit == "s") {
      multiplier = ms_per_second;
    } else if (unit == "m") {
      multiplier = ms_per_minute;
    } else if (unit == "h") {
      multiplier = ms_per_hour;
    }

    if (value > max_count / multiplier) {
      error_message = "Duration value too large (overflow): " + str;
      GOPHER_LOG(Error, "{}", error_message);
      return {false, std::chrono::milliseconds(0)};
    }

    const int64_t ns_multiplier = multiplier * ns_per_ms;
    if (value > max_count / ns_multiplier) {
      error_message = "Duration value too large (overflow): " + str;
      GOPHER_LOG(Error, "{}", error_message);
      return {false, std::chrono::milliseconds(0)};
    }

    auto total_ms = value * multiplier;
    std::chrono::milliseconds result(total_ms);

    GOPHER_LOG(Debug, "Successfully parsed duration '{}' to {} milliseconds",
               str, result.count());
    return {true, result};

  } catch (const std::exception& e) {
    error_message = "Failed to parse duration value: " + std::string(e.what());
    GOPHER_LOG(Error, "{}", error_message);
    return {false, std::chrono::milliseconds(0)};
  }
}

// Size implementation
std::pair<bool, size_t> Size::parse(const std::string& str) {
  std::string error;
  return parseWithError(str, error);
}

std::pair<bool, size_t> Size::parse(const json::JsonValue& value) {
  if (value.isString()) {
    return parse(value.getString());
  }

  if (value.isInteger() || value.isFloat()) {
    // Assume bytes if no unit specified
    long double raw = value.isInteger()
                          ? static_cast<long double>(value.getInt64())
                          : static_cast<long double>(value.getFloat());
    if (raw < 0) {
      GOPHER_LOG(Error, "Size values must be non-negative: {}", raw);
      return {false, 0};
    }

    const long double max_size =
        static_cast<long double>(std::numeric_limits<size_t>::max());
    if (raw > max_size) {
      GOPHER_LOG(Error, "Size value too large (overflow): {}", raw);
      return {false, 0};
    }

    size_t bytes = static_cast<size_t>(raw);
    GOPHER_LOG(Debug, "Parsing numeric value as bytes: {}", bytes);
    return {true, bytes};
  }

  GOPHER_LOG(Error, "Invalid size value type: expected string or number");
  return {false, 0};
}

std::string Size::toString(size_t bytes) {
  if (bytes == 0)
    return "0B";

  if (bytes >= UnitConversion::GIGABYTE &&
      bytes % UnitConversion::GIGABYTE == 0) {
    return std::to_string(bytes / UnitConversion::GIGABYTE) + "GB";
  }
  if (bytes >= UnitConversion::MEGABYTE &&
      bytes % UnitConversion::MEGABYTE == 0) {
    return std::to_string(bytes / UnitConversion::MEGABYTE) + "MB";
  }
  if (bytes >= UnitConversion::KILOBYTE &&
      bytes % UnitConversion::KILOBYTE == 0) {
    return std::to_string(bytes / UnitConversion::KILOBYTE) + "KB";
  }
  return std::to_string(bytes) + "B";
}

bool Size::isValid(const std::string& str) {
  static const std::regex pattern("^[0-9]+(B|KB|MB|GB)$");
  return std::regex_match(str, pattern);
}

std::pair<bool, size_t> Size::parseWithError(const std::string& str,
                                             std::string& error_message) {
  static const std::regex pattern("^([0-9]+)(B|KB|MB|GB)$");
  std::smatch match;

  if (!std::regex_match(str, match, pattern)) {
    error_message = "Invalid size format '" + str +
                    "'. Expected format: <number><unit> where unit is B, KB, "
                    "MB, or GB (e.g., '1024B', '10MB', '2GB')";
    GOPHER_LOG(Error, "{}", error_message);
    return {false, 0};
  }

  try {
    uint64_t value = std::stoull(match[1].str());
    std::string unit = match[2].str();

    size_t result = 0;
    const uint64_t max_size =
        static_cast<uint64_t>(std::numeric_limits<size_t>::max());

    if (unit == "B") {
      if (value > max_size) {
        error_message = "Size value too large (overflow): " + str;
        GOPHER_LOG(Error, "{}", error_message);
        return {false, 0};
      }
      result = static_cast<size_t>(value);
    } else if (unit == "KB") {
      const uint64_t multiplier =
          static_cast<uint64_t>(UnitConversion::KILOBYTE);
      if (value > max_size / multiplier) {
        error_message = "Size value too large (overflow): " + str;
        GOPHER_LOG(Error, "{}", error_message);
        return {false, 0};
      }
      result = static_cast<size_t>(value * multiplier);
    } else if (unit == "MB") {
      const uint64_t multiplier =
          static_cast<uint64_t>(UnitConversion::MEGABYTE);
      if (value > max_size / multiplier) {
        error_message = "Size value too large (overflow): " + str;
        GOPHER_LOG(Error, "{}", error_message);
        return {false, 0};
      }
      result = static_cast<size_t>(value * multiplier);
    } else if (unit == "GB") {
      const uint64_t multiplier =
          static_cast<uint64_t>(UnitConversion::GIGABYTE);
      if (value > max_size / multiplier) {
        error_message = "Size value too large (overflow): " + str;
        GOPHER_LOG(Error, "{}", error_message);
        return {false, 0};
      }
      result = static_cast<size_t>(value * multiplier);
    }

    GOPHER_LOG(Debug, "Successfully parsed size '{}' to {} bytes", str, result);
    return {true, result};

  } catch (const std::exception& e) {
    error_message = "Failed to parse size value: " + std::string(e.what());
    GOPHER_LOG(Error, "{}", error_message);
    return {false, 0};
  }
}

// UnitValidator implementation
bool UnitValidator::isSuspiciousValue(const json::JsonValue& value,
                                      const std::string& field_name) {
  // Check if field name suggests it should have units
  std::string lower_field = field_name;
  std::transform(lower_field.begin(), lower_field.end(), lower_field.begin(),
                 ::tolower);

  bool is_duration_field = (lower_field.find("timeout") != std::string::npos ||
                            lower_field.find("duration") != std::string::npos ||
                            lower_field.find("interval") != std::string::npos ||
                            lower_field.find("delay") != std::string::npos ||
                            lower_field.find("period") != std::string::npos);

  bool is_size_field = (lower_field.find("size") != std::string::npos ||
                        lower_field.find("memory") != std::string::npos ||
                        lower_field.find("buffer") != std::string::npos ||
                        lower_field.find("limit") != std::string::npos ||
                        lower_field.find("quota") != std::string::npos);

  if (!is_duration_field && !is_size_field) {
    return false;
  }

  // Check if value is a large number without units
  if (value.isInteger() || value.isFloat()) {
    double num_value = value.isInteger() ? value.getInt64() : value.getFloat();

    // Suspicious if it's a duration field with value > 1000 (likely
    // milliseconds)
    if (is_duration_field && num_value > 1000) {
      GOPHER_LOG(Warning,
                 "Suspicious duration value for field '{}': {} (large number "
                 "without unit - assuming milliseconds)",
                 field_name, num_value);
      return true;
    }

    // Suspicious if it's a size field with value > 1048576 (1MB in bytes)
    if (is_size_field && num_value > 1048576) {
      GOPHER_LOG(Warning,
                 "Suspicious size value for field '{}': {} (large number "
                 "without unit - assuming bytes)",
                 field_name, num_value);
      return true;
    }
  }

  // Check if value is a string that looks like it's missing proper units
  if (value.isString()) {
    std::string str_value = value.getString();

    // Check if it's all digits (missing unit)
    if (!str_value.empty() &&
        std::all_of(str_value.begin(), str_value.end(), ::isdigit)) {
      if (is_duration_field) {
        GOPHER_LOG(Warning,
                   "Suspicious duration value for field '{}': '{}' (numeric "
                   "string without unit)",
                   field_name, str_value);
        return true;
      }
      if (is_size_field) {
        GOPHER_LOG(Warning,
                   "Suspicious size value for field '{}': '{}' (numeric string "
                   "without unit)",
                   field_name, str_value);
        return true;
      }
    }

    // Check for common mistakes
    if (str_value.find(" ") != std::string::npos) {
      GOPHER_LOG(Warning,
                 "Suspicious value for field '{}': '{}' (contains spaces - "
                 "units should be directly attached to number)",
                 field_name, str_value);
      return true;
    }
  }

  return false;
}

std::string UnitValidator::formatUnitError(const std::string& value,
                                           const std::string& field_path,
                                           const std::string& expected_format) {
  std::ostringstream error;
  error << "Invalid unit value for field '" << field_path << "': '" << value
        << "'.\n";
  error << "Expected format: " << expected_format << "\n";

  // Provide specific guidance based on the error
  if (value.find(" ") != std::string::npos) {
    error
        << "Note: Units must be directly attached to the number (no spaces).\n";
    error << "  Incorrect: '10 ms' or '5 GB'\n";
    error << "  Correct: '10ms' or '5GB'\n";
  }

  // Check if it looks like a plain number
  if (!value.empty() && std::all_of(value.begin(), value.end(), ::isdigit)) {
    error << "Note: Plain numbers should include units.\n";
    if (expected_format.find("ms") != std::string::npos) {
      error << "  Examples: '1000ms', '5s', '2m', '1h'\n";
    } else if (expected_format.find("B") != std::string::npos) {
      error << "  Examples: '1024B', '10KB', '5MB', '2GB'\n";
    }
  }

  // Check for incorrect unit case
  std::string lower = value;
  std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
  const bool contains_lower_unit = (lower.find("kb") != std::string::npos ||
                                    lower.find("mb") != std::string::npos ||
                                    lower.find("gb") != std::string::npos);
  const bool contains_upper_unit = (value.find("KB") != std::string::npos ||
                                    value.find("MB") != std::string::npos ||
                                    value.find("GB") != std::string::npos);
  if (contains_lower_unit && !contains_upper_unit) {
    error << "Note: Size units must be uppercase (KB, MB, GB).\n";
  }

  std::string error_msg = error.str();
  GOPHER_LOG(Error, "{}", error_msg);
  return error_msg;
}

}  // namespace config
}  // namespace mcp
