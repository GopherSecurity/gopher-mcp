/**
 * @file filter_chain_assembler.h
 * @brief Simple filter chain assembly system for listener configuration
 */

#pragma once

#include <memory>
#include <string>
#include <vector>

#include "mcp/config/listener_config.h"
#include "mcp/filter/filter_context.h"
#include "mcp/filter/filter_registry.h"
#include "mcp/network/filter.h"

namespace mcp {
namespace filter {

struct AssemblyResult {
  bool success = false;
  std::string error_message;
  std::vector<std::string> created_filters;
  std::vector<std::string> warnings;

  AssemblyResult() = default;
  explicit AssemblyResult(bool s) : success(s) {}
  AssemblyResult(bool s, const std::string& error)
      : success(s), error_message(error) {}
};

struct ValidationResult {
  bool valid = false;
  std::vector<std::string> errors;
  std::vector<std::string> warnings;

  ValidationResult() = default;
  explicit ValidationResult(bool v) : valid(v) {}
};

class FilterChainAssembler {
public:
  explicit FilterChainAssembler(FilterRegistry& registry);
  ~FilterChainAssembler();

  AssemblyResult assembleFilterChain(
      const config::FilterChainConfig& filter_chain_config,
      const FilterCreationContext& context,
      network::FilterManager& filter_manager);

  ValidationResult validateFilterChain(const config::FilterChainConfig& config);

private:
  network::FilterSharedPtr createSingleFilter(
      const config::FilterConfig& filter_config,
      const FilterCreationContext& context);

  void validateBasicOrdering(
      const std::vector<config::FilterConfig>& filters,
      std::vector<std::string>& warnings);

  int getExpectedFilterPosition(const std::string& filter_name);

private:
  FilterRegistry& registry_;
};

class ConfigurableFilterChainFactory {
public:
  explicit ConfigurableFilterChainFactory(const config::FilterChainConfig& config);
  ~ConfigurableFilterChainFactory();

  bool createFilterChain(
      const FilterCreationContext& context,
      network::FilterManager& filter_manager);

  bool validateConfiguration() const;
  std::vector<std::string> getValidationErrors() const;

private:
  config::FilterChainConfig filter_chain_config_;
  std::unique_ptr<FilterChainAssembler> assembler_;
};

}  // namespace filter
}  // namespace mcp