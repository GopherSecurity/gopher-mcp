/**
 * @file config_validator.h
 * @brief Public interfaces for schema and range validators.
 */

#pragma once

#include <limits>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "mcp/json/json_bridge.h"

namespace mcp {
namespace config {

struct ValidationError {
  enum class Severity { ERROR, WARNING, INFO };
  Severity severity;
  std::string path;
  std::string message;
  std::string category;
};

struct ValidationResult {
  bool is_valid{true};
  std::vector<ValidationError> errors;
  std::set<std::string> failing_categories;
  std::set<std::string> unknown_fields;

  // Helpers used by validator implementations
  void addError(const std::string& path,
                const std::string& message,
                const std::string& category = "");
  void addWarning(const std::string& path,
                  const std::string& message,
                  const std::string& category = "");

  size_t getErrorCount() const;
  size_t getWarningCount() const;
};

class Validator {
 public:
  enum class ValidationMode { STRICT, WARN, PERMISSIVE };
  virtual ~Validator() = default;
  virtual ValidationResult validate(
      const mcp::json::JsonValue& config,
      ValidationMode mode = ValidationMode::WARN) = 0;
  virtual std::string getName() const = 0;
};

class RangeValidator : public Validator {
 public:
  struct RangeRule {
    std::string path;
    double min = std::numeric_limits<double>::lowest();
    double max = std::numeric_limits<double>::max();
    bool min_inclusive = true;
    bool max_inclusive = true;
  };

  explicit RangeValidator(const std::string& name);
  void addRule(const RangeRule& rule);
  ValidationResult validate(const mcp::json::JsonValue& config,
                            ValidationMode mode) override;
  std::string getName() const override;

 private:
  std::string name_;
  std::vector<RangeRule> rules_;
};

class CompositeValidator : public Validator {
 public:
  explicit CompositeValidator(const std::string& name);
  void addValidator(std::unique_ptr<Validator> validator);
  ValidationResult validate(const mcp::json::JsonValue& config,
                            ValidationMode mode) override;
  std::string getName() const override;

 private:
  std::string name_;
  std::vector<std::unique_ptr<Validator>> validators_;
};

// Factory functions
std::unique_ptr<Validator> createSchemaValidator(
    const std::string& name, const mcp::json::JsonValue& schema);
std::unique_ptr<RangeValidator> createRangeValidator(const std::string& name);
std::unique_ptr<CompositeValidator> createCompositeValidator(
    const std::string& name);

}  // namespace config
}  // namespace mcp
