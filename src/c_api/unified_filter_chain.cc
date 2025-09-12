/**
 * @file unified_filter_chain.cc
 * @brief Implementation of unified filter chain wrapper
 */

#include "unified_filter_chain.h"

#include <sstream>

// Forward declare the actual chain classes to avoid including their full headers
namespace mcp {
namespace filter_chain {
class AdvancedFilterChain;
}
namespace filter_api {
class FilterChain;
}
}

namespace mcp {
namespace c_api_internal {

bool UnifiedFilterChain::pause() {
  if (type_ == ChainType::Advanced && advanced_chain_) {
    // AdvancedFilterChain has pause method, but we need its definition
    // For now, return false to indicate not implemented
    // This will be fixed when we properly include the header
    return false;
  }
  return false;  // Simple chains don't support pause
}

bool UnifiedFilterChain::resume() {
  if (type_ == ChainType::Advanced && advanced_chain_) {
    // AdvancedFilterChain has resume method, but we need its definition
    // For now, return false to indicate not implemented
    // This will be fixed when we properly include the header
    return false;
  }
  return false;  // Simple chains don't support resume
}

mcp_chain_state_t UnifiedFilterChain::getState() const {
  if (type_ == ChainType::Advanced && advanced_chain_) {
    // AdvancedFilterChain has getState method
    // For now, return error state
    return MCP_CHAIN_STATE_ERROR;
  }
  return MCP_CHAIN_STATE_ERROR;  // Simple chains don't have state
}

std::string UnifiedFilterChain::dump(const std::string& format) const {
  std::ostringstream oss;
  
  if (type_ == ChainType::Advanced && advanced_chain_) {
    // Advanced chains have their own dump implementation
    // For now, return a placeholder
    if (format == "json") {
      oss << "{\"type\":\"advanced\",\"chain\":\"<advanced_chain>\"}";
    } else if (format == "dot") {
      oss << "digraph advanced_chain { label=\"Advanced Chain\"; }";
    } else {
      oss << "Advanced Filter Chain\n";
    }
  } else if (type_ == ChainType::Simple && simple_chain_) {
    // Simple chains need a basic dump implementation
    if (format == "json") {
      oss << "{\"type\":\"simple\",\"filters\":[]}";
    } else if (format == "dot") {
      oss << "digraph simple_chain { label=\"Simple Chain\"; }";
    } else {
      oss << "Simple Filter Chain\n";
    }
  } else {
    oss << "Invalid chain";
  }
  
  return oss.str();
}

}  // namespace c_api_internal
}  // namespace mcp