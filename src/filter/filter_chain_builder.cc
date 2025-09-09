/**
 * @file filter_chain_builder.cc
 * @brief Implementation of configuration-driven filter chain builder
 */

#include "mcp/filter/filter_chain_builder.h"

#include <algorithm>
#include <set>
#include <sstream>
#include <set>

#include "mcp/json/json_serialization.h"

// Must undefine before redefining
#ifdef GOPHER_LOG_COMPONENT
#undef GOPHER_LOG_COMPONENT
#endif

#include "mcp/logging/log_macros.h"

#define GOPHER_LOG_COMPONENT "config.chainbuilder"

namespace mcp {
namespace filter {

// ============================================================================
// FilterChainBuilder Implementation
// ============================================================================

FilterChainBuilder::FilterChainBuilder() {
  GOPHER_LOG(Debug, "FilterChainBuilder created");
}

FilterChainBuilder::~FilterChainBuilder() {
  GOPHER_LOG(Debug, "FilterChainBuilder destroyed");
}

FilterChainBuilder& FilterChainBuilder::addFilter(const std::string& name,
                                                  const json::JsonValue& config) {
  FilterConfig filter_config(name, config);
  return addFilter(filter_config);
}

FilterChainBuilder& FilterChainBuilder::addFilter(const FilterConfig& filter_config) {
  GOPHER_LOG(Debug, "Adding filter '{}' to chain", filter_config.name);
  
  // Check if filter type exists in registry
  if (!FilterRegistry::instance().hasFactory(filter_config.name)) {
    GOPHER_LOG(Warning, "Filter type '{}' not found in registry", filter_config.name);
  }
  
  filters_.push_back(filter_config);
  
  GOPHER_LOG(Info, "Filter '{}' added to chain (total: {})", 
             filter_config.name, filters_.size());
  
  return *this;
}

FilterChainBuilder& FilterChainBuilder::addFilterIf(bool condition,
                                                    const std::string& name,
                                                    const json::JsonValue& config) {
  if (condition) {
    GOPHER_LOG(Debug, "Condition met, adding filter '{}'", name);
    return addFilter(name, config);
  } else {
    GOPHER_LOG(Debug, "Condition not met, skipping filter '{}'", name);
    return *this;
  }
}

FilterChainBuilder& FilterChainBuilder::addFilterIf(const json::JsonValue& conditions,
                                                    const std::string& name,
                                                    const json::JsonValue& config) {
  bool condition_met = evaluateConditions(conditions);
  
  if (condition_met) {
    GOPHER_LOG(Debug, "JSON conditions met, adding filter '{}'", name);
    return addFilter(name, config);
  } else {
    GOPHER_LOG(Info, "Conditional filter '{}' pruned", name);
    return *this;
  }
}

FilterChainBuilder& FilterChainBuilder::withDependencyValidator(
    std::shared_ptr<DependencyValidator> validator) {
  GOPHER_LOG(Debug, "Setting dependency validator");
  dependency_validator_ = validator;
  return *this;
}

FilterChainBuilder& FilterChainBuilder::withOptions(const ChainBuildOptions& options) {
  GOPHER_LOG(Debug, "Setting build options");
  options_ = options;
  return *this;
}

FilterChainBuilder& FilterChainBuilder::withOrdering(const std::string& strategy) {
  GOPHER_LOG(Debug, "Setting ordering strategy: {}", strategy);
  options_.ordering_strategy = strategy;
  return *this;
}

FilterChainBuilder& FilterChainBuilder::fromConfig(const json::JsonValue& config) {
  GOPHER_LOG(Info, "Loading filter chain from configuration");
  
  if (!config.isObject()) {
    GOPHER_LOG(Error, "Filter chain configuration must be an object");
    return *this;
  }
  
  // Clear existing filters
  clear();
  
  // Load build options if present
  if (config.contains("options") && config["options"].isObject()) {
    auto opts = config["options"];
    
    if (opts.contains("validate_dependencies")) {
      options_.validate_dependencies = opts["validate_dependencies"].getBool();
    }
    if (opts.contains("allow_missing_optional")) {
      options_.allow_missing_optional = opts["allow_missing_optional"].getBool();
    }
    if (opts.contains("enable_metrics")) {
      options_.enable_metrics = opts["enable_metrics"].getBool();
    }
    if (opts.contains("max_build_time_ms")) {
      options_.max_build_time = std::chrono::milliseconds(
          opts["max_build_time_ms"].getInt());
    }
    if (opts.contains("ordering_strategy")) {
      options_.ordering_strategy = opts["ordering_strategy"].getString();
    }
  }
  
  // Load filters
  if (config.contains("filters") && config["filters"].isArray()) {
    auto filters = config["filters"];
    size_t filter_count = filters.size();
    
    GOPHER_LOG(Debug, "Loading {} filters from configuration", filter_count);
    
    for (size_t i = 0; i < filter_count; ++i) {
      auto filter_json = filters[i];
      
      if (!filter_json.isObject()) {
        GOPHER_LOG(Warning, "Skipping invalid filter configuration at index {}", i);
        continue;
      }
      
      FilterConfig filter_config;
      
      // Required: name
      if (!filter_json.contains("name") || !filter_json["name"].isString()) {
        GOPHER_LOG(Warning, "Filter at index {} missing 'name' field", i);
        continue;
      }
      filter_config.name = filter_json["name"].getString();
      
      // Optional: config
      if (filter_json.contains("config")) {
        filter_config.config = filter_json["config"];
      }
      
      // Optional: enabled
      if (filter_json.contains("enabled")) {
        filter_config.enabled = filter_json["enabled"].getBool(true);
      }
      
      // Optional: dependencies
      if (filter_json.contains("dependencies") && filter_json["dependencies"].isArray()) {
        auto deps = filter_json["dependencies"];
        for (size_t j = 0; j < deps.size(); ++j) {
          if (deps[j].isString()) {
            filter_config.dependencies.push_back(deps[j].getString());
          }
        }
      }
      
      // Optional: conditions
      if (filter_json.contains("conditions")) {
        filter_config.conditions = filter_json["conditions"];
      }
      
      // Optional: priority
      if (filter_json.contains("priority")) {
        filter_config.priority = filter_json["priority"].getInt(0);
      }
      
      // Check conditions before adding
      if (!filter_config.conditions.isNull() && !filter_config.conditions.empty()) {
        if (!evaluateConditions(filter_config.conditions)) {
          GOPHER_LOG(Info, "Conditional filter '{}' pruned based on conditions", 
                     filter_config.name);
          continue;
        }
      }
      
      // Add filter if enabled
      if (filter_config.enabled) {
        filters_.push_back(filter_config);
        GOPHER_LOG(Debug, "Loaded filter '{}' from configuration", filter_config.name);
      } else {
        GOPHER_LOG(Info, "Filter '{}' disabled in configuration", filter_config.name);
      }
    }
  }
  
  GOPHER_LOG(Info, "Loaded {} filters from configuration", filters_.size());
  
  return *this;
}

FilterChainBuilder& FilterChainBuilder::clear() {
  GOPHER_LOG(Debug, "Clearing all filters");
  filters_.clear();
  validation_errors_.clear();
  return *this;
}

bool FilterChainBuilder::validate() const {
  validation_errors_.clear();
  
  GOPHER_LOG(Debug, "Validating filter chain with {} filters", filters_.size());
  
  // Check for empty chain
  if (filters_.empty()) {
    validation_errors_.push_back("Filter chain is empty");
    GOPHER_LOG(Warning, "Filter chain validation failed: empty chain");
    return false;
  }
  
  // Check all filter types exist
  for (const auto& filter : filters_) {
    if (!FilterRegistry::instance().hasFactory(filter.name)) {
      std::string error = "Unknown filter type: " + filter.name;
      validation_errors_.push_back(error);
      GOPHER_LOG(Error, "Validation failed: {}", error);
    }
  }
  
  // Apply ordering and validate dependencies
  auto ordered_filters = applyOrdering();
  
  if (options_.validate_dependencies) {
    if (!validateDependencies(ordered_filters)) {
      GOPHER_LOG(Error, "Filter chain validation failed: dependency errors");
      return false;
    }
  }
  
  // Use external validator if provided
  if (dependency_validator_) {
    if (!dependency_validator_->validate(ordered_filters)) {
      auto errors = dependency_validator_->getErrors();
      validation_errors_.insert(validation_errors_.end(), errors.begin(), errors.end());
      GOPHER_LOG(Error, "External dependency validation failed");
      return false;
    }
  }
  
  bool valid = validation_errors_.empty();
  
  if (valid) {
    GOPHER_LOG(Info, "Filter chain validation successful");
  } else {
    GOPHER_LOG(Error, "Filter chain validation failed with {} errors", 
               validation_errors_.size());
  }
  
  return valid;
}

std::vector<std::string> FilterChainBuilder::getValidationErrors() const {
  return validation_errors_;
}

bool FilterChainBuilder::build(network::FilterManager& filter_manager) const {
  if (options_.enable_metrics) {
    build_start_ = std::chrono::steady_clock::now();
  }
  
  GOPHER_LOG(Info, "Building filter chain with {} filters", filters_.size());
  
  // Validate first
  if (!validate()) {
    GOPHER_LOG(Error, "Cannot build invalid filter chain");
    return false;
  }
  
  // Apply ordering
  auto ordered_filters = applyOrdering();
  
  // Apply dependency validator reordering if available
  if (dependency_validator_) {
    ordered_filters = dependency_validator_->reorder(ordered_filters);
  }
  
  // Create and add filters
  size_t created_count = 0;
  size_t pruned_count = 0;
  
  for (const auto& filter_config : ordered_filters) {
    if (!isFilterEnabled(filter_config)) {
      GOPHER_LOG(Info, "Conditional filter '{}' pruned", filter_config.name);
      pruned_count++;
      continue;
    }
    
    try {
      auto filter = createFilter(filter_config);
      
      if (filter) {
        filter_manager.addFilter(filter);
        created_count++;
        GOPHER_LOG(Debug, "Added filter '{}' to manager", filter_config.name);
      } else {
        GOPHER_LOG(Warning, "Filter '{}' creation returned nullptr", filter_config.name);
      }
      
    } catch (const std::exception& e) {
      GOPHER_LOG(Error, "Failed to create filter '{}': {}", 
                 filter_config.name, e.what());
      
      if (!options_.allow_missing_optional) {
        return false;
      }
    }
  }
  
  if (options_.enable_metrics) {
    auto build_end = std::chrono::steady_clock::now();
    last_build_time_ = std::chrono::duration_cast<std::chrono::milliseconds>(
        build_end - build_start_);
    
    if (last_build_time_ > options_.max_build_time) {
      GOPHER_LOG(Warning, "Chain build time {}ms exceeded limit {}ms",
                 last_build_time_.count(), options_.max_build_time.count());
    }
  }
  
  GOPHER_LOG(Info, "Filter chain built successfully: {} filters created, {} pruned",
             created_count, pruned_count);
  
  return true;
}

std::vector<network::FilterSharedPtr> FilterChainBuilder::buildFilters() const {
  std::vector<network::FilterSharedPtr> result;
  
  GOPHER_LOG(Debug, "Building filter list without installing");
  
  // Validate first
  if (!validate()) {
    GOPHER_LOG(Error, "Cannot build filters from invalid chain");
    return result;
  }
  
  // Apply ordering
  auto ordered_filters = applyOrdering();
  
  // Apply dependency validator reordering if available
  if (dependency_validator_) {
    ordered_filters = dependency_validator_->reorder(ordered_filters);
  }
  
  // Create filters
  for (const auto& filter_config : ordered_filters) {
    if (!isFilterEnabled(filter_config)) {
      continue;
    }
    
    try {
      auto filter = createFilter(filter_config);
      if (filter) {
        result.push_back(filter);
      }
    } catch (const std::exception& e) {
      GOPHER_LOG(Error, "Failed to create filter '{}': {}", 
                 filter_config.name, e.what());
      
      if (!options_.allow_missing_optional) {
        result.clear();
        return result;
      }
    }
  }
  
  return result;
}

std::unique_ptr<FilterChainBuilder> FilterChainBuilder::clone() const {
  auto cloned = std::make_unique<FilterChainBuilder>();
  
  cloned->filters_ = filters_;
  cloned->options_ = options_;
  cloned->dependency_validator_ = dependency_validator_;
  
  GOPHER_LOG(Debug, "Cloned filter chain builder with {} filters", filters_.size());
  
  return cloned;
}

size_t FilterChainBuilder::getEnabledFilterCount() const {
  return std::count_if(filters_.begin(), filters_.end(),
                       [this](const FilterConfig& f) { return isFilterEnabled(f); });
}

bool FilterChainBuilder::evaluateConditions(const json::JsonValue& conditions) const {
  if (!conditions.isObject()) {
    return true;  // No conditions means always include
  }
  
  // Simple condition evaluation - can be extended
  // Example conditions:
  // { "environment": "production", "feature_flag": "metrics_enabled" }
  
  if (conditions.contains("enabled")) {
    return conditions["enabled"].getBool(true);
  }
  
  if (conditions.contains("environment")) {
    // Check against actual environment - simplified for now
    std::string required_env = conditions["environment"].getString();
    // In real implementation, get actual environment from config
    std::string current_env = "production";
    if (required_env != current_env) {
      return false;
    }
  }
  
  // Add more condition types as needed
  
  return true;
}

bool FilterChainBuilder::isFilterEnabled(const FilterConfig& filter) const {
  if (!filter.enabled) {
    return false;
  }
  
  if (!filter.conditions.isNull() && !filter.conditions.empty()) {
    return evaluateConditions(filter.conditions);
  }
  
  return true;
}

std::vector<FilterConfig> FilterChainBuilder::applyOrdering() const {
  std::vector<FilterConfig> ordered = filters_;
  
  if (options_.ordering_strategy == "manual") {
    // Keep original order
    GOPHER_LOG(Debug, "Using manual filter ordering");
    
  } else if (options_.ordering_strategy == "priority") {
    // Sort by priority
    std::sort(ordered.begin(), ordered.end(),
             [](const FilterConfig& a, const FilterConfig& b) {
               return a.priority < b.priority;
             });
    GOPHER_LOG(Debug, "Ordered filters by priority");
    
  } else if (options_.ordering_strategy == "dependency") {
    // Use dependency validator if available
    if (dependency_validator_) {
      ordered = dependency_validator_->reorder(ordered);
      GOPHER_LOG(Debug, "Ordered filters by dependencies");
    }
    
  } else {  // "auto" or default
    // Auto ordering based on known filter types
    // HTTP codec should come before SSE, etc.
    std::sort(ordered.begin(), ordered.end(),
             [](const FilterConfig& a, const FilterConfig& b) {
               // Define order for known filter types
               static const std::map<std::string, int> order_map = {
                 {"circuit_breaker", 10},
                 {"rate_limit", 20},
                 {"metrics", 30},
                 {"request_validation", 40},
                 {"backpressure", 50},
                 {"http_codec", 60},
                 {"sse_codec", 70},
                 {"json_rpc_protocol", 80}
               };
               
               auto get_order = [](const std::string& name) {
                 auto it = order_map.find(name);
                 return (it != order_map.end()) ? it->second : 100;
               };
               
               int order_a = get_order(a.name);
               int order_b = get_order(b.name);
               
               if (order_a != order_b) {
                 return order_a < order_b;
               }
               
               // Fall back to priority if same order
               return a.priority < b.priority;
             });
    GOPHER_LOG(Debug, "Applied automatic filter ordering");
  }
  
  return ordered;
}

bool FilterChainBuilder::validateDependencies(
    const std::vector<FilterConfig>& ordered_filters) const {
  
  // Build set of available filters
  std::set<std::string> available;
  
  for (const auto& filter : ordered_filters) {
    // Check if all dependencies are satisfied
    for (const auto& dep : filter.dependencies) {
      if (available.find(dep) == available.end()) {
        std::string error = "Filter '" + filter.name + 
                           "' depends on '" + dep + 
                           "' which is not available or comes later in chain";
        validation_errors_.push_back(error);
        GOPHER_LOG(Error, "Missing dependency: {}", error);
        return false;
      }
    }
    
    // Add this filter to available set
    available.insert(filter.name);
  }
  
  return true;
}

network::FilterSharedPtr FilterChainBuilder::createFilter(
    const FilterConfig& filter_config) const {
  
  try {
    auto filter = FilterRegistry::instance().createFilter(
        filter_config.name, filter_config.config);
    
    if (!filter) {
      // Factory returned nullptr - this is expected for factories that need
      // runtime dependencies (callbacks, dispatcher, etc.)
      GOPHER_LOG(Debug, "Filter factory '{}' returned nullptr (needs runtime deps)",
                 filter_config.name);
    }
    
    return filter;
    
  } catch (const std::exception& e) {
    GOPHER_LOG(Error, "Failed to create filter '{}': {}", 
               filter_config.name, e.what());
    throw;
  }
}

// ============================================================================
// ConfigurableFilterChainFactory Implementation
// ============================================================================

ConfigurableFilterChainFactory::ConfigurableFilterChainFactory()
    : builder_(std::make_unique<FilterChainBuilder>()),
      config_(json::JsonValue::object()) {
  GOPHER_LOG(Debug, "ConfigurableFilterChainFactory created");
}

ConfigurableFilterChainFactory::ConfigurableFilterChainFactory(
    const json::JsonValue& config)
    : builder_(std::make_unique<FilterChainBuilder>()),
      config_(config) {
  GOPHER_LOG(Debug, "ConfigurableFilterChainFactory created with config");
  loadConfig(config);
}

ConfigurableFilterChainFactory::~ConfigurableFilterChainFactory() {
  GOPHER_LOG(Debug, "ConfigurableFilterChainFactory destroyed");
}

bool ConfigurableFilterChainFactory::loadConfig(const json::JsonValue& config) {
  GOPHER_LOG(Info, "Loading filter chain factory configuration");
  
  config_ = config;
  cache_valid_ = false;
  
  builder_->fromConfig(config);
  
  if (!builder_->validate()) {
    auto errors = builder_->getValidationErrors();
    for (const auto& error : errors) {
      GOPHER_LOG(Error, "Configuration error: {}", error);
    }
    return false;
  }
  
  GOPHER_LOG(Info, "Configuration loaded successfully with {} filters",
             builder_->getFilterCount());
  
  return true;
}

bool ConfigurableFilterChainFactory::validateConfiguration(
    const json::JsonValue& config) {
  
  GOPHER_LOG(Debug, "Validating filter chain configuration");
  
  FilterChainBuilder temp_builder;
  temp_builder.fromConfig(config);
  
  return temp_builder.validate();
}

bool ConfigurableFilterChainFactory::createFilterChain(
    network::FilterManager& filter_manager) const {
  
  GOPHER_LOG(Info, "Creating filter chain from configuration");
  
  // Clone builder for this specific connection
  auto connection_builder = builder_->clone();
  
  // Build the chain
  bool success = connection_builder->build(filter_manager);
  
  if (!success) {
    GOPHER_LOG(Error, "Failed to create filter chain");
    auto errors = connection_builder->getValidationErrors();
    for (const auto& error : errors) {
      GOPHER_LOG(Error, "Chain build error: {}", error);
    }
  }
  
  return success;
}

bool ConfigurableFilterChainFactory::createNetworkFilterChain(
    network::FilterManager& filter_manager,
    const std::vector<network::FilterFactoryCb>& factories) const {
  
  GOPHER_LOG(Info, "Creating network filter chain with {} additional factories",
             factories.size());
  
  // First create configured filters
  if (!createFilterChain(filter_manager)) {
    return false;
  }
  
  // Then add any additional filters from factories
  for (const auto& factory : factories) {
    try {
      auto filter = factory();
      if (filter) {
        filter_manager.addFilter(filter);
      }
    } catch (const std::exception& e) {
      GOPHER_LOG(Error, "Failed to create filter from factory: {}", e.what());
      return false;
    }
  }
  
  return true;
}

std::unique_ptr<ConfigurableFilterChainFactory> 
ConfigurableFilterChainFactory::clone() const {
  
  auto cloned = std::make_unique<ConfigurableFilterChainFactory>();
  cloned->config_ = config_;
  cloned->builder_ = builder_->clone();
  
  GOPHER_LOG(Debug, "Cloned filter chain factory");
  
  return cloned;
}

}  // namespace filter
}  // namespace mcp