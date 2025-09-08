/**
 * @file parse_error.h
 * @brief Enhanced error handling for configuration parsing
 *
 * Provides detailed error context and diagnostics when parsing fails.
 */

#pragma once

#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "mcp/config/types.h"
#include "mcp/config/units.h"
#include "mcp/json/json_bridge.h"

namespace mcp {
namespace config {

// Use extractJsonValue from types.h

/**
 * @brief Enhanced configuration parse error with context
 */
class ConfigParseError : public std::runtime_error {
 public:
  ConfigParseError(const std::string& message,
                   const std::string& field = "",
                   const std::string& file = "",
                   int line = -1)
      : std::runtime_error(formatError(message, field, file, line)),
        message_(message),
        field_(field),
        file_(file),
        line_(line) {}

  const std::string& field() const { return field_; }
  const std::string& file() const { return file_; }
  int line() const { return line_; }
  const std::string& message() const { return message_; }

  /**
   * @brief Add context to the error
   */
  ConfigParseError& withContext(const std::string& context) {
    context_stack_.push_back(context);
    return *this;
  }

  /**
   * @brief Get the context stack
   */
  const std::vector<std::string>& contextStack() const {
    return context_stack_;
  }

 private:
  static std::string formatError(const std::string& msg,
                                 const std::string& field,
                                 const std::string& file,
                                 int line) {
    std::ostringstream oss;
    oss << "Configuration parse error";

    if (!file.empty()) {
      oss << " in " << file;
      if (line > 0) {
        oss << ":" << line;
      }
    }

    if (!field.empty()) {
      oss << " at field '" << field << "'";
    }

    oss << ": " << msg;
    return oss.str();
  }

  std::string message_;
  std::string field_;
  std::string file_;
  int line_;
  std::vector<std::string> context_stack_;
};

/**
 * @brief Parse context tracker for better error messages
 */
class ParseContext {
 public:
  ParseContext() = default;

  /**
   * @brief Push a field onto the path stack
   */
  void pushField(const std::string& field) { path_stack_.push_back(field); }

  /**
   * @brief Pop the last field from the path stack
   */
  void popField() {
    if (!path_stack_.empty()) {
      path_stack_.pop_back();
    }
  }

  /**
   * @brief Get the current path as a dot-separated string
   */
  std::string getCurrentPath() const {
    std::ostringstream oss;
    for (size_t i = 0; i < path_stack_.size(); ++i) {
      if (i > 0)
        oss << ".";
      oss << path_stack_[i];
    }
    return oss.str();
  }

  /**
   * @brief Set the current file being parsed
   */
  void setFile(const std::string& file) { current_file_ = file; }

  /**
   * @brief Get the current file
   */
  const std::string& getFile() const { return current_file_; }

  /**
   * @brief Create an error with current context
   */
  ConfigParseError createError(const std::string& message) const {
    return ConfigParseError(message, getCurrentPath(), current_file_);
  }

  /**
   * @brief RAII helper for field context
   */
  class FieldScope {
   public:
    FieldScope(ParseContext& ctx, const std::string& field) : ctx_(ctx) {
      ctx_.pushField(field);
    }

    ~FieldScope() { ctx_.popField(); }

   private:
    ParseContext& ctx_;
  };

 private:
  std::vector<std::string> path_stack_;
  std::string current_file_;
};

/**
 * @brief Safe JSON field accessor with error context
 */
template <typename T>
T getJsonField(const mcp::json::JsonValue& j,
               const std::string& field,
               ParseContext& ctx) {
  ParseContext::FieldScope scope(ctx, field);

  if (!j.contains(field)) {
    throw ctx.createError("Required field '" + field + "' is missing");
  }

  try {
    return extractJsonValue<T>(j[field]);
  } catch (const mcp::json::JsonException& e) {
    std::string type_name = typeid(T).name();
    throw ctx.createError("Failed to parse field '" + field + "' as type " +
                          type_name + ": " + e.what());
  }
}

/**
 * @brief Safe optional JSON field accessor
 */
template <typename T>
bool getOptionalJsonField(const mcp::json::JsonValue& j,
                          const std::string& field,
                          T& value,
                          ParseContext& ctx) {
  if (!j.contains(field)) {
    return false;
  }

  ParseContext::FieldScope scope(ctx, field);

  try {
    value = extractJsonValue<T>(j[field]);
    return true;
  } catch (const mcp::json::JsonException& e) {
    std::string type_name = typeid(T).name();
    throw ctx.createError("Failed to parse optional field '" + field +
                          "' as type " + type_name + ": " + e.what());
  }
}

/**
 * @brief Parse JSON with detailed error handling
 */
template <typename ConfigType>
ConfigType parseJsonWithContext(const mcp::json::JsonValue& j,
                                const std::string& config_name = "") {
  ParseContext ctx;
  if (!config_name.empty()) {
    ctx.pushField(config_name);
  }

  try {
    return ConfigType::fromJson(j);
  } catch (const ConfigValidationError& e) {
    throw ctx.createError("Validation failed: " + std::string(e.what()));
  } catch (const mcp::json::JsonException& e) {
    throw ctx.createError("JSON parsing failed: " + std::string(e.what()));
  } catch (const std::exception& e) {
    throw ctx.createError("Unexpected error: " + std::string(e.what()));
  }
}

/**
 * @brief Wrapper to add parse context to existing fromJson methods
 */
template <typename ConfigType>
class ConfigParser {
 public:
  static ConfigType parse(const mcp::json::JsonValue& j, ParseContext& ctx) {
    try {
      return ConfigType::fromJson(j, ctx);
    } catch (const ConfigValidationError& e) {
      // Re-throw with context
      throw ConfigParseError(e.reason(),
                             ctx.getCurrentPath() + "." + e.field());
    } catch (const UnitParseError& e) {
      // Re-throw with context
      throw ctx.createError(e.what());
    } catch (const mcp::json::JsonException& e) {
      // JSON parsing error
      throw ctx.createError(std::string("JSON error: ") + e.what());
    } catch (const std::exception& e) {
      // Generic error
      throw ctx.createError(std::string("Parse error: ") + e.what());
    }
  }

