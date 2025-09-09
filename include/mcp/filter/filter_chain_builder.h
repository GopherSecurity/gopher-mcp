/**
 * @file filter_chain_builder.h
 * @brief Configuration-driven filter chain builder with fluent interface
 */

#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <map>

#include "mcp/core/compat.h"
#include "mcp/json/json_bridge.h"
#include "mcp/network/filter.h"
#include "mcp/filter/filter_registry.h"

namespace mcp {
namespace filter {

/**
 * Filter configuration with metadata
 */
struct FilterConfig {
  std::string name;                        // Filter type name
  json::JsonValue config;                  // Filter-specific configuration
  bool enabled = true;                     // Whether filter is enabled
  std::vector<std::string> dependencies;   // Required dependencies
  json::JsonValue conditions;              // Conditional inclusion rules
  int priority = 0;                        // Ordering priority (lower = earlier)
  
  FilterConfig() : config(json::JsonValue::object()), 
                   conditions(json::JsonValue::object()) {}
  
  FilterConfig(const std::string& n, const json::JsonValue& c)
      : name(n), config(c), conditions(json::JsonValue::object()) {}
};

/**
 * Chain building options
 */
struct ChainBuildOptions {
  bool validate_dependencies = true;       // Validate filter dependencies
  bool allow_missing_optional = true;      // Allow missing optional filters
  bool enable_metrics = true;              // Enable build metrics
  std::chrono::milliseconds max_build_time{10}; // Maximum build time
  std::string ordering_strategy = "auto";  // Ordering strategy: auto, manual, dependency
};

/**
 * Dependency validator interface
 * Will be implemented in Prompt 2.5
 */
class DependencyValidator {
 public:
  virtual ~DependencyValidator() = default;
  
  /**
   * Validate filter dependencies
   * @param filters List of filter configurations
   * @return true if dependencies are satisfied
   */
  virtual bool validate(const std::vector<FilterConfig>& filters) const = 0;
  
  /**
   * Get validation errors
   * @return List of validation error messages
   */
  virtual std::vector<std::string> getErrors() const = 0;
  
  /**
   * Reorder filters based on dependencies
   * @param filters List of filters to reorder
   * @return Reordered list
   */
  virtual std::vector<FilterConfig> reorder(
      const std::vector<FilterConfig>& filters) const = 0;
};

/**
 * Filter chain builder with fluent interface
 * 
 * Provides configuration-driven construction of filter chains with:
 * - Conditional filter inclusion
 * - Dependency resolution
 * - Chain validation
 * - Cloning for per-connection instances
 */
class FilterChainBuilder {
 public:
  FilterChainBuilder();
  ~FilterChainBuilder();
  
  /**
   * Add a filter to the chain
   * @param name Filter type name
   * @param config Filter configuration
   * @return Builder for chaining
   */
  FilterChainBuilder& addFilter(const std::string& name,
                                const json::JsonValue& config);
  
  /**
   * Add a filter with full configuration
   * @param filter_config Complete filter configuration
   * @return Builder for chaining
   */
  FilterChainBuilder& addFilter(const FilterConfig& filter_config);
  
  /**
   * Conditionally add a filter
   * @param condition Condition to evaluate
   * @param name Filter type name
   * @param config Filter configuration
   * @return Builder for chaining
   */
  FilterChainBuilder& addFilterIf(bool condition,
                                  const std::string& name,
                                  const json::JsonValue& config);
  
  /**
   * Add filter if condition in JSON evaluates to true
   * @param conditions JSON conditions to evaluate
   * @param name Filter type name
   * @param config Filter configuration
   * @return Builder for chaining
   */
  FilterChainBuilder& addFilterIf(const json::JsonValue& conditions,
                                  const std::string& name,
                                  const json::JsonValue& config);
  
  /**
   * Set dependency validator
   * @param validator Dependency validator instance
   * @return Builder for chaining
   */
  FilterChainBuilder& withDependencyValidator(
      std::shared_ptr<DependencyValidator> validator);
  
  /**
   * Set build options
   * @param options Build options
   * @return Builder for chaining
   */
  FilterChainBuilder& withOptions(const ChainBuildOptions& options);
  
  /**
   * Set ordering strategy
   * @param strategy Ordering strategy name
   * @return Builder for chaining
   */
  FilterChainBuilder& withOrdering(const std::string& strategy);
  
  /**
   * Load configuration from JSON
   * @param config JSON configuration
   * @return Builder for chaining
   */
  FilterChainBuilder& fromConfig(const json::JsonValue& config);
  
  /**
   * Clear all filters
   * @return Builder for chaining
   */
  FilterChainBuilder& clear();
  
