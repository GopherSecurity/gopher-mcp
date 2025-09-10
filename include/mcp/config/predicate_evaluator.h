/**
 * @file predicate_evaluator.h
 * @brief Predicate evaluation system for conditional filter inclusion
 *
 * This file defines the predicate evaluation system that allows filters to be
 * conditionally included based on environment variables, node metadata,
 * transport type/name, and route paths.
 */

#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "mcp/json/json_bridge.h"

namespace mcp {
namespace config {

/**
 * @brief Context for predicate evaluation
 *
 * Contains all the information needed to evaluate predicates including
 * environment variables, node metadata, transport info, and route details.
 */
struct PredicateContext {
  /// Node metadata key-value pairs
  std::map<std::string, std::string> node_metadata;
  
  /// Transport type (e.g., "tcp", "http", "https", "stdio")
  std::string transport_type;
  
  /// Transport name (user-defined identifier)
  std::string transport_name;
  
  /// Route path (e.g., "/health", "/admin/status")
  std::string route_path;
  
  /// Additional context data as JSON
  json::JsonValue extra_context = json::JsonValue::object();
  
  /**
   * @brief Create from JSON configuration
   */
  static PredicateContext fromJson(const json::JsonValue& j);
  
  /**
   * @brief Convert to JSON
   */
  json::JsonValue toJson() const;
};

/**
 * @brief Result of predicate evaluation
 */
struct PredicateResult {
  /// Whether the predicate evaluated to true
  bool matched = false;
  
  /// Whether evaluation succeeded (false on errors)
  bool success = true;
  
  /// Error message if evaluation failed
  std::string error_message;
  
  /// Predicate ID for logging
  std::string predicate_id;
  
  PredicateResult() = default;
  PredicateResult(bool m, bool s, const std::string& err = "")
      : matched(m), success(s), error_message(err) {}
};

/**
 * @brief Predicate evaluator for conditional filter inclusion
 *
 * Evaluates predicates to determine whether filters should be enabled based on:
 * - Environment variables (env:VAR=value or env:VAR for existence)
 * - Node metadata (node.metadata.key=value)
 * - Transport type (transport.type=http)
 * - Transport name (transport.name=frontend)
 * - Route path (route.path=/health or route.path.prefix=/admin)
 *
 * Example predicate configurations:
 * ```json
 * {
 *   "enabled_when": {
 *     "env": "DEBUG=true",
 *     "node.metadata.env": "production",
 *     "transport.type": "http"
 *   }
 * }
 * ```
 */
class PredicateEvaluator {
 public:
  PredicateEvaluator();
  ~PredicateEvaluator();
  
  /**
   * @brief Evaluate a single predicate
   * @param predicate_expr Predicate expression (e.g., "env:DEBUG=true")
   * @param context Evaluation context
   * @return Evaluation result
   */
  PredicateResult evaluateSingle(const std::string& predicate_expr,
                                 const PredicateContext& context) const;
  
  /**
   * @brief Evaluate JSON predicate configuration
   * @param predicates JSON object with predicate definitions
   * @param context Evaluation context
   * @return Evaluation result (all predicates must match for true)
   */
  PredicateResult evaluate(const json::JsonValue& predicates,
                          const PredicateContext& context) const;
  
  /**
   * @brief Check if filter is enabled based on configuration
   * @param filter_config Filter configuration with enabled/enabled_when fields
   * @param context Evaluation context
   * @return true if filter should be enabled
   */
  bool isFilterEnabled(const json::JsonValue& filter_config,
                      const PredicateContext& context) const;
  
  /**
   * @brief Check if filters should be bypassed for a route
   * @param route_config Route configuration with bypass_filters field
   * @param context Evaluation context
   * @return true if filters should be bypassed
   */
  bool shouldBypassFilters(const json::JsonValue& route_config,
                          const PredicateContext& context) const;
  
  /**
   * @brief Validate predicate syntax without evaluation
   * @param predicates Predicates to validate
   * @return Vector of validation errors (empty if valid)
   */
  std::vector<std::string> validatePredicates(
      const json::JsonValue& predicates) const;
  
  /**
   * @brief Set whether to use safe defaults on evaluation errors
   * @param safe If true, evaluation errors result in disabled filters
   */
  void setSafeDefaults(bool safe) { safe_defaults_ = safe; }
  
  /**
   * @brief Get whether safe defaults are enabled
   */
  bool getSafeDefaults() const { return safe_defaults_; }

 private:
  /**
   * @brief Evaluate environment variable predicate
   * @param predicate Environment predicate (e.g., "DEBUG=true" or "DEBUG")
   * @return Evaluation result
   */
  PredicateResult evaluateEnvPredicate(const std::string& predicate) const;
  
  /**
   * @brief Evaluate node metadata predicate
   * @param key Metadata key
   * @param expected_value Expected value (empty for existence check)
   * @param context Evaluation context
   * @return Evaluation result
   */
  PredicateResult evaluateNodePredicate(const std::string& key,
                                       const std::string& expected_value,
                                       const PredicateContext& context) const;
  
  /**
   * @brief Evaluate transport predicate
   * @param predicate_type "type" or "name"
   * @param expected_value Expected value
   * @param context Evaluation context
   * @return Evaluation result
   */
  PredicateResult evaluateTransportPredicate(const std::string& predicate_type,
                                            const std::string& expected_value,
                                            const PredicateContext& context) const;
  
  /**
   * @brief Evaluate route predicate
   * @param predicate_type "path" or "path.prefix"
   * @param expected_value Expected value
   * @param context Evaluation context
   * @return Evaluation result
   */
  PredicateResult evaluateRoutePredicate(const std::string& predicate_type,
                                        const std::string& expected_value,
                                        const PredicateContext& context) const;
  
  /**
   * @brief Parse predicate expression into type and value
   * @param expr Predicate expression
   * @param type Output: predicate type
   * @param key Output: predicate key
   * @param value Output: expected value (empty for existence checks)
   * @return true if successfully parsed
   */
  bool parsePredicateExpr(const std::string& expr,
                         std::string& type,
                         std::string& key,
                         std::string& value) const;
  
  /**
   * @brief Get environment variable value
   * @param var_name Variable name
   * @return Variable value or empty string if not set
   */
  std::string getEnvVar(const std::string& var_name) const;
  
  /**
   * @brief Check if string matches prefix
   * @param str String to check
   * @param prefix Prefix to match
   * @return true if str starts with prefix
   */
  bool hasPrefix(const std::string& str, const std::string& prefix) const;

 private:
  /// Whether to use safe defaults on evaluation errors
  bool safe_defaults_ = true;
  
  /// Cache for environment variables (optimization)
  mutable std::map<std::string, std::string> env_cache_;
  
  /// Whether environment cache is populated
  mutable bool env_cache_populated_ = false;
};

/**
 * @brief Global predicate evaluator instance
 *
 * Provides a singleton evaluator for use throughout the configuration system
 */
class GlobalPredicateEvaluator {
 public:
  /**
   * @brief Get the global predicate evaluator instance
   */
  static PredicateEvaluator& instance();
  
 private:
  GlobalPredicateEvaluator() = default;
  ~GlobalPredicateEvaluator() = default;
  GlobalPredicateEvaluator(const GlobalPredicateEvaluator&) = delete;
  GlobalPredicateEvaluator& operator=(const GlobalPredicateEvaluator&) = delete;
};

}  // namespace config
}  // namespace mcp