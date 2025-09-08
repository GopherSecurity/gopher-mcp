#define GOPHER_LOG_COMPONENT "config.validate"

#include <algorithm>
#include <memory>
#include <regex>
#include <set>
#include <sstream>
#include <vector>

#include <nlohmann/json.hpp>
#if MCP_HAS_JSON_SCHEMA_VALIDATOR
#include <nlohmann/json-schema.hpp>
#endif

#include "mcp/config/json_conversion.h"
#include "mcp/json/json_bridge.h"
#include "mcp/json/json_serialization.h"
#include "mcp/logging/log_macros.h"

namespace mcp {
namespace config {

// Use shared conversion utilities from json_conversion.h
namespace {
using mcp::config::toNlohmann;
using mcp::config::fromNlohmann;
}  // namespace

// Validation error information
struct ValidationError {
  enum class Severity {
    ERROR,
    WARNING,
    INFO
  };
  
  Severity severity;
  std::string path;
  std::string message;
  std::string category;  // Top-level category affected
};

// Validation result
struct ValidationResult {
  bool is_valid = true;
  std::vector<ValidationError> errors;
  std::set<std::string> failing_categories;
  std::set<std::string> unknown_fields;
  
  void addError(const std::string& path, const std::string& message, 
                const std::string& category = "") {
    errors.push_back({ValidationError::Severity::ERROR, path, message, category});
    is_valid = false;
    if (!category.empty()) {
      failing_categories.insert(category);
    }
  }
  
  void addWarning(const std::string& path, const std::string& message,
                  const std::string& category = "") {
    errors.push_back({ValidationError::Severity::WARNING, path, message, category});
  }
  
  size_t getErrorCount() const {
    return std::count_if(errors.begin(), errors.end(),
                         [](const ValidationError& e) { 
                           return e.severity == ValidationError::Severity::ERROR; 
                         });
  }
  
  size_t getWarningCount() const {
    return std::count_if(errors.begin(), errors.end(),
                         [](const ValidationError& e) { 
                           return e.severity == ValidationError::Severity::WARNING; 
                         });
  }
};

// Base validator interface
class Validator {
 public:
  enum class ValidationMode {
    STRICT,      // Unknown fields are errors
    WARN,        // Unknown fields generate warnings
    PERMISSIVE   // Unknown fields are ignored
  };
  
  virtual ~Validator() = default;
  
  virtual ValidationResult validate(const mcp::json::JsonValue& config,
                                   ValidationMode mode = ValidationMode::WARN) = 0;
  
  virtual std::string getName() const = 0;
};

// Schema-based validator
#if MCP_HAS_JSON_SCHEMA_VALIDATOR
class SchemaValidator : public Validator {
 public:
  SchemaValidator(const std::string& name, const mcp::json::JsonValue& schema)
      : name_(name) {
    // Convert schema to nlohmann for validation
    schema_json_ = toNlohmann(schema);
    
    try {
      validator_ = std::make_unique<nlohmann::json_schema::json_validator>();
      validator_->set_root_schema(schema_json_);
    } catch (const std::exception& e) {
      LOG_ERROR("Failed to compile JSON schema: %s", e.what());
      throw std::runtime_error("Invalid JSON schema: " + std::string(e.what()));
    }
  }
  
  ValidationResult validate(const mcp::json::JsonValue& config,
                          ValidationMode mode) override {
    ValidationResult result;
    
    LOG_DEBUG("Starting schema validation: %s mode=%s",
              name_.c_str(), modeToString(mode).c_str());
    
    // Convert config to nlohmann for validation
    nlohmann::json config_json = toNlohmann(config);
    
    // Perform schema validation
    try {
      class ValidationErrorHandler : public nlohmann::json_schema::error_handler {
       public:
        void error(const nlohmann::json::json_pointer& ptr, 
                   const nlohmann::json& instance, 
                   const std::string& message) override {
          errors_.push_back(ptr.to_string() + ": " + message);
        }
        std::vector<std::string> errors_;
      };
      
      ValidationErrorHandler err_handler;
      validator_->validate(config_json, err_handler);
      
      for (const auto& error : err_handler.errors_) {
        result.addError("", "Schema validation error: " + error);
      }
    } catch (const std::exception& e) {
      // Schema validation failed
      result.addError("", "Schema validation failed: " + std::string(e.what()));
    }
    
    // Check for unknown fields based on mode
    if (mode != ValidationMode::PERMISSIVE) {
      checkUnknownFields(config, result, mode);
    }
    
    return result;
  }
  
  std::string getName() const override { return name_; }