  /**
   * Validate the filter chain
   * @return true if valid, false otherwise
   */
  bool validate() const;
  
  /**
   * Get validation errors
   * @return List of validation error messages
   */
  std::vector<std::string> getValidationErrors() const;
  
  /**
   * Build the filter chain
   * @param filter_manager Target filter manager
   * @return true if successful
   */
  bool build(network::FilterManager& filter_manager) const;
  
  /**
   * Build and return filter list without installing
   * @return List of created filters
   */
  std::vector<network::FilterSharedPtr> buildFilters() const;
  
  /**
   * Clone the builder for connection-specific instance
   * @return New builder instance with same configuration
   */
  std::unique_ptr<FilterChainBuilder> clone() const;
  
  /**
   * Get filter count
   * @return Number of filters in chain
   */
  size_t getFilterCount() const { return filters_.size(); }
  
  /**
   * Get enabled filter count
   * @return Number of enabled filters
   */
  size_t getEnabledFilterCount() const;
  
  /**
   * Get filter configurations
   * @return List of filter configurations
   */
  const std::vector<FilterConfig>& getFilters() const { return filters_; }

 private:
  /**
   * Evaluate JSON conditions
   * @param conditions Conditions to evaluate
   * @return true if conditions are met
   */
  bool evaluateConditions(const json::JsonValue& conditions) const;
  
  /**
   * Check if filter is enabled
   * @param filter Filter configuration
   * @return true if enabled
   */
  bool isFilterEnabled(const FilterConfig& filter) const;
  
  /**
   * Apply ordering strategy
   * @return Ordered list of filters
   */
  std::vector<FilterConfig> applyOrdering() const;
  
  /**
   * Validate dependencies
   * @param ordered_filters Ordered filter list
   * @return true if dependencies are satisfied
   */
  bool validateDependencies(const std::vector<FilterConfig>& ordered_filters) const;
  
  /**
   * Create filter instance
   * @param filter_config Filter configuration
   * @return Created filter or nullptr on failure
   */
  network::FilterSharedPtr createFilter(const FilterConfig& filter_config) const;

 private:
  std::vector<FilterConfig> filters_;
  ChainBuildOptions options_;
  std::shared_ptr<DependencyValidator> dependency_validator_;
  mutable std::vector<std::string> validation_errors_;
  
  // Build metrics
  mutable std::chrono::steady_clock::time_point build_start_;
  mutable std::chrono::milliseconds last_build_time_{0};
};

/**
 * Configurable filter chain factory
 * 
 * Replaces hardcoded filter chain factories with configuration-driven approach
 */
class ConfigurableFilterChainFactory : public network::FilterChainFactory {
 public:
  ConfigurableFilterChainFactory();
  explicit ConfigurableFilterChainFactory(const json::JsonValue& config);
  ~ConfigurableFilterChainFactory() override;
  
  /**
   * Load configuration from JSON
   * @param config JSON configuration
   * @return true if successful
   */
  bool loadConfig(const json::JsonValue& config);
  
  /**
   * Validate configuration without building
   * @param config Configuration to validate
   * @return true if valid
   */
  static bool validateConfiguration(const json::JsonValue& config);
  
  /**
   * Create filter chain for connection
   * @param filter_manager Target filter manager
   * @return true if successful
   */
  bool createFilterChain(network::FilterManager& filter_manager) const override;
  
  /**
   * Create network filter chain
   * @param filter_manager Target filter manager
   * @param factories Additional filter factories
   * @return true if successful
   */
  bool createNetworkFilterChain(
      network::FilterManager& filter_manager,
      const std::vector<network::FilterFactoryCb>& factories) const override;
  
  /**
   * Create listener filter chain (not implemented)
   * @param filter_manager Target filter manager
   * @return false (not implemented)
   */
  bool createListenerFilterChain(
      network::FilterManager& filter_manager) const override {
    return false;
  }
  
  /**
   * Get builder for customization
   * @return Filter chain builder
   */
  FilterChainBuilder& getBuilder() { return *builder_; }
  const FilterChainBuilder& getBuilder() const { return *builder_; }
  
  /**
   * Clone for connection-specific instance
   * @return New factory instance
   */
  std::unique_ptr<ConfigurableFilterChainFactory> clone() const;

 private:
  std::unique_ptr<FilterChainBuilder> builder_;
  json::JsonValue config_;
  
  // Cache for performance
  mutable std::vector<network::FilterSharedPtr> cached_filters_;
  mutable bool cache_valid_ = false;
};

}  // namespace filter
}  // namespace mcp