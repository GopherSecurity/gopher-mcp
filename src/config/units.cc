#define GOPHER_LOG_COMPONENT "config.units"

#include "mcp/config/units.h"

#include <algorithm>
#include <cctype>
#include <sstream>

#include "mcp/logging/log_macros.h"

namespace mcp {
namespace config {

// Duration implementation
std::pair<bool, std::chrono::milliseconds> Duration::parse(const std::string& str) {
  std::string error;
  return parseWithError(str, error);
}

std::pair<bool, std::chrono::milliseconds> Duration::parse(const json::JsonValue& value) {
  if (value.isString()) {
    return parse(value.getString());
  } else if (value.isInteger() || value.isFloat()) {
    // Assume milliseconds if no unit specified
    int64_t ms = value.isInteger() ? value.getInt64() : static_cast<int64_t>(value.getFloat());
    LOG_DEBUG("Parsing numeric value as milliseconds: %ld", static_cast<long>(ms));
    return {true, std::chrono::milliseconds(ms)};
  }
  
  LOG_ERROR("Invalid duration value type: expected string or number");
  return {false, std::chrono::milliseconds(0)};
}

std::string Duration::toString(std::chrono::milliseconds duration) {
  auto ms = duration.count();
  
  if (ms == 0) return "0ms";
  if (ms % (60 * 60 * 1000) == 0) return std::to_string(ms / (60 * 60 * 1000)) + "h";
  if (ms % (60 * 1000) == 0) return std::to_string(ms / (60 * 1000)) + "m";
  if (ms % 1000 == 0) return std::to_string(ms / 1000) + "s";
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
                   "'. Expected format: <number><unit> where unit is ms, s, m, or h (e.g., '30s', '5m', '1h')";
    LOG_ERROR("%s", error_message.c_str());
    return {false, std::chrono::milliseconds(0)};
  }
  
  try {
    int64_t value = std::stoll(match[1].str());
    std::string unit = match[2].str();
    
    if (value < 0) {
      error_message = "Duration value cannot be negative: " + str;
      LOG_ERROR("%s", error_message.c_str());
      return {false, std::chrono::milliseconds(0)};
    }
    
    std::chrono::milliseconds result;
    
    if (unit == "ms") {
      result = std::chrono::milliseconds(value);
    } else if (unit == "s") {
      result = std::chrono::seconds(value);
    } else if (unit == "m") {
      result = std::chrono::minutes(value);
    } else if (unit == "h") {
      result = std::chrono::hours(value);
    }
    
    // Check for overflow
    if (result.count() < 0) {
      error_message = "Duration value too large (overflow): " + str;
      LOG_ERROR("%s", error_message.c_str());
      return {false, std::chrono::milliseconds(0)};
    }
    
    LOG_DEBUG("Successfully parsed duration '%s' to %ld milliseconds",
              str.c_str(), static_cast<long>(result.count()));
    return {true, result};
    
  } catch (const std::exception& e) {
    error_message = "Failed to parse duration value: " + std::string(e.what());
    LOG_ERROR("%s", error_message.c_str());
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
  } else if (value.isInteger() || value.isFloat()) {
    // Assume bytes if no unit specified
    size_t bytes = value.isInteger() ? value.getInt64() : static_cast<size_t>(value.getFloat());
    LOG_DEBUG("Parsing numeric value as bytes: %zu", bytes);
    return {true, bytes};
  }
  