 private:
  void checkUnknownFields(const mcp::json::JsonValue& config,
                          ValidationResult& result,
                          ValidationMode mode,
                          const std::string& path = "") {
    if (!config.isObject()) return;
    
    // Get allowed fields from schema
    std::set<std::string> allowed_fields;
    if (schema_json_.contains("properties")) {
      for (auto& [key, _] : schema_json_["properties"].items()) {
        allowed_fields.insert(key);
      }
    }
    
    // Check each field in config
    for (const auto& key : config.keys()) {
      std::string current_path = path.empty() ? key : path + "." + key;
      
      if (allowed_fields.find(key) == allowed_fields.end()) {
        result.unknown_fields.insert(current_path);
        
        if (mode == ValidationMode::STRICT) {
          result.addError(current_path, "Unknown field", extractCategory(current_path));
        } else if (mode == ValidationMode::WARN) {
          result.addWarning(current_path, "Unknown field", extractCategory(current_path));
        }
      } else if (config[key].isObject()) {
        // Recursively check nested objects
        checkUnknownFields(config[key], result, mode, current_path);
      }
    }
  }
  
  std::string modeToString(ValidationMode mode) {
    switch (mode) {
      case ValidationMode::STRICT: return "strict";
      case ValidationMode::WARN: return "warn";
      case ValidationMode::PERMISSIVE: return "permissive";
    }
    return "unknown";
  }
  
  std::string extractCategory(const std::string& path) {
    size_t dot_pos = path.find('.');
    return (dot_pos != std::string::npos) ? path.substr(0, dot_pos) : path;
  }
  
  std::string name_;
  nlohmann::json schema_json_;
  std::unique_ptr<nlohmann::json_schema::json_validator> validator_;
};
#else
// Fallback stub when json-schema-validator is unavailable
class SchemaValidator : public Validator {
 public:
  SchemaValidator(const std::string& name, const mcp::json::JsonValue& schema)
      : name_(name) {
    (void)schema;
  }
  ValidationResult validate(const mcp::json::JsonValue& config,
                            ValidationMode mode) override {
    (void)config;
    (void)mode;
    ValidationResult res;
    res.is_valid = false;
    res.addError("", "Schema validation unavailable: json-schema-validator not linked");
    return res;
  }
  std::string getName() const override { return name_; }

 private:
  std::string name_;
};
#endif

// Range validator for numeric bounds
class RangeValidator : public Validator {
 public:
  struct RangeRule {
    std::string path;
    double min = std::numeric_limits<double>::lowest();
    double max = std::numeric_limits<double>::max();
    bool min_inclusive = true;
    bool max_inclusive = true;
  };
  
  RangeValidator(const std::string& name) : name_(name) {}
  
  void addRule(const RangeRule& rule) {
    rules_.push_back(rule);
  }
  
  ValidationResult validate(const mcp::json::JsonValue& config,
                          ValidationMode mode) override {
    ValidationResult result;
    
    LOG_DEBUG("Starting range validation: %s rules=%zu",
              name_.c_str(), rules_.size());
    
    for (const auto& rule : rules_) {
      validatePath(config, rule, result);
    }
    
    return result;
  }
  
  std::string getName() const override { return name_; }

 private:
  void validatePath(const mcp::json::JsonValue& config,
                   const RangeRule& rule,
                   ValidationResult& result) {
    // Navigate to the path
    mcp::json::JsonValue value = navigateToPath(config, rule.path);
    
    if (value.isNull()) {
      // Path doesn't exist - might be optional
      return;
    }
    
    if (!value.isFloat() && !value.isInteger()) {
      result.addError(rule.path, "Expected numeric value", 
                     extractCategory(rule.path));
      return;
    }
    
    double num_value = value.isFloat() ? value.getFloat() : 
                       static_cast<double>(value.getInt64());
    
    // Check min bound
    if (rule.min_inclusive) {
      if (num_value < rule.min) {
        result.addError(rule.path, 
                       "Value " + std::to_string(num_value) + 
                       " is below minimum " + std::to_string(rule.min),
                       extractCategory(rule.path));
      }
    } else {
      if (num_value <= rule.min) {
        result.addError(rule.path,
                       "Value " + std::to_string(num_value) + 
                       " must be greater than " + std::to_string(rule.min),
                       extractCategory(rule.path));
      }
    }
    
    // Check max bound
    if (rule.max_inclusive) {
      if (num_value > rule.max) {
        result.addError(rule.path,
                       "Value " + std::to_string(num_value) + 
                       " exceeds maximum " + std::to_string(rule.max),
                       extractCategory(rule.path));
      }
    } else {
      if (num_value >= rule.max) {
        result.addError(rule.path,
                       "Value " + std::to_string(num_value) + 
                       " must be less than " + std::to_string(rule.max),
                       extractCategory(rule.path));
      }
    }
  }
  
