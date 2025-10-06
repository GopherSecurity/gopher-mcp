/**
 * @file filter_order_validator.h
 * @brief Filter ordering model and constraints validator
 * 
 * Enforces a normative ordering model with stage taxonomy and explicit
 * constraints for safe filter chains. Ensures filters are properly ordered
 * according to their stages (Security → QoS → Observability → Protocol → Application)
 * and validates dependencies and ordering constraints.
 */

#pragma once

#include <memory>
#include <string>
#include <vector>
#include <map>
#include <set>

#include "mcp/core/compat.h"
#include "mcp/json/json_bridge.h"
// TODO : Update filter order validator for new architecture
// The filter order validator needs to be updated to work with the new
// FilterChainAssembler architecture instead of the old filter_chain_builder
// #include "mcp/filter/filter_chain_assembler.h"

namespace mcp {
namespace config {

// TODO : Restore entire FilterOrderValidator system for new architecture
#if 0

/**
 * Filter stage taxonomy
 * 
 * Defines the normative ordering of filter stages in a chain.
 * Filters must be ordered by increasing stage number.
 */
enum class FilterStage {
  Security = 0,      // Authentication, authorization, rate limiting
  QoS = 1,          // Circuit breaker, backpressure, flow control
  Observability = 2, // Metrics, tracing, logging
  Protocol = 3,      // HTTP codec, SSE codec, protocol parsing
  Application = 4    // JSON-RPC, business logic, custom filters
};

/**
 * Filter ordering metadata
 * 
 * Defines the stage and constraints for a filter type.
 */
struct FilterOrderMetadata {
  std::string filter_type;              // Filter type identifier
  FilterStage stage;                    // Stage in the processing pipeline
  std::vector<std::string> requires;    // Must come after these filter types
  std::vector<std::string> before;      // Must come before these filter types
  std::vector<std::string> after;       // Must come after these filter types
  bool optional = false;                // Whether the filter is optional
  
  FilterOrderMetadata() : stage(FilterStage::Application) {}
  
  FilterOrderMetadata(const std::string& type, FilterStage s)
      : filter_type(type), stage(s) {}
};

/**
 * Validation result with detailed error information
 */
struct ValidationResult {
  bool valid = true;
  std::vector<std::string> errors;
  std::vector<std::string> warnings;
  
  void addError(const std::string& error) {
    valid = false;
    errors.push_back(error);
  }
  
  void addWarning(const std::string& warning) {
    warnings.push_back(warning);
  }
  
  void merge(const ValidationResult& other) {
    if (!other.valid) {
      valid = false;
    }
    errors.insert(errors.end(), other.errors.begin(), other.errors.end());
    warnings.insert(warnings.end(), other.warnings.begin(), other.warnings.end());
  }
};

/**
 * Transport-specific ordering rules
 */
enum class TransportType {
  Generic,     // Default ordering rules
  HTTP,        // HTTP-specific ordering
  HTTPWithSSE, // HTTP with Server-Sent Events
  WebSocket,   // WebSocket-specific ordering
  TCP,         // Raw TCP ordering
  Stdio        // Standard I/O ordering
};

// TODO : Restore FilterOrderValidator class for new architecture
/*
 * Filter order validator
 *
 * Validates filter chains against stage taxonomy and explicit constraints.
 * Provides transport-specific validation rules and detailed error reporting.
 *
 * TODO : Restore FilterOrderValidator implementation
 */
/*
class FilterOrderValidator : public filter::DependencyValidator {
 public:
  FilterOrderValidator();
  ~FilterOrderValidator() override;
  
  /**
   * Register filter metadata
   * 
   * @param metadata Filter ordering metadata
   */
  void registerFilter(const FilterOrderMetadata& metadata);
  
  /**
   * Set transport type for validation
   * 
   * @param transport Transport type for specific rules
   */
  void setTransportType(TransportType transport);
  
  /**
   * Validate filter chain configuration
   * 
   * @param filters List of filter configurations
   * @return true if valid, false otherwise
   */
  bool validate(const std::vector<filter::FilterConfig>& filters) const override;
  