  LOG_ERROR("Invalid size value type: expected string or number");
  return {false, 0};
}

std::string Size::toString(size_t bytes) {
  if (bytes == 0) return "0B";
  
  if (bytes >= UnitConversion::GIGABYTE && bytes % UnitConversion::GIGABYTE == 0) {
    return std::to_string(bytes / UnitConversion::GIGABYTE) + "GB";
  }
  if (bytes >= UnitConversion::MEGABYTE && bytes % UnitConversion::MEGABYTE == 0) {
    return std::to_string(bytes / UnitConversion::MEGABYTE) + "MB";
  }
  if (bytes >= UnitConversion::KILOBYTE && bytes % UnitConversion::KILOBYTE == 0) {
    return std::to_string(bytes / UnitConversion::KILOBYTE) + "KB";
  }
  return std::to_string(bytes) + "B";
}

bool Size::isValid(const std::string& str) {
  static const std::regex pattern("^[0-9]+(B|KB|MB|GB)$");
  return std::regex_match(str, pattern);
}

std::pair<bool, size_t> Size::parseWithError(
    const std::string& str, std::string& error_message) {
  
  static const std::regex pattern("^([0-9]+)(B|KB|MB|GB)$");
  std::smatch match;
  
  if (!std::regex_match(str, match, pattern)) {
    error_message = "Invalid size format '" + str + 
                   "'. Expected format: <number><unit> where unit is B, KB, MB, or GB (e.g., '1024B', '10MB', '2GB')";
    LOG_ERROR("%s", error_message.c_str());
    return {false, 0};
  }
  
  try {
    uint64_t value = std::stoull(match[1].str());
    std::string unit = match[2].str();
    
    size_t result = 0;
    
    if (unit == "B") {
      result = value;
    } else if (unit == "KB") {
      // Check for overflow before multiplication
      if (value > SIZE_MAX / UnitConversion::KILOBYTE) {
        error_message = "Size value too large (overflow): " + str;
        LOG_ERROR("%s", error_message.c_str());
        return {false, 0};
      }
      result = value * UnitConversion::KILOBYTE;
    } else if (unit == "MB") {
      // Check for overflow before multiplication
      if (value > SIZE_MAX / UnitConversion::MEGABYTE) {
        error_message = "Size value too large (overflow): " + str;
        LOG_ERROR("%s", error_message.c_str());
        return {false, 0};
      }
      result = value * UnitConversion::MEGABYTE;
    } else if (unit == "GB") {
      // Check for overflow before multiplication
      if (value > SIZE_MAX / UnitConversion::GIGABYTE) {
        error_message = "Size value too large (overflow): " + str;
        LOG_ERROR("%s", error_message.c_str());
        return {false, 0};
      }
      result = value * UnitConversion::GIGABYTE;
    }
    
    LOG_DEBUG("Successfully parsed size '%s' to %zu bytes",
              str.c_str(), result);
    return {true, result};
    
  } catch (const std::exception& e) {
    error_message = "Failed to parse size value: " + std::string(e.what());
    LOG_ERROR("%s", error_message.c_str());
    return {false, 0};
  }
}

// UnitValidator implementation
bool UnitValidator::isSuspiciousValue(const json::JsonValue& value, const std::string& field_name) {
  // Check if field name suggests it should have units
  std::string lower_field = field_name;
  std::transform(lower_field.begin(), lower_field.end(), lower_field.begin(), ::tolower);
  
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
    
    // Suspicious if it's a duration field with value > 1000 (likely milliseconds)
    if (is_duration_field && num_value > 1000) {
      LOG_WARNING("Suspicious duration value for field '%s': %ld (large number without unit - assuming milliseconds)",
                  field_name.c_str(), static_cast<long>(num_value));
      return true;
    }
    
    // Suspicious if it's a size field with value > 1048576 (1MB in bytes)
    if (is_size_field && num_value > 1048576) {
      LOG_WARNING("Suspicious size value for field '%s': %ld (large number without unit - assuming bytes)",
                  field_name.c_str(), static_cast<long>(num_value));
      return true;
    }
  }
  
  // Check if value is a string that looks like it's missing proper units
  if (value.isString()) {
    std::string str_value = value.getString();
    
    // Check if it's all digits (missing unit)
    if (!str_value.empty() && std::all_of(str_value.begin(), str_value.end(), ::isdigit)) {
      if (is_duration_field) {
        LOG_WARNING("Suspicious duration value for field '%s': '%s' (numeric string without unit)",
                    field_name.c_str(), str_value.c_str());
        return true;
      }
      if (is_size_field) {
        LOG_WARNING("Suspicious size value for field '%s': '%s' (numeric string without unit)",
                    field_name.c_str(), str_value.c_str());
        return true;
      }
    }
    
    // Check for common mistakes
    if (str_value.find(" ") != std::string::npos) {
      LOG_WARNING("Suspicious value for field '%s': '%s' (contains spaces - units should be directly attached to number)",
                  field_name.c_str(), str_value.c_str());
      return true;
    }
  }
  
  return false;
}

std::string UnitValidator::formatUnitError(const std::string& value, 
                                          const std::string& field_path,
                                          const std::string& expected_format) {
  std::ostringstream error;
  error << "Invalid unit value for field '" << field_path << "': '" << value << "'.\n";
  error << "Expected format: " << expected_format << "\n";
  
  // Provide specific guidance based on the error
  if (value.find(" ") != std::string::npos) {
    error << "Note: Units must be directly attached to the number (no spaces).\n";
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
  if (lower != value && (lower.find("kb") != std::string::npos ||
                         lower.find("mb") != std::string::npos ||
                         lower.find("gb") != std::string::npos)) {
    error << "Note: Size units must be uppercase (KB, MB, GB).\n";
  }
  
  std::string error_msg = error.str();
  LOG_ERROR("%s", error_msg.c_str());
  return error.str();
}

}  // namespace config
}  // namespace mcp