  mcp::json::JsonValue navigateToPath(const mcp::json::JsonValue& root,
                                      const std::string& path) {
    mcp::json::JsonValue current = root;
    std::istringstream path_stream(path);
    std::string segment;
    
    while (std::getline(path_stream, segment, '.')) {
      if (!current.isObject() || !current.contains(segment)) {
        return mcp::json::JsonValue::null();
      }
      current = current[segment];
    }
    
    return current;
  }
  
  std::string extractCategory(const std::string& path) {
    size_t dot_pos = path.find('.');
    return (dot_pos != std::string::npos) ? path.substr(0, dot_pos) : path;
  }
  
  std::string name_;
  std::vector<RangeRule> rules_;
};

// Composite validator that runs multiple validators
class CompositeValidator : public Validator {
 public:
  CompositeValidator(const std::string& name) : name_(name) {}
  
  void addValidator(std::unique_ptr<Validator> validator) {
    validators_.push_back(std::move(validator));
  }
  
  ValidationResult validate(const mcp::json::JsonValue& config,
                          ValidationMode mode) override {
    ValidationResult result;
    
    LOG_INFO("Starting configuration validation: validator=%s mode=%s validators=%zu",
             name_.c_str(), modeToString(mode).c_str(), validators_.size());
    
    for (const auto& validator : validators_) {
      auto sub_result = validator->validate(config, mode);
      
      // Merge results
      result.is_valid = result.is_valid && sub_result.is_valid;
      result.errors.insert(result.errors.end(), 
                          sub_result.errors.begin(), 
                          sub_result.errors.end());
      result.failing_categories.insert(sub_result.failing_categories.begin(),
                                      sub_result.failing_categories.end());
      result.unknown_fields.insert(sub_result.unknown_fields.begin(),
                                  sub_result.unknown_fields.end());
    }
    
    // Log validation summary
    LOG_INFO("Configuration validation completed: valid=%s errors=%zu warnings=%zu",
             result.is_valid ? "true" : "false",
             result.getErrorCount(), result.getWarningCount());
    
    if (!result.failing_categories.empty()) {
      std::string categories = joinStrings(result.failing_categories, ", ");
      LOG_WARNING("Categories with validation failures: [%s]", categories.c_str());
    }
    
    if (result.getErrorCount() > 0 && result.getErrorCount() <= 5) {
      for (const auto& error : result.errors) {
        if (error.severity == ValidationError::Severity::ERROR) {
          LOG_ERROR("Validation error at %s: %s",
                    error.path.c_str(), error.message.c_str());
        }
      }
    } else if (result.getErrorCount() > 5) {
      LOG_ERROR("Multiple validation errors (%zu total). Showing first 5:",
                result.getErrorCount());
      int shown = 0;
      for (const auto& error : result.errors) {
        if (error.severity == ValidationError::Severity::ERROR && shown < 5) {
          LOG_ERROR("  - %s: %s", error.path.c_str(), error.message.c_str());
          shown++;
        }
      }
    }
    
    return result;
  }
  
  std::string getName() const override { return name_; }

 private:
  std::string modeToString(ValidationMode mode) {
    switch (mode) {
      case ValidationMode::STRICT: return "strict";
      case ValidationMode::WARN: return "warn";
      case ValidationMode::PERMISSIVE: return "permissive";
    }
    return "unknown";
  }
  
  std::string joinStrings(const std::set<std::string>& strings,
                          const std::string& delimiter) {
    std::ostringstream oss;
    auto it = strings.begin();
    if (it != strings.end()) {
      oss << *it;
      ++it;
    }
    while (it != strings.end()) {
      oss << delimiter << *it;
      ++it;
    }
    return oss.str();
  }
  
  std::string name_;
  std::vector<std::unique_ptr<Validator>> validators_;
};

// Factory functions
std::unique_ptr<Validator> createSchemaValidator(
    const std::string& name,
    const mcp::json::JsonValue& schema) {
  return std::make_unique<SchemaValidator>(name, schema);
}

std::unique_ptr<RangeValidator> createRangeValidator(const std::string& name) {
  return std::make_unique<RangeValidator>(name);
}

std::unique_ptr<CompositeValidator> createCompositeValidator(
    const std::string& name) {
  return std::make_unique<CompositeValidator>(name);
}

}  // namespace config
}  // namespace mcp
