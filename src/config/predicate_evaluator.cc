/**
 * @file predicate_evaluator.cc
 * @brief Implementation of predicate evaluation system for conditional filter
 * inclusion
 */

#include "mcp/config/predicate_evaluator.h"

#include <algorithm>
#include <cstdlib>
#include <numeric>
#include <sstream>

#define GOPHER_LOG_COMPONENT "config.predicates"
#include "mcp/logging/log_macros.h"

namespace mcp {
namespace config {

// ============================================================================
// PredicateContext Implementation
// ============================================================================

PredicateContext PredicateContext::fromJson(const json::JsonValue& j) {
  PredicateContext ctx;

  if (j.contains("node_metadata") && j["node_metadata"].isObject()) {
    for (const auto& key : j["node_metadata"].keys()) {
      ctx.node_metadata[key] = j["node_metadata"][key].getString();
    }
  }

  if (j.contains("transport_type")) {
    ctx.transport_type = j["transport_type"].getString();
  }

  if (j.contains("transport_name")) {
    ctx.transport_name = j["transport_name"].getString();
  }

  if (j.contains("route_path")) {
    ctx.route_path = j["route_path"].getString();
  }

  if (j.contains("extra_context")) {
    ctx.extra_context = j["extra_context"];
  }

  return ctx;
}

json::JsonValue PredicateContext::toJson() const {
  json::JsonObjectBuilder builder;

  if (!node_metadata.empty()) {
    auto meta_obj = json::JsonValue::object();
    for (const auto& kv : node_metadata) {
      meta_obj[kv.first] = json::JsonValue(kv.second);
    }
    builder.add("node_metadata", meta_obj);
  }

  if (!transport_type.empty()) {
    builder.add("transport_type", transport_type);
  }

  if (!transport_name.empty()) {
    builder.add("transport_name", transport_name);
  }

  if (!route_path.empty()) {
    builder.add("route_path", route_path);
  }

  if (!extra_context.isNull()) {
    builder.add("extra_context", extra_context);
  }

  return builder.build();
}

// ============================================================================
// PredicateEvaluator Implementation
// ============================================================================

PredicateEvaluator::PredicateEvaluator() {
  GOPHER_LOG(Debug, "PredicateEvaluator initialized with safe_defaults={}",
             safe_defaults_);
}

PredicateEvaluator::~PredicateEvaluator() = default;

PredicateResult PredicateEvaluator::evaluateSingle(
    const std::string& predicate_expr, const PredicateContext& context) const {
  GOPHER_LOG(Debug, "Evaluating single predicate: '{}'", predicate_expr);

  std::string type, key, value;
  if (!parsePredicateExpr(predicate_expr, type, key, value)) {
    GOPHER_LOG(Warning, "Failed to parse predicate expression: '{}'",
               predicate_expr);
    return PredicateResult(false, false,
                           "Invalid predicate format: " + predicate_expr);
  }

  PredicateResult result;
  result.predicate_id = predicate_expr;

  if (type == "env") {
    result = evaluateEnvPredicate(key + (value.empty() ? "" : "=" + value));
  } else if (type == "node.metadata") {
    result = evaluateNodePredicate(key, value, context);
  } else if (type == "transport") {
    result = evaluateTransportPredicate(key, value, context);
  } else if (type == "route") {
    result = evaluateRoutePredicate(key, value, context);
  } else {
    GOPHER_LOG(Warning, "Unknown predicate type: '{}'", type);
    result = PredicateResult(false, false, "Unknown predicate type: " + type);
  }

  result.predicate_id = predicate_expr;

  GOPHER_LOG(Debug, "Predicate '{}' evaluated to: {} (success={})",
             predicate_expr, result.matched, result.success);

  if (!result.success && !result.error_message.empty()) {
    GOPHER_LOG(Warning, "Predicate evaluation error for '{}': {}",
               predicate_expr, result.error_message);
  }

  return result;
}

PredicateResult PredicateEvaluator::evaluate(
    const json::JsonValue& predicates, const PredicateContext& context) const {
  if (predicates.isNull() || !predicates.isObject()) {
    return PredicateResult(true, true);  // No predicates = always match
  }

  GOPHER_LOG(Debug, "Evaluating predicate set with {} conditions",
             predicates.keys().size());

  PredicateResult overall_result(true, true);
  std::vector<std::string> failed_predicates;

  // All predicates must match (AND logic)
  for (const auto& key : predicates.keys()) {
    std::string predicate_expr;

    // Build predicate expression from key and value
    if (predicates[key].isString()) {
      predicate_expr = key + ":" + predicates[key].getString();
    } else if (predicates[key].isBoolean()) {
      predicate_expr =
          key + "=" + (predicates[key].getBool() ? "true" : "false");
    } else {
      // Complex predicate value, skip
      GOPHER_LOG(Warning, "Skipping complex predicate value for key '{}'", key);
      continue;
    }

    auto result = evaluateSingle(predicate_expr, context);

    if (!result.success) {
      overall_result.success = false;
      if (!overall_result.error_message.empty()) {
        overall_result.error_message += "; ";
      }
      overall_result.error_message += result.error_message;
      failed_predicates.push_back(predicate_expr);
    }

    if (!result.matched) {
      overall_result.matched = false;
      // Continue evaluating to collect all errors
    }
  }

  if (!failed_predicates.empty()) {
    GOPHER_LOG(Debug, "Failed predicates: {}",
               std::accumulate(failed_predicates.begin(),
                               failed_predicates.end(), std::string(),
                               [](const std::string& a, const std::string& b) {
                                 return a.empty() ? b : a + ", " + b;
                               }));
  }

  return overall_result;
}

bool PredicateEvaluator::isFilterEnabled(
    const json::JsonValue& filter_config,
    const PredicateContext& context) const {
  // Check explicit enabled field first
  if (filter_config.contains("enabled")) {
    bool enabled = filter_config["enabled"].getBool();
    if (!enabled) {
      GOPHER_LOG(Debug, "Filter explicitly disabled via 'enabled' field");
      return false;
    }
  }

  // Check enabled_when predicates
  if (filter_config.contains("enabled_when")) {
    auto result = evaluate(filter_config["enabled_when"], context);

    if (!result.success && safe_defaults_) {
      GOPHER_LOG(
          Warning,
          "Predicate evaluation failed, using safe default (disabled): {}",
          result.error_message);
      return false;
    }

    return result.matched;
  }

  // Default to enabled if no conditions specified
  return true;
}

bool PredicateEvaluator::shouldBypassFilters(
    const json::JsonValue& route_config,
    const PredicateContext& context) const {
  if (route_config.contains("bypass_filters")) {
    if (route_config["bypass_filters"].isBoolean()) {
      bool bypass = route_config["bypass_filters"].getBool();
      GOPHER_LOG(Debug, "Route bypass_filters set to: {}", bypass);
      return bypass;
    } else if (route_config["bypass_filters"].isObject()) {
      // Conditional bypass based on predicates
      auto result = evaluate(route_config["bypass_filters"], context);

      if (!result.success && safe_defaults_) {
        GOPHER_LOG(Warning,
                   "Bypass predicate evaluation failed, using safe default (no "
                   "bypass): {}",
                   result.error_message);
        return false;
      }

      GOPHER_LOG(Debug, "Route bypass_filters evaluated to: {}",
                 result.matched);
      return result.matched;
    }
  }

  return false;
}

std::vector<std::string> PredicateEvaluator::validatePredicates(
    const json::JsonValue& predicates) const {
  std::vector<std::string> errors;

  if (!predicates.isObject()) {
    errors.push_back("Predicates must be a JSON object");
    return errors;
  }

  for (const auto& key : predicates.keys()) {
    // Validate key format
    if (key.empty()) {
      errors.push_back("Empty predicate key");
      continue;
    }

    // Check for valid predicate types
    if (key != "env" && !hasPrefix(key, "env:") &&
        !hasPrefix(key, "node.metadata.") && !hasPrefix(key, "transport.") &&
        !hasPrefix(key, "route.")) {
      errors.push_back("Unknown predicate type: " + key);
    }

    // Validate value type
    if (!predicates[key].isString() && !predicates[key].isBoolean() &&
        !predicates[key].isNull()) {
      errors.push_back(
          "Predicate value must be string, boolean, or null for key: " + key);
    }
  }

  return errors;
}

PredicateResult PredicateEvaluator::evaluateEnvPredicate(
    const std::string& predicate) const {
  size_t eq_pos = predicate.find('=');
  std::string var_name;
  std::string expected_value;

  if (eq_pos != std::string::npos) {
    var_name = predicate.substr(0, eq_pos);
    expected_value = predicate.substr(eq_pos + 1);
  } else {
    var_name = predicate;
    // No value means check for existence
  }

  std::string actual_value = getEnvVar(var_name);

  if (expected_value.empty()) {
    // Existence check
    bool exists = !actual_value.empty();
    GOPHER_LOG(Debug, "Environment variable '{}' existence check: {}", var_name,
               exists);
    return PredicateResult(exists, true);
  } else {
    // Value check
    bool matches = (actual_value == expected_value);
    GOPHER_LOG(Debug,
               "Environment variable '{}' value check: {} (expected '{}')",
               var_name, matches ? "match" : "no match", expected_value);
    return PredicateResult(matches, true);
  }
}

PredicateResult PredicateEvaluator::evaluateNodePredicate(
    const std::string& key,
    const std::string& expected_value,
    const PredicateContext& context) const {
  auto it = context.node_metadata.find(key);

  if (expected_value.empty()) {
    // Existence check
    bool exists = (it != context.node_metadata.end());
    GOPHER_LOG(Debug, "Node metadata key '{}' existence check: {}", key,
               exists);
    return PredicateResult(exists, true);
  } else {
    // Value check
    if (it == context.node_metadata.end()) {
      GOPHER_LOG(Debug, "Node metadata key '{}' not found", key);
      return PredicateResult(false, true);
    }

    bool matches = (it->second == expected_value);
    GOPHER_LOG(Debug, "Node metadata '{}' value check: {} (expected '{}')", key,
               matches ? "match" : "no match", expected_value);
    return PredicateResult(matches, true);
  }
}

PredicateResult PredicateEvaluator::evaluateTransportPredicate(
    const std::string& predicate_type,
    const std::string& expected_value,
    const PredicateContext& context) const {
  if (predicate_type == "type") {
    bool matches = (context.transport_type == expected_value);
    GOPHER_LOG(Debug, "Transport type check: {} (actual '{}', expected '{}')",
               matches ? "match" : "no match", context.transport_type,
               expected_value);
    return PredicateResult(matches, true);
  } else if (predicate_type == "name") {
    bool matches = (context.transport_name == expected_value);
    GOPHER_LOG(Debug, "Transport name check: {} (actual '{}', expected '{}')",
               matches ? "match" : "no match", context.transport_name,
               expected_value);
    return PredicateResult(matches, true);
  } else {
    return PredicateResult(
        false, false, "Unknown transport predicate type: " + predicate_type);
  }
}

PredicateResult PredicateEvaluator::evaluateRoutePredicate(
    const std::string& predicate_type,
    const std::string& expected_value,
    const PredicateContext& context) const {
  if (predicate_type == "path") {
    bool matches = (context.route_path == expected_value);
    GOPHER_LOG(Debug, "Route path check: {} (actual '{}', expected '{}')",
               matches ? "match" : "no match", context.route_path,
               expected_value);
    return PredicateResult(matches, true);
  } else if (predicate_type == "path.prefix") {
    bool matches = hasPrefix(context.route_path, expected_value);
    GOPHER_LOG(Debug, "Route path prefix check: {} (path '{}', prefix '{}')",
               matches ? "match" : "no match", context.route_path,
               expected_value);
    return PredicateResult(matches, true);
  } else {
    return PredicateResult(false, false,
                           "Unknown route predicate type: " + predicate_type);
  }
}

bool PredicateEvaluator::parsePredicateExpr(const std::string& expr,
                                            std::string& type,
                                            std::string& key,
                                            std::string& value) const {
  if (expr.empty()) {
    return false;
  }

  // Parse different predicate formats
  size_t colon_pos = expr.find(':');
  size_t eq_pos = expr.find('=');

  if (colon_pos != std::string::npos) {
    std::string prefix = expr.substr(0, colon_pos);
    std::string remainder = expr.substr(colon_pos + 1);

    if (prefix == "env") {
      type = "env";
      size_t eq_in_remainder = remainder.find('=');
      if (eq_in_remainder != std::string::npos) {
        key = remainder.substr(0, eq_in_remainder);
        value = remainder.substr(eq_in_remainder + 1);
      } else {
        key = remainder;
        value = "";  // Existence check
      }
      return true;
    } else if (hasPrefix(expr, "node.metadata.")) {
      type = "node.metadata";
      std::string full_key =
          expr.substr(0, eq_pos != std::string::npos ? eq_pos : expr.length());
      key = full_key.substr(14);  // Remove "node.metadata." prefix
      if (eq_pos != std::string::npos) {
        value = expr.substr(eq_pos + 1);
      }
      return true;
    } else if (hasPrefix(expr, "transport.")) {
      type = "transport";
      size_t dot_pos = expr.find('.', 10);  // Find dot after "transport."
      if (dot_pos != std::string::npos || eq_pos != std::string::npos) {
        size_t end_pos =
            std::min(dot_pos == std::string::npos ? expr.length() : dot_pos,
                     eq_pos == std::string::npos ? expr.length() : eq_pos);
        key = expr.substr(10, end_pos - 10);  // Extract type/name
        if (eq_pos != std::string::npos) {
          value = expr.substr(eq_pos + 1);
        }
        return true;
      }
    } else if (hasPrefix(expr, "route.")) {
      type = "route";
      size_t route_key_end =
          eq_pos != std::string::npos ? eq_pos : expr.length();
      key = expr.substr(6, route_key_end - 6);  // Remove "route." prefix
      if (eq_pos != std::string::npos) {
        value = expr.substr(eq_pos + 1);
      }
      return true;
    }
  } else if (hasPrefix(expr, "node.metadata.") ||
             hasPrefix(expr, "transport.") || hasPrefix(expr, "route.")) {
    // Handle predicates without colon
    return parsePredicateExpr(expr + ":", type, key, value);
  }

  return false;
}

std::string PredicateEvaluator::getEnvVar(const std::string& var_name) const {
  // Check cache first
  auto it = env_cache_.find(var_name);
  if (it != env_cache_.end()) {
    return it->second;
  }

  // Get from environment
  const char* value = std::getenv(var_name.c_str());
  std::string result = value ? value : "";

  // Cache the result
  env_cache_[var_name] = result;

  // Don't log the actual value for security
  GOPHER_LOG(Debug, "Environment variable '{}' lookup: {}", var_name,
             result.empty() ? "not set" : "set");

  return result;
}

bool PredicateEvaluator::hasPrefix(const std::string& str,
                                   const std::string& prefix) const {
  if (prefix.length() > str.length()) {
    return false;
  }
  return str.compare(0, prefix.length(), prefix) == 0;
}

// ============================================================================
// GlobalPredicateEvaluator Implementation
// ============================================================================

PredicateEvaluator& GlobalPredicateEvaluator::instance() {
  static PredicateEvaluator evaluator;
  return evaluator;
}

}  // namespace config
}  // namespace mcp