  /**
   * Validate filter chain from JSON configuration
   * 
   * @param chain_config JSON configuration with chain name and filters
   * @return Validation result with detailed errors
   */
  ValidationResult validateChain(const json::JsonValue& chain_config) const;
  
  /**
   * Get validation errors from last validation
   * 
   * @return List of validation error messages
   */
  std::vector<std::string> getErrors() const override;
  
  /**
   * Reorder filters based on stages and dependencies
   * 
   * @param filters List of filters to reorder
   * @return Reordered list of filters
   */
  std::vector<filter::FilterConfig> reorder(
      const std::vector<filter::FilterConfig>& filters) const override;
  
  /**
   * Check if a filter type is registered
   * 
   * @param filter_type Filter type to check
   * @return true if registered, false otherwise
   */
  bool hasFilter(const std::string& filter_type) const;
  
  /**
   * Get the stage for a filter type
   * 
   * @param filter_type Filter type
   * @return Filter stage
   */
  FilterStage getFilterStage(const std::string& filter_type) const;
  
  /**
   * Initialize with default filter metadata
   * 
   * Registers metadata for all built-in filter types.
   */
  void initializeDefaults();
  
  /**
   * Clear all registered filter metadata
   */
  void clear();
  
  /**
   * Get transport-specific ordering recommendation
   * 
   * @param transport Transport type
   * @return Recommended filter ordering for the transport
   */
  std::vector<std::string> getRecommendedOrdering(TransportType transport) const;

 private:
  /**
   * Validate stage ordering
   * 
   * @param filters List of filters
   * @param chain_name Chain name for error reporting
   * @return Validation result
   */
  ValidationResult validateStageOrdering(
      const std::vector<filter::FilterConfig>& filters,
      const std::string& chain_name) const;
  
  /**
   * Validate filter dependencies
   * 
   * @param filters List of filters
   * @param chain_name Chain name for error reporting
   * @return Validation result
   */
  ValidationResult validateDependencies(
      const std::vector<filter::FilterConfig>& filters,
      const std::string& chain_name) const;
  
  /**
   * Validate before/after constraints
   * 
   * @param filters List of filters
   * @param chain_name Chain name for error reporting
   * @return Validation result
   */
  ValidationResult validateConstraints(
      const std::vector<filter::FilterConfig>& filters,
      const std::string& chain_name) const;
  
  /**
   * Apply transport-specific validation
   * 
   * @param filters List of filters
   * @param chain_name Chain name for error reporting
   * @return Validation result
   */
  ValidationResult validateTransportSpecific(
      const std::vector<filter::FilterConfig>& filters,
      const std::string& chain_name) const;
  
  /**
   * Perform topological sort for dependency ordering
   * 
   * @param filters List of filters
   * @return Sorted list or empty if cycle detected
   */
  std::vector<filter::FilterConfig> topologicalSort(
      const std::vector<filter::FilterConfig>& filters) const;
  
  /**
   * Format error message with location
   * 
   * @param chain_name Chain name
   * @param filter_index Filter index (0-based)
   * @param filter_name Filter name
   * @param message Error message
   * @return Formatted error string
   */
  std::string formatError(const std::string& chain_name,
                          size_t filter_index,
                          const std::string& filter_name,
                          const std::string& message) const;

 private:
  // Filter metadata registry
  std::map<std::string, FilterOrderMetadata> filter_metadata_;
  
  // Transport type for validation
  TransportType transport_type_;
  
  // Last validation errors (mutable for const methods)
  mutable std::vector<std::string> last_errors_;
  
  // Transport-specific recommended orderings
  std::map<TransportType, std::vector<std::string>> transport_orderings_;
  
  // Initialize transport-specific orderings
  void initializeTransportOrderings();
};
*/

*/

// TODO : Restore utility functions for new architecture
/*
 * Create a default filter order validator with built-in rules
 *
 * @return Shared pointer to configured validator
 */
// std::shared_ptr<FilterOrderValidator> createDefaultValidator();

/*
 * Get stage name as string
 *
 * @param stage Filter stage
 * @return Stage name
 */
// const char* getFilterStageName(FilterStage stage);

#endif  // TODO

}  // namespace config
}  // namespace mcp
