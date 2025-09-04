#define GOPHER_LOG_COMPONENT "config.validate"

#include <algorithm>
#include <memory>
#include <regex>
#include <sstream>
#include <vector>

#include <nlohmann/json.hpp>
#include <nlohmann/json-schema.hpp>

#include "mcp/json/json_bridge.h"
#include "mcp/json/json_serialization.h"
#include "mcp/logging/log_macros.h"

namespace mcp {
namespace config {

// Local adapter functions for JSON conversion
namespace {

// Convert from mcp::json::JsonValue to nlohmann::json
nlohmann::json toNlohmann(const mcp::json::JsonValue& jv) {
  if (jv.isNull()) {
    return nullptr;
  } else if (jv.isBool()) {
    return jv.getBool();
  } else if (jv.isInt()) {
    return jv.getInt();
  } else if (jv.isDouble()) {
    return jv.getDouble();
  } else if (jv.isString()) {
    return jv.getString();
  } else if (jv.isArray()) {
    nlohmann::json arr = nlohmann::json::array();
    for (size_t i = 0; i < jv.size(); ++i) {
      arr.push_back(toNlohmann(jv[i]));
    }
    return arr;
  } else if (jv.isObject()) {
    nlohmann::json obj = nlohmann::json::object();
    for (const auto& key : jv.getMemberNames()) {
      obj[key] = toNlohmann(jv[key]);
    }
    return obj;
  }
  return nullptr;
}

// Convert from nlohmann::json to mcp::json::JsonValue  
mcp::json::JsonValue fromNlohmann(const nlohmann::json& nj) {
  if (nj.is_null()) {
    return mcp::json::JsonValue::null();
  } else if (nj.is_boolean()) {
    return mcp::json::JsonValue(nj.get<bool>());
  } else if (nj.is_number_integer()) {
    return mcp::json::JsonValue(nj.get<int64_t>());
  } else if (nj.is_number_float()) {
    return mcp::json::JsonValue(nj.get<double>());
  } else if (nj.is_string()) {
    return mcp::json::JsonValue(nj.get<std::string>());
  } else if (nj.is_array()) {
    mcp::json::JsonValue arr = mcp::json::JsonValue::array();
    for (const auto& item : nj) {
      arr.push_back(fromNlohmann(item));
    }
    return arr;
  } else if (nj.is_object()) {
    mcp::json::JsonValue obj = mcp::json::JsonValue::object();
    for (auto it = nj.begin(); it != nj.end(); ++it) {
      obj[it.key()] = fromNlohmann(it.value());
    }
    return obj;
  }
  return mcp::json::JsonValue::null();
}

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
      LOG_ERROR() << "Failed to compile JSON schema: " << e.what();
      throw std::runtime_error("Invalid JSON schema: " + std::string(e.what()));
    }
  }
  
  ValidationResult validate(const mcp::json::JsonValue& config,
                          ValidationMode mode) override {
    ValidationResult result;
    
    LOG_DEBUG() << "Starting schema validation: " << name_
                << " mode=" << modeToString(mode);
    
    // Convert config to nlohmann for validation
    nlohmann::json config_json = toNlohmann(config);
    
    // Perform schema validation
    try {
      nlohmann::json_schema::error_handler err_handler;
      validator_->validate(config_json, err_handler);
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
    for (const auto& key : config.getMemberNames()) {
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
    
    LOG_DEBUG() << "Starting range validation: " << name_
                << " rules=" << rules_.size();
    
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
    
    if (!value.isDouble() && !value.isInt()) {
      result.addError(rule.path, "Expected numeric value", 
                     extractCategory(rule.path));
      return;
    }
    
    double num_value = value.isDouble() ? value.getDouble() : 
                       static_cast<double>(value.getInt());
    
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
      if (!current.isObject() || !current.isMember(segment)) {
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
    
    LOG_INFO() << "Starting configuration validation:"
               << " validator=" << name_
               << " mode=" << modeToString(mode)
               << " validators=" << validators_.size();
    
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
    LOG_INFO() << "Configuration validation completed:"
               << " valid=" << (result.is_valid ? "true" : "false")
               << " errors=" << result.getErrorCount()
               << " warnings=" << result.getWarningCount();
    
    if (!result.failing_categories.empty()) {
      LOG_WARNING() << "Categories with validation failures: ["
                    << joinStrings(result.failing_categories, ", ") << "]";
    }
    
    if (result.getErrorCount() > 0 && result.getErrorCount() <= 5) {
      for (const auto& error : result.errors) {
        if (error.severity == ValidationError::Severity::ERROR) {
          LOG_ERROR() << "Validation error at " << error.path 
                      << ": " << error.message;
        }
      }
    } else if (result.getErrorCount() > 5) {
      LOG_ERROR() << "Multiple validation errors (" << result.getErrorCount() 
                  << " total). Showing first 5:";
      int shown = 0;
      for (const auto& error : result.errors) {
        if (error.severity == ValidationError::Severity::ERROR && shown < 5) {
          LOG_ERROR() << "  - " << error.path << ": " << error.message;
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