  static ConfigType parseField(const mcp::json::JsonValue& parent,
                               const std::string& field,
                               ParseContext& ctx) {
    ParseContext::FieldScope scope(ctx, field);

    if (!parent.contains(field)) {
      throw ctx.createError("Required field missing");
    }

    return parse(parent[field], ctx);
  }

  static bool parseOptionalField(const mcp::json::JsonValue& parent,
                                 const std::string& field,
                                 ConfigType& result,
                                 ParseContext& ctx) {
    if (!parent.contains(field)) {
      return false;
    }

    ParseContext::FieldScope scope(ctx, field);
    result = parse(parent[field], ctx);
    return true;
  }
};

/**
 * @brief Load configuration from file with enhanced error reporting
 */
template <typename ConfigType>
ConfigType loadConfigWithDiagnostics(const std::string& filename) {
  ParseContext ctx;
  ctx.setFile(filename);

  std::ifstream file(filename);
  if (!file.is_open()) {
    throw ctx.createError("Failed to open file");
  }

  mcp::json::JsonValue j;
  try {
    std::string content((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());
    j = mcp::json::JsonValue::parse(content);
  } catch (const mcp::json::JsonException& e) {
    throw ConfigParseError("JSON syntax error: " + std::string(e.what()), "",
                           filename);
  }

  try {
    return ConfigParser<ConfigType>::parse(j, ctx);
  } catch (const ConfigParseError& e) {
    // Already has context, re-throw
    throw;
  } catch (const std::exception& e) {
    throw ctx.createError("Failed to parse configuration: " +
                          std::string(e.what()));
  }
}

/**
 * @brief Helper macro for wrapping existing fromJson implementations
 */
#define WRAP_FROM_JSON(ConfigType, json, ctx) \
  ConfigParser<ConfigType>::parse(json, ctx)

/**
 * @brief Helper to provide detailed JSON path in error messages
 */
class JsonPath {
 public:
  JsonPath() = default;

  JsonPath& operator/(const std::string& segment) {
    segments_.push_back(segment);
    return *this;
  }

  JsonPath& operator/(size_t index) {
    segments_.push_back("[" + std::to_string(index) + "]");
    return *this;
  }

  std::string toString() const {
    std::ostringstream oss;
    for (size_t i = 0; i < segments_.size(); ++i) {
      if (i > 0 && segments_[i][0] != '[') {
        oss << ".";
      }
      oss << segments_[i];
    }
    return oss.str();
  }

  operator std::string() const { return toString(); }

 private:
  std::vector<std::string> segments_;
};

/**
 * @brief Pretty-print JSON excerpt for error messages
 */
inline std::string getJsonExcerpt(const mcp::json::JsonValue& j,
                                  size_t max_length = 100) {
  std::string str = j.toString();
  if (str.length() > max_length) {
    str = str.substr(0, max_length - 3) + "...";
  }
  return str;
}

/**
 * @brief Validate JSON against expected type with error reporting
 */
inline void validateJsonType(const mcp::json::JsonValue& j,
                             mcp::json::JsonType expected_type,
                             const std::string& field,
                             ParseContext& ctx) {
  if (j.type() != expected_type) {
    std::string actual;
    std::string expected;

    // Get actual type name
    switch (j.type()) {
      case mcp::json::JsonType::Null:
        actual = "null";
        break;
      case mcp::json::JsonType::Boolean:
        actual = "boolean";
        break;
      case mcp::json::JsonType::Integer:
        actual = "integer";
        break;
      case mcp::json::JsonType::Float:
        actual = "float";
        break;
      case mcp::json::JsonType::String:
        actual = "string";
        break;
      case mcp::json::JsonType::Array:
        actual = "array";
        break;
      case mcp::json::JsonType::Object:
        actual = "object";
        break;
      default:
        actual = "unknown";
        break;
    }

    // Get expected type name
    switch (expected_type) {
      case mcp::json::JsonType::Null:
        expected = "null";
        break;
      case mcp::json::JsonType::Boolean:
        expected = "boolean";
        break;
      case mcp::json::JsonType::Integer:
      case mcp::json::JsonType::Float:
        expected = "number";
        break;
      case mcp::json::JsonType::String:
        expected = "string";
        break;
      case mcp::json::JsonType::Array:
        expected = "array";
        break;
      case mcp::json::JsonType::Object:
        expected = "object";
        break;
      default:
        expected = "unknown";
        break;
    }

    throw ctx.createError("Expected " + expected + " for field '" + field +
                          "', got " + actual);
  }
}

}  // namespace config
}  // namespace mcp
