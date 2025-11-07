/**
 * @file filter_chain_assembler.cc
 * @brief Implementation of simple filter chain assembly system
 */

#include "mcp/filter/filter_chain_assembler.h"

#include <iostream>

#include "mcp/filter/filter_chain_event_hub.h"
#include "mcp/filter/filter_event_emitter.h"

namespace mcp {
namespace filter {

FilterChainAssembler::FilterChainAssembler(FilterRegistry& registry)
    : registry_(registry) {
  // FilterChainAssembler created
}

FilterChainAssembler::~FilterChainAssembler() {
  // FilterChainAssembler destroyed
}

AssemblyResult FilterChainAssembler::assembleFilterChain(
    const config::FilterChainConfig& filter_chain_config,
    const FilterCreationContext& context,
    network::FilterManager& filter_manager) {

  AssemblyResult result;
  std::vector<network::FilterSharedPtr> created_filters;

  // Assembling filter chain

  // 1. Simple validation first
  auto validation = validateFilterChain(filter_chain_config);
  if (!validation.valid) {
    result.success = false;
    result.error_message = "Filter chain validation failed";
    return result;
  }

  // Include warnings from validation
  result.warnings = validation.warnings;

  // 2. Create filters in order (simple approach)
  for (const auto& filter_config : filter_chain_config.filters) {
    try {
      auto filter = createSingleFilter(filter_config, context);
      if (!filter) {
        result.success = false;
        result.error_message = "Failed to create filter: " + filter_config.name;
        return result;
      }

      created_filters.push_back(filter);
      result.created_filters.push_back(filter_config.name);

      // Created filter in chain

    } catch (const std::exception& e) {
      result.success = false;
      result.error_message = "Failed to create filter '" + filter_config.name + "': " + e.what();
      return result;
    }
  }

  // 3. Add filters to manager in order (no complex wiring needed)
  for (auto& filter : created_filters) {
    filter_manager.addFilter(filter);
  }

  result.success = true;
  // Assembled filter chain
  return result;
}

ValidationResult FilterChainAssembler::validateFilterChain(
    const config::FilterChainConfig& config) {

  ValidationResult result;
  result.valid = true;

  // Validating filter chain

  // Basic validation checks
  if (config.filters.empty()) {
    result.valid = true;
    return result;
  }

  // Check that all filter types exist in registry
  for (const auto& filter_config : config.filters) {
    if (!registry_.hasContextFactory(filter_config.type) &&
        !registry_.hasFactory(filter_config.type)) {
      result.valid = false;
      result.errors.push_back("Unknown filter type: " + filter_config.type);
    }
  }

  // Simple ordering validation for HTTP+SSE+JSON-RPC
  validateBasicOrdering(config.filters, result.warnings);

  if (result.valid) {
    // Filter chain validation successful
  } else {
    // Filter chain validation failed
  }

  return result;
}

network::FilterSharedPtr FilterChainAssembler::createSingleFilter(
    const config::FilterConfig& filter_config,
    const FilterCreationContext& context) {

  FilterCreationContext local_context = context;

  if (context.event_hub && !filter_config.type.empty()) {
    try {
      auto hub =
          std::static_pointer_cast<FilterChainEventHub>(context.event_hub);
      if (hub) {
        std::string filter_name = filter_config.type;
        std::string instance_id = filter_config.name;

        auto emitter = std::make_shared<FilterEventEmitter>(
            hub,
            filter_name,
            instance_id);

        local_context.event_emitter =
            std::shared_ptr<void>(emitter, emitter.get());
      }
    } catch (const std::exception& ex) {
      std::cerr << "[FilterChainAssembler] Failed to create event emitter: "
                << ex.what() << std::endl;
    }
  }

  if (registry_.hasContextFactory(filter_config.type)) {
    return registry_.createFilterWithContext(filter_config.type, local_context,
                                             filter_config.config);
  }

  return registry_.createFilter(filter_config.type, filter_config.config);
}

void FilterChainAssembler::validateBasicOrdering(
    const std::vector<config::FilterConfig>& filters,
    std::vector<std::string>& warnings) {

  if (filters.size() < 2) {
    return; // No ordering issues with single filter
  }

  // Check for expected HTTP->SSE->JSON-RPC ordering
  for (size_t i = 0; i < filters.size() - 1; ++i) {
    int current_pos = getExpectedFilterPosition(filters[i].type);
    int next_pos = getExpectedFilterPosition(filters[i + 1].type);

    if (current_pos > next_pos && current_pos != 999 && next_pos != 999) {
      warnings.push_back("Filter ordering may be incorrect: '" +
                         filters[i].type + "' should come after '" +
                         filters[i + 1].type + "'");
    }
  }

  // Check for specific expected ordering for core filter sequence
  // Find the positions of core filters if they exist
  int http_pos = -1, sse_pos = -1, json_rpc_pos = -1;
  for (size_t i = 0; i < filters.size(); ++i) {
    if (filters[i].type == "http.codec") http_pos = i;
    else if (filters[i].type == "sse.codec") sse_pos = i;
    else if (filters[i].type == "json_rpc.dispatcher") json_rpc_pos = i;
  }

  // If we have HTTP and SSE codecs, ensure proper ordering
  if (http_pos >= 0 && sse_pos >= 0 && http_pos > sse_pos) {
    warnings.push_back("'http.codec' should come before 'sse.codec'");
  }
  if (sse_pos >= 0 && json_rpc_pos >= 0 && sse_pos > json_rpc_pos) {
    warnings.push_back("'sse.codec' should come before 'json_rpc.dispatcher'");
  }
}

int FilterChainAssembler::getExpectedFilterPosition(const std::string& filter_name) {
  // Define standard ordering for known filter types
  if (filter_name == "http.codec") return 10;
  if (filter_name == "sse.codec") return 20;
  if (filter_name == "json_rpc.dispatcher") return 30;

  // QoS filters typically come first
  if (filter_name == "rate_limit") return 5;
  if (filter_name == "circuit_breaker") return 6;
  if (filter_name == "metrics") return 7;
  if (filter_name == "backpressure") return 8;

  return 999; // Unknown filter
}

// ConfigurableFilterChainFactory Implementation

ConfigurableFilterChainFactory::ConfigurableFilterChainFactory(
    const config::FilterChainConfig& config)
    : filter_chain_config_(config),
      assembler_(std::make_unique<FilterChainAssembler>(FilterRegistry::instance())) {
  // ConfigurableFilterChainFactory created
}

ConfigurableFilterChainFactory::~ConfigurableFilterChainFactory() {
  // ConfigurableFilterChainFactory destroyed
}

bool ConfigurableFilterChainFactory::createFilterChain(
    const FilterCreationContext& context,
    network::FilterManager& filter_manager) {

  std::cerr << "Creating filter chain from configuration" << std::endl;

  auto result = assembler_->assembleFilterChain(
      filter_chain_config_, context, filter_manager);

  if (!result.success) {
    std::cerr << "Failed to create filter chain: " << result.error_message << std::endl;
  } else {
    std::cerr << "Successfully created filter chain with " << result.created_filters.size() << " filters" << std::endl;
  }

  return result.success;
}

bool ConfigurableFilterChainFactory::validateConfiguration() const {
  auto result = assembler_->validateFilterChain(filter_chain_config_);
  return result.valid;
}

std::vector<std::string> ConfigurableFilterChainFactory::getValidationErrors() const {
  auto result = assembler_->validateFilterChain(filter_chain_config_);
  return result.errors;
}

}  // namespace filter
}  // namespace mcp
