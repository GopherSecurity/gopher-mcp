/**
 * @file filter_order_validator.cc
 * @brief Implementation of filter ordering model and constraints validator
 */

#include "mcp/config/filter_order_validator.h"

#include <algorithm>
#include <queue>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <iostream>

#include "mcp/logging/log_macros.h"

#define GOPHER_LOG_COMPONENT "config.order"

namespace mcp {
namespace config {

namespace {

// Helper to convert stage to string
const char* stageToString(FilterStage stage) {
  switch (stage) {
    case FilterStage::Security:
      return "Security";
    case FilterStage::QoS:
      return "QoS";
    case FilterStage::Observability:
      return "Observability";
    case FilterStage::Protocol:
      return "Protocol";
    case FilterStage::Application:
      return "Application";
    default:
      return "Unknown";
  }
}

}  // namespace

// ============================================================================
// FilterOrderValidator Implementation
// ============================================================================

FilterOrderValidator::FilterOrderValidator() : transport_type_(TransportType::Generic) {
  initializeDefaults();
  initializeTransportOrderings();
}

FilterOrderValidator::~FilterOrderValidator() = default;

void FilterOrderValidator::registerFilter(const FilterOrderMetadata& metadata) {
  filter_metadata_[metadata.filter_type] = metadata;
  GOPHER_LOG(Debug, "Registered filter '{}' at stage {}", 
                   metadata.filter_type, stageToString(metadata.stage));
}

void FilterOrderValidator::setTransportType(TransportType transport) {
  transport_type_ = transport;
  GOPHER_LOG(Debug, "Set transport type to {}", static_cast<int>(transport));
}

bool FilterOrderValidator::validate(const std::vector<filter::FilterConfig>& filters) const {
  last_errors_.clear();
  
  if (filters.empty()) {
    GOPHER_LOG(Info, "Validated empty filter chain");
    return true;
  }
  
  // Perform all validations
  ValidationResult result;
  result.merge(validateStageOrdering(filters, ""));
  result.merge(validateDependencies(filters, ""));
  result.merge(validateConstraints(filters, ""));
  result.merge(validateTransportSpecific(filters, ""));
  
  // Store errors for getErrors()
  last_errors_ = result.errors;
  
  if (result.valid) {
    GOPHER_LOG(Info, "Successfully validated filter chain with {} filters", filters.size());
  } else {
    GOPHER_LOG(Error, "Filter chain validation failed with {} errors", result.errors.size());
    for (const auto& error : result.errors) {
      GOPHER_LOG(Error, "  {}", error);
      // Also store for debugging
      std::cerr << "Validation error: " << error << std::endl;
    }
  }
  
  return result.valid;
}

ValidationResult FilterOrderValidator::validateChain(const json::JsonValue& chain_config) const {
  ValidationResult result;
  
  // Extract chain name
  std::string chain_name = "unnamed";
  if (chain_config.isObject() && chain_config.contains("name")) {
    chain_name = chain_config["name"].getString();
  }
  
  // Extract filters array
  if (!chain_config.isObject() || !chain_config.contains("filters")) {
    result.addError("Chain configuration must have 'filters' array");
    return result;
  }
  
  const auto& filters_json = chain_config["filters"];
  if (!filters_json.isArray()) {
    result.addError("'filters' must be an array");
    return result;
  }
  
  // Convert JSON to FilterConfig vector
  std::vector<filter::FilterConfig> filters;
  for (size_t i = 0; i < filters_json.size(); ++i) {
    const auto& filter_json = filters_json[i];
    
    filter::FilterConfig config;
    
    // Extract filter name/type
    if (filter_json.contains("name")) {
      config.name = filter_json["name"].getString();
    } else if (filter_json.contains("type")) {
      config.name = filter_json["type"].getString();
    } else {
      result.addError(formatError(chain_name, i, "unknown", 
                                 "Filter must have 'name' or 'type' field"));
      continue;
    }
    
    // Extract configuration
    if (filter_json.contains("config")) {
      config.config = filter_json["config"];
    }
    
    // Extract enabled flag
    if (filter_json.contains("enabled")) {
      config.enabled = filter_json["enabled"].getBool();
    }
    
    // Extract dependencies
    if (filter_json.contains("dependencies")) {
      const auto& deps = filter_json["dependencies"];
      if (deps.isArray()) {
        for (size_t j = 0; j < deps.size(); ++j) {
          config.dependencies.push_back(deps[j].getString());
        }
      }
    }
    
    // Extract conditions
    if (filter_json.contains("conditions")) {
      config.conditions = filter_json["conditions"];
    }
    
    // Extract priority
    if (filter_json.contains("priority")) {
      config.priority = filter_json["priority"].getInt();
    }
    
    filters.push_back(config);
  }
  
  // Validate the filter chain
  result.merge(validateStageOrdering(filters, chain_name));
  result.merge(validateDependencies(filters, chain_name));
  result.merge(validateConstraints(filters, chain_name));
  result.merge(validateTransportSpecific(filters, chain_name));
  
  // Log summary
  if (result.valid) {
    GOPHER_LOG(Info, "Chain '{}': validation passed ({} filters)", chain_name, filters.size());
  } else {
    GOPHER_LOG(Error, "Chain '{}': validation failed ({} errors, {} warnings)", 
                     chain_name, result.errors.size(), result.warnings.size());
  }
  
  return result;
}

std::vector<std::string> FilterOrderValidator::getErrors() const {
  return last_errors_;
}

std::vector<filter::FilterConfig> FilterOrderValidator::reorder(
    const std::vector<filter::FilterConfig>& filters) const {
  
  if (filters.empty()) {
    return filters;
  }
  
  // First, try topological sort based on dependencies
  auto sorted = topologicalSort(filters);
  if (!sorted.empty()) {
    // Then sort by stage within dependency constraints
    std::stable_sort(sorted.begin(), sorted.end(),
                    [this](const filter::FilterConfig& a, const filter::FilterConfig& b) {
                      auto stage_a = getFilterStage(a.name);
                      auto stage_b = getFilterStage(b.name);
                      if (stage_a != stage_b) {
                        return static_cast<int>(stage_a) < static_cast<int>(stage_b);
                      }
                      return a.priority < b.priority;
                    });
    
    GOPHER_LOG(Debug, "Reordered {} filters", sorted.size());
    return sorted;
  }
  
  // Fallback to stage-based ordering only
  auto result = filters;
  std::stable_sort(result.begin(), result.end(),
                  [this](const filter::FilterConfig& a, const filter::FilterConfig& b) {
                    auto stage_a = getFilterStage(a.name);
                    auto stage_b = getFilterStage(b.name);
                    if (stage_a != stage_b) {
                      return static_cast<int>(stage_a) < static_cast<int>(stage_b);
                    }
                    return a.priority < b.priority;
                  });
  
  return result;
}

bool FilterOrderValidator::hasFilter(const std::string& filter_type) const {
  return filter_metadata_.find(filter_type) != filter_metadata_.end();
}

FilterStage FilterOrderValidator::getFilterStage(const std::string& filter_type) const {
  auto it = filter_metadata_.find(filter_type);
  if (it != filter_metadata_.end()) {
    return it->second.stage;
  }
  // Default to Application stage for unknown filters
  return FilterStage::Application;
}

void FilterOrderValidator::initializeDefaults() {
  // Security stage filters
  registerFilter(FilterOrderMetadata("rate_limit", FilterStage::Security));
  registerFilter(FilterOrderMetadata("auth", FilterStage::Security));
  registerFilter(FilterOrderMetadata("authorization", FilterStage::Security));
  
  // QoS stage filters
  registerFilter(FilterOrderMetadata("circuit_breaker", FilterStage::QoS));
  registerFilter(FilterOrderMetadata("backpressure", FilterStage::QoS));
  registerFilter(FilterOrderMetadata("flow_control", FilterStage::QoS));
  
  // Observability stage filters
  registerFilter(FilterOrderMetadata("metrics", FilterStage::Observability));
  registerFilter(FilterOrderMetadata("tracing", FilterStage::Observability));
  registerFilter(FilterOrderMetadata("logging", FilterStage::Observability));
  registerFilter(FilterOrderMetadata("access_log", FilterStage::Observability));
  
  // Protocol stage filters
  FilterOrderMetadata http_codec("http_codec", FilterStage::Protocol);
  http_codec.before = {"sse_codec"};  // Don't add json_rpc here - stage ordering handles it
  registerFilter(http_codec);
  
  FilterOrderMetadata sse_codec("sse_codec", FilterStage::Protocol);
  sse_codec.requires = {"http_codec"};
  registerFilter(sse_codec);
  
  registerFilter(FilterOrderMetadata("websocket", FilterStage::Protocol));
  registerFilter(FilterOrderMetadata("grpc", FilterStage::Protocol));
  
  // Application stage filters
  FilterOrderMetadata json_rpc("json_rpc", FilterStage::Application);
  // Don't add after constraint - stage ordering handles protocol -> application
  registerFilter(json_rpc);
  
  registerFilter(FilterOrderMetadata("router", FilterStage::Application));
  registerFilter(FilterOrderMetadata("custom", FilterStage::Application));
  
  GOPHER_LOG(Info, "Initialized default filter metadata ({} filters)", filter_metadata_.size());
}

void FilterOrderValidator::clear() {
  filter_metadata_.clear();
  last_errors_.clear();
  GOPHER_LOG(Debug, "Cleared all filter metadata");
}

std::vector<std::string> FilterOrderValidator::getRecommendedOrdering(
    TransportType transport) const {
  
  auto it = transport_orderings_.find(transport);
  if (it != transport_orderings_.end()) {
    return it->second;
  }
  
  // Return generic ordering
  return {
    "rate_limit",
    "circuit_breaker",
    "metrics",
    "http_codec",
    "json_rpc"
  };
}

// ============================================================================
// Private Methods
// ============================================================================

ValidationResult FilterOrderValidator::validateStageOrdering(
    const std::vector<filter::FilterConfig>& filters,
    const std::string& chain_name) const {
  
  ValidationResult result;
  
  FilterStage prev_stage = FilterStage::Security;
  std::string prev_filter;
  
  for (size_t i = 0; i < filters.size(); ++i) {
    const auto& filter = filters[i];
    
    if (!filter.enabled) {
      continue;  // Skip disabled filters
    }
    
    FilterStage current_stage = getFilterStage(filter.name);
    
    if (i > 0 && static_cast<int>(current_stage) < static_cast<int>(prev_stage)) {
      result.addError(formatError(chain_name, i, filter.name,
          "Stage violation: " + std::string(stageToString(current_stage)) + 
          " filter cannot come before " + std::string(stageToString(prev_stage)) + 
          " filter '" + prev_filter + "'"));
    }
    
    prev_stage = current_stage;
    prev_filter = filter.name;
  }
  
  return result;
}

ValidationResult FilterOrderValidator::validateDependencies(
    const std::vector<filter::FilterConfig>& filters,
    const std::string& chain_name) const {
  
  ValidationResult result;
  
  // Build set of available filters
  std::set<std::string> available;
  for (const auto& filter : filters) {
    if (filter.enabled) {
      available.insert(filter.name);
    }
  }
  
  // Check each filter's dependencies
  for (size_t i = 0; i < filters.size(); ++i) {
    const auto& filter = filters[i];
    
    if (!filter.enabled) {
      continue;
    }
    
    // Check explicit dependencies
    for (const auto& dep : filter.dependencies) {
      if (available.find(dep) == available.end()) {
        result.addError(formatError(chain_name, i, filter.name,
            "Missing required dependency: '" + dep + "'"));
      }
    }
    
    // Check metadata-based dependencies
    auto it = filter_metadata_.find(filter.name);
    if (it != filter_metadata_.end()) {
      for (const auto& req : it->second.requires) {
        if (available.find(req) == available.end()) {
          result.addError(formatError(chain_name, i, filter.name,
              "Missing required filter: '" + req + "'"));
        }
      }
    }
  }
  
  return result;
}

ValidationResult FilterOrderValidator::validateConstraints(
    const std::vector<filter::FilterConfig>& filters,
    const std::string& chain_name) const {
  
  ValidationResult result;
  
  // Build position map
  std::unordered_map<std::string, size_t> positions;
  for (size_t i = 0; i < filters.size(); ++i) {
    if (filters[i].enabled) {
      positions[filters[i].name] = i;
    }
  }
  
  // Check before/after constraints
  for (size_t i = 0; i < filters.size(); ++i) {
    const auto& filter = filters[i];
    
    if (!filter.enabled) {
      continue;
    }
    
    auto it = filter_metadata_.find(filter.name);
    if (it == filter_metadata_.end()) {
      continue;
    }
    
    const auto& metadata = it->second;
    
    // Check "before" constraints (only if the referenced filter exists)
    for (const auto& before : metadata.before) {
      auto pos_it = positions.find(before);
      if (pos_it != positions.end() && pos_it->second < i) {
        result.addError(formatError(chain_name, i, filter.name,
            "Constraint violation: must come before '" + before + "'"));
      }
    }
    
    // Check "after" constraints (only if the referenced filter exists)
    for (const auto& after : metadata.after) {
      auto pos_it = positions.find(after);
      if (pos_it != positions.end() && pos_it->second > i) {
        result.addError(formatError(chain_name, i, filter.name,
            "Constraint violation: must come after '" + after + "'"));
      }
    }
  }
  
  return result;
}

ValidationResult FilterOrderValidator::validateTransportSpecific(
    const std::vector<filter::FilterConfig>& filters,
    const std::string& chain_name) const {
  
  ValidationResult result;
  
  // Build set of filter types
  std::set<std::string> filter_types;
  for (const auto& filter : filters) {
    if (filter.enabled) {
      filter_types.insert(filter.name);
    }
  }
  
  switch (transport_type_) {
    case TransportType::HTTP:
      // HTTP transport must have http_codec
      if (filter_types.find("http_codec") == filter_types.end()) {
        result.addWarning("HTTP transport should include 'http_codec' filter");
      }
      break;
      
    case TransportType::HTTPWithSSE:
      // HTTP+SSE must have both http_codec and sse_codec
      if (filter_types.find("http_codec") == filter_types.end()) {
        result.addError("HTTP+SSE transport requires 'http_codec' filter");
      }
      if (filter_types.find("sse_codec") == filter_types.end()) {
        result.addError("HTTP+SSE transport requires 'sse_codec' filter");
      }
      
      // Verify ordering: http_codec before sse_codec
      if (filter_types.count("http_codec") && filter_types.count("sse_codec")) {
        size_t http_pos = SIZE_MAX, sse_pos = SIZE_MAX;
        for (size_t i = 0; i < filters.size(); ++i) {
          if (filters[i].enabled) {
            if (filters[i].name == "http_codec") http_pos = i;
            if (filters[i].name == "sse_codec") sse_pos = i;
          }
        }
        if (http_pos > sse_pos) {
          result.addError("Transport-specific violation: 'http_codec' must come before 'sse_codec'");
        }
      }
      break;
      
    case TransportType::Stdio:
      // Stdio typically uses json_rpc directly
      if (filter_types.find("json_rpc") == filter_types.end()) {
        result.addWarning("Stdio transport typically includes 'json_rpc' filter");
      }
      // Should not have HTTP filters
      if (filter_types.find("http_codec") != filter_types.end()) {
        result.addWarning("Stdio transport should not include 'http_codec' filter");
      }
      break;
      
    case TransportType::WebSocket:
      // WebSocket specific validation
      if (filter_types.find("websocket") == filter_types.end()) {
        result.addWarning("WebSocket transport should include 'websocket' filter");
      }
      break;
      
    case TransportType::TCP:
    case TransportType::Generic:
    default:
      // No specific requirements
      break;
  }
  
  return result;
}

std::vector<filter::FilterConfig> FilterOrderValidator::topologicalSort(
    const std::vector<filter::FilterConfig>& filters) const {
  
  // Build adjacency list and in-degree map
  std::unordered_map<std::string, std::vector<std::string>> adj;
  std::unordered_map<std::string, int> in_degree;
  std::unordered_map<std::string, filter::FilterConfig> filter_map;
  
  // Initialize
  for (const auto& filter : filters) {
    if (!filter.enabled) continue;
    
    filter_map[filter.name] = filter;
    in_degree[filter.name] = 0;
    adj[filter.name] = {};
  }
  
  // Build graph from dependencies
  for (const auto& filter : filters) {
    if (!filter.enabled) continue;
    
    // Add edges from dependencies
    for (const auto& dep : filter.dependencies) {
      if (filter_map.find(dep) != filter_map.end()) {
        adj[dep].push_back(filter.name);
        in_degree[filter.name]++;
      }
    }
    
    // Add edges from metadata
    auto it = filter_metadata_.find(filter.name);
    if (it != filter_metadata_.end()) {
      for (const auto& req : it->second.requires) {
        if (filter_map.find(req) != filter_map.end()) {
          adj[req].push_back(filter.name);
          in_degree[filter.name]++;
        }
      }
    }
  }
  
  // Kahn's algorithm for topological sort
  std::queue<std::string> queue;
  for (const auto& pair : in_degree) {
    if (pair.second == 0) {
      queue.push(pair.first);
    }
  }
  
  std::vector<filter::FilterConfig> result;
  while (!queue.empty()) {
    std::string current = queue.front();
    queue.pop();
    
    result.push_back(filter_map[current]);
    
    for (const auto& neighbor : adj[current]) {
      in_degree[neighbor]--;
      if (in_degree[neighbor] == 0) {
        queue.push(neighbor);
      }
    }
  }
  
  // Check for cycle
  if (result.size() != filter_map.size()) {
    GOPHER_LOG(Error, "Dependency cycle detected in filter chain");
    return {};  // Return empty on cycle
  }
  
  return result;
}

void FilterOrderValidator::initializeTransportOrderings() {
  // HTTP transport
  transport_orderings_[TransportType::HTTP] = {
    "rate_limit",
    "circuit_breaker",
    "metrics",
    "http_codec",
    "router",
    "json_rpc"
  };
  
  // HTTP with SSE
  transport_orderings_[TransportType::HTTPWithSSE] = {
    "rate_limit",
    "circuit_breaker",
    "metrics",
    "http_codec",
    "sse_codec",
    "json_rpc"
  };
  
  // Stdio transport
  transport_orderings_[TransportType::Stdio] = {
    "rate_limit",
    "metrics",
    "json_rpc"
  };
  
  // WebSocket transport
  transport_orderings_[TransportType::WebSocket] = {
    "rate_limit",
    "circuit_breaker",
    "metrics",
    "websocket",
    "json_rpc"
  };
  
  // TCP transport
  transport_orderings_[TransportType::TCP] = {
    "rate_limit",
    "circuit_breaker",
    "metrics",
    "json_rpc"
  };
}

std::string FilterOrderValidator::formatError(const std::string& chain_name,
                                              size_t filter_index,
                                              const std::string& filter_name,
                                              const std::string& message) const {
  std::ostringstream oss;
  
  if (!chain_name.empty()) {
    oss << "Chain '" << chain_name << "': ";
  }
  
  oss << "Filter[" << filter_index << "] '" << filter_name << "': " << message;
  
  return oss.str();
}

// ============================================================================
// Helper Functions
// ============================================================================

std::shared_ptr<FilterOrderValidator> createDefaultValidator() {
  auto validator = std::make_shared<FilterOrderValidator>();
  validator->initializeDefaults();
  return validator;
}

const char* getFilterStageName(FilterStage stage) {
  return stageToString(stage);
}

}  // namespace config
}  // namespace